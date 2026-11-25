/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/Looper.h>
#include <ngui/MessageRunner.h>
#include <ngui/Messenger.h>
#include <ngui/platform/Platform.h>


namespace ngui {


class BCursor;


/*!	The application (module G4), as Haiku's BApplication: a looper whose
	loop runs on the thread that calls Run(), the main thread, as macOS and
	SDL require. There is one per process.

	\code
	int
	main(int argc, char** argv)
	{
		MyApp app("application/x-vnd.example", argc, argv);
		return app.Run();
	}
	\endcode

	Differences to Haiku, from the rules on ownership and threads
	(CONVENTIONS.md 3.3, 3.4):
	- The application belongs to main: an object on its stack, no `new`
	  and no `delete be_app`. Run() blocks until the application quits and
	  returns an exit code instead of a thread id.
	- There is no global `be_app` pointer. Other threads reach the
	  application through AppMessenger(), Haiku's be_app_messenger.
	- ArgvReceived() takes the arguments as strings.
	- The pulse is timed by Clock::Default() (rule T1).
	- Windows are reached through messengers (Windows()); windows left when
	  Run() ends are quit without being asked, and Run() waits for them.
	- The cursor functions are static (there is no be_app to call them on):
	  any thread may call them, and they act on the process's cursor.
*/
class BApplication : public BLooper {
public:
	/*!	The default pulse rate of Haiku: 0.5 s. */
	static constexpr Clock::Time kDefaultPulseRate = Clock::Time(500000);

								BApplication(std::string_view signature, int argc = 0,
									char** argv = nullptr);
									/*!< The application with MIME type \a signature;
										\a argc and \a argv as main got them. InitCheck()
										tells whether it can run. */
								~BApplication() override;
									/*!< Ends AppMessenger(). */

			std::expected<void, Error> UsePlatform(
									std::unique_ptr<platform::Platform> platform);
									/*!< The window system for this application, before
										Run(); its loop then waits for the platform's events
										and the messages together, and platform::Current()
										gives it to other threads. InvalidArgument for
										nullptr or while it runs. */
			platform::Platform*	Platform() const;
									/*!< The platform, nullptr without one. */

			std::expected<void, Error> InitCheck() const;
									/*!< InvalidArgument for a signature that is not an
										application MIME type ("application/..."), or if
										another application exists in the process. */
			int					Run();
									/*!< Runs the loop on the calling thread until the
										application quits; first ArgvReceived() (if there
										are arguments) and ReadyToRun(). 0, or 1 if
										InitCheck() fails. */

	virtual	void				ArgvReceived(std::span<const std::string> arguments);
									/*!< The arguments after the program name. */
	virtual	void				ReadyToRun();
									/*!< The application is about to process messages. */
	virtual	void				Pulse();
									/*!< Called at the pulse rate. */
	virtual	void				AboutRequested();
									/*!< The user asks for the about box; nothing by
										default until alerts exist (W6). */
			bool				QuitRequested() override;
									/*!< Asks the windows in the order they were made, as
										Haiku's: each that agrees quits at once, the first
										that refuses ends the asking with false. True if all
										agreed. Waits for each answer, so a window must not
										wait for the application meanwhile. */
			std::int32_t		CountWindows() const;
									/*!< The windows of the application. */
			std::vector<BMessenger> Windows() const;
									/*!< Messengers to its windows, in the order they were
										made; there are no pointers to them (CONVENTIONS.md
										3.4). */

	// Cursor control

	static	void				SetCursor(const BCursor& cursor);
									/*!< The cursor shown where no view has one of its own
										(BView::SetViewCursor()); the arrow by default. */
	static	void				ShowCursor();
									/*!< Undoes one HideCursor(). */
	static	void				HideCursor();
									/*!< Hides the cursor until as many ShowCursor()
										calls. */
	static	void				ObscureCursor();
									/*!< Hides the cursor until the mouse moves. */
	static	bool				IsCursorHidden();
									/*!< HideCursor() was called more often than
										ShowCursor(). */

			void				SetPulseRate(Clock::Time rate);
									/*!< The interval of Pulse(); 0 stops it. */
			Clock::Time			PulseRate() const;
									/*!< The interval of Pulse(), 0 if stopped. */
			bool				IsLaunching() const;
									/*!< True until ReadyToRun() has returned. */
			const std::string&	Signature() const;
									/*!< The MIME type given to the constructor. */

			void				DispatchMessage(BMessage* message, BHandler* handler) override;
									/*!< The system commands of AppDefs.h, then as
										BLooper's. */

private:
			class PlatformEvents;

			std::string			fSignature;
			std::unique_ptr<platform::Platform> fPlatform;
			std::vector<std::string> fArguments;
			Clock::Time			fPulseRate = kDefaultPulseRate;
			std::unique_ptr<BMessageRunner> fPulseRunner;
			bool				fLaunching = true;
			bool				fRegistered = false;
			bool				fValidSignature;
};


/*!	The messenger of the application, from any thread: Haiku's
	be_app_messenger. Invalid when there is no application or it has quit. */
BMessenger						AppMessenger();


} // namespace ngui
