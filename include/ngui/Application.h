/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <filesystem>
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

namespace remote::detail {
class RemoteServer;
}


/*!	Which colors the application shows (Haiku's light or dark ui colors). */
enum class BColorScheme : std::uint8_t {
	System,	/*!< As the user chose for the system (light without a choice). */
	Light,	/*!< Haiku's light colors (Palette::Default()). */
	Dark	/*!< Haiku's dark colors (Palette::Dark()). */
};


/*!	Who draws the frames of the windows (title tab, borders). */
enum class BWindowFrames : std::uint8_t {
	System,	/*!< The window system, as it draws them for other programs. */
	Look	/*!< NewGUI, as the look's decorator: Haiku's (the yellow tab of
				"be") where the platform has transparent windows and the look a
				decorator, the window system's otherwise. */
};


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
			std::expected<void, Error> UsePlatform(std::string_view name);
									/*!< The platform \a name of
										platform::PlatformRegistry::Default(), or with an
										empty name the one it prefers (NGUI_PLATFORM, e.g.
										"headless" for tests, else the first that can
										run); its error otherwise. */
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
	static	void				SetWindowFrames(BWindowFrames frames);
									/*!< NewGUI: who draws the frames of the windows made
										from now on; System by default, Look with the
										environment variable NGUI_WINDOW_FRAMES=look. */
	static	BWindowFrames		WindowFrames();
									/*!< Who draws the frames of new windows. */
	static	void				SetColorScheme(BColorScheme scheme);
									/*!< NewGUI: which colors the application shows
										(Palette::Current()); System by default, or as the
										environment variable NGUI_COLOR_SCHEME=light, dark
										or system says. With System the colors follow the
										user's choice while the application runs. */
	static	BColorScheme		ColorScheme();
									/*!< Which colors the application shows. */
			Clock::Time			PulseRate() const;
									/*!< The interval of Pulse(), 0 if stopped. */
			bool				IsLaunching() const;
									/*!< True until ReadyToRun() has returned. */
			const std::string&	Signature() const;
									/*!< The MIME type given to the constructor. */

			void				DispatchMessage(BMessage* message, BHandler* handler) override;
									/*!< The system commands of AppDefs.h, then as
										BLooper's. */

	// Scripting (specification 20.7)

			void				MessageReceived(BMessage* message) override;
									/*!< Answers "Name" (its signature) and the count of
										"Window"; otherwise as BLooper's. */
			BHandler*			ResolveSpecifier(BMessage* message, std::int32_t index,
									const BMessage& specifier, std::uint32_t form,
									std::string_view property) override;
									/*!< "Window" by index, reverse index or title passes the
										message on to that window (Haiku forwards it so);
										otherwise as BLooper's. */
			std::expected<void, Error> GetSupportedSuites(BMessage* data) override;
									/*!< "suite/vnd.Be-application" and BLooper's. */

	// Remote control (specification 20.7, rule T4)

			std::expected<std::filesystem::path, Error> EnableRemoteControl(
									const std::filesystem::path& directory = {});
									/*!< Lets other processes of the user send scripting
										messages (ngui/RemoteControl.h): listens at
										remote::EndpointPath() in \a directory
										(remote::DefaultDirectory() if empty) and returns the
										socket. Off until this is called or the application
										starts with NGUI_REMOTE_CONTROL=1. Again moves it. */
			void				DisableRemoteControl();
									/*!< Stops listening and removes the socket. */
			std::filesystem::path RemoteControlPath() const;
									/*!< The socket, empty while remote control is off. */

	// Record & Replay (specification 20.8, rule T5)

			std::expected<void, Error> StartRecording(const std::filesystem::path& file);
									/*!< Records the events of the platform into \a file
										(ngui/Recording.h), only the user may read it, until
										StopRecording(): what they type is in it. Call it only
										after the user agreed (rule T5); starting with
										NGUI_RECORD=\<file\> is that agreement. Typing into
										password fields and views marked private
										(BView::SetInputPrivate()) is masked. InvalidArgument
										while a recording runs, IoError if \a file cannot be
										written. Test::Driver::Replay() plays it back. */
			void				StopRecording();
									/*!< Ends the recording and closes its file (also when
										the application goes). */
			bool				IsRecording() const;
									/*!< Whether a recording runs. */

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
			std::unique_ptr<remote::detail::RemoteServer> fRemote;
			bool				fRecording = false;
};


/*!	The messenger of the application, from any thread: Haiku's
	be_app_messenger. Invalid when there is no application or it has quit. */
BMessenger						AppMessenger();


} // namespace ngui
