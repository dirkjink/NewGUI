/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Handler.h>
#include <ngui/Locker.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


class LooperPort;


/*!	A thread with a message queue and a chain of handlers (module G3), as
	Haiku's BLooper: it takes one message after the other and dispatches it
	to its target handler, locked.

	Differences to Haiku, from the rules on ownership and threads
	(CONVENTIONS.md 3.3 and 3.4):
	- **The thread owns the looper.** Run() takes it as std::unique_ptr and
	  returns a BMessenger; the thread destroys the looper when its loop
	  ends. There is no `new` and no `delete this`.
	- **Other threads use messages.** They quit a looper with
	  kQuitRequested through its messenger. Quit(), Lock(), AddHandler()
	  and PostMessage() belong to the looper's own thread, or to the time
	  before Run(); elsewhere they are programming errors (an assertion in
	  debug builds).
	- The queue has no capacity limit and no priority.

	\code
	BMessenger looper = *BLooper::Run(std::make_unique<MyLooper>());
	looper.SendMessage(kQuitRequested);
	looper.WaitForLooperQuit();
	\endcode
*/
class BLooper : public BHandler {
public:
	explicit					BLooper(std::string_view name = {});
									/*!< A looper that does not run yet; it is its own first
										handler. */
								~BLooper() override;
									/*!< Detaches its handlers; on its own thread once it
										ran. */

	static	std::expected<BMessenger, Error> Run(std::unique_ptr<BLooper> looper);
									/*!< Starts the thread of \a looper, which owns it from
										now on; the messenger targets the looper itself, as
										Haiku's BMessenger(looper), so kQuitRequested through
										it quits. InvalidArgument for nullptr. */

			void				Quit();
									/*!< Ends the loop after the current message; queued
										messages are dropped. On the looper's thread. */
	virtual	bool				QuitRequested();
									/*!< Asked by kQuitRequested; true lets it quit. */
	virtual	void				DispatchMessage(BMessage* message, BHandler* handler);
									/*!< Calls \a handler; kQuitRequested to the looper itself
										asks QuitRequested(), replying with the bool
										"result" to a waiting sender. */
			void				MessageReceived(BMessage* message) override;
									/*!< As BHandler's. */

			std::expected<void, Error> PostMessage(std::uint32_t command);
									/*!< Queues a message for the preferred handler. */
			std::expected<void, Error> PostMessage(const BMessage& message,
									BHandler* handler = nullptr, BHandler* replyTo = nullptr);
									/*!< Queues a copy for \a handler (nullptr: the preferred
										one); InvalidArgument if \a handler is in another
										looper. */

			void				AddHandler(BHandler* handler);
									/*!< Adds \a handler, not owned; its next handler becomes
										this looper. */
			bool				RemoveHandler(BHandler* handler);
									/*!< Removes \a handler; false if it is not here. The
										looper itself cannot be removed. */
			std::int32_t		CountHandlers() const;
									/*!< Its handlers, itself included. */
			BHandler*			HandlerAt(std::int32_t index) const;
									/*!< Handler \a index, nullptr outside. */
			std::int32_t		IndexOf(const BHandler* handler) const;
									/*!< The index of \a handler, -1 if it is not here. */
			BHandler*			PreferredHandler() const;
									/*!< Where messages without a handler go; nullptr means
										the looper itself. */
			void				SetPreferredHandler(BHandler* handler);
									/*!< Sets it; \a handler must be in this looper. */

			BMessage*			CurrentMessage() const;
									/*!< The message being dispatched, nullptr between them. */
			std::unique_ptr<BMessage> DetachCurrentMessage();
									/*!< Takes the current message, e.g. to reply later. */

			bool				Lock();
									/*!< Locks it recursively; on its own thread. */
			void				Unlock();
									/*!< Unlocks one level. */
			bool				IsLocked() const;
									/*!< Whether the calling thread holds the lock. */
			std::thread::id		Thread() const;
									/*!< Its thread; std::thread::id() before Run(). */

private:
	friend class BHandler;
	friend class BMessenger;

			bool				_OnOwnThread() const;
			BHandler*			_Target(const BMessage& message) const;
			void				_Loop();

			std::shared_ptr<LooperPort> fPort;
			BLocker				fLock;
			std::vector<BHandler*> fHandlers;
			BHandler*			fPreferred = nullptr;
			std::unique_ptr<BMessage> fCurrent;
			std::thread::id		fThread;
			bool				fRunning = false;
			bool				fQuitting = false;
};


} // namespace ngui
