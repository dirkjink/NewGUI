/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <cstdint>
#include <expected>
#include <memory>

#include <ngui/Error.h>


namespace ngui {


class BHandler;
class BLooper;
class BMessage;
class LooperPort;


/*!	The address of a handler in a looper (module G3), as Haiku's BMessenger:
	a small value that any thread may copy and send messages to. It stays
	safe when the looper is gone; sending then fails with NotFound.

	Differences to Haiku: only targets in the same process; no Target() and
	no LockTarget(), since other threads reach a looper only through
	messages (CONVENTIONS.md 3.4); WaitForLooperQuit() is new.
*/
class BMessenger {
public:
	/*!	Wait without a time limit (B_INFINITE_TIMEOUT). */
	static constexpr std::chrono::microseconds kInfiniteTimeout
		= std::chrono::microseconds::max();

								BMessenger() = default;
									/*!< An invalid messenger. */
	explicit					BMessenger(const BHandler* handler,
									const BLooper* looper = nullptr);
									/*!< Targets \a handler in its looper, or, if \a handler
										is nullptr, the preferred handler of \a looper;
										invalid if neither is in a looper. On the looper's
										thread, or before it runs. */

			bool				IsValid() const;
									/*!< The looper exists and accepts messages. */
			bool				IsTargetLocal() const;
									/*!< Always true: there are only local targets. */

			std::expected<void, Error> SendMessage(std::uint32_t command,
									BHandler* replyTo = nullptr) const;
									/*!< Sends a message with what = \a command; replies go
										to \a replyTo. */
			std::expected<void, Error> SendMessage(const BMessage& message) const;
									/*!< Sends a copy of \a message; NotFound if the looper
										is gone. */
			std::expected<void, Error> SendMessage(const BMessage& message,
									const BMessenger& replyTo) const;
									/*!< As above; replies go to \a replyTo. */
			std::expected<void, Error> SendMessage(const BMessage& message, BMessage* reply,
									std::chrono::microseconds replyTimeout
										= kInfiniteTimeout) const;
									/*!< Sends a copy of \a message and waits for the reply,
										kNoReply if the message is destroyed unanswered.
										TimedOut after \a replyTimeout; InvalidArgument on
										the target's own thread, where it would deadlock. */

			std::expected<void, Error> WaitForLooperQuit(
									std::chrono::microseconds timeout = kInfiniteTimeout)
									const;
									/*!< Waits until the target's looper has ended and its
										object is destroyed; TimedOut after \a timeout. */

			bool				operator==(const BMessenger& other) const;
									/*!< Same looper and same handler. */

private:
	friend class BLooper;
	friend class BMessage;

			std::expected<void, Error> _Send(std::unique_ptr<BMessage> message) const;

			std::weak_ptr<LooperPort> fPort;
			std::int32_t		fToken = 0;
};


} // namespace ngui
