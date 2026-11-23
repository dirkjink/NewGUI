/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>

#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


/*!	Sends a message to a target at regular intervals (module G3), as
	Haiku's BMessageRunner, timed by a Clock (rule T1): the first message
	one interval after construction, then every interval, \a count times or
	until the runner is destroyed.

	Differences to Haiku: the clock is a parameter (Clock::Default() if
	omitted); there is no be_app_messenger as default reply target;
	StartSending() returns nothing to keep, the runner then lives until its
	count is reached or the target is gone.
*/
class BMessageRunner {
public:
								BMessageRunner(BMessenger target, const BMessage& message,
									Clock::Time interval, std::int32_t count = -1,
									BMessenger replyTo = {}, Clock* clock = nullptr);
									/*!< Starts sending copies of \a message; \a count -1 is
										unlimited. InitCheck() tells whether it runs. */
								~BMessageRunner();
									/*!< Stops sending. */

								BMessageRunner(const BMessageRunner&) = delete;
									/*!< Not copyable. */
			BMessageRunner&		operator=(const BMessageRunner&) = delete;
									/*!< Not copyable. */

			std::expected<void, Error> InitCheck() const;
									/*!< InvalidArgument for an interval <= 0, a count of 0 or
										an invalid target. */
			std::expected<void, Error> SetInterval(Clock::Time interval);
									/*!< A new interval, counted from now. */
			std::expected<void, Error> SetCount(std::int32_t count);
									/*!< How many messages are still to be sent; -1 for no
										limit, 0 stops. */
			Clock::Time			Interval() const;
									/*!< The interval. */
			std::int32_t		Count() const;
									/*!< The messages still to be sent; -1 for no limit. */

	static	std::expected<void, Error> StartSending(BMessenger target, const BMessage& message,
									Clock::Time interval, std::int32_t count,
									Clock* clock = nullptr);
									/*!< Sends \a count copies without a runner object;
										InvalidArgument for an unlimited count. */

private:
			struct State;

			std::shared_ptr<State> fState;
};


} // namespace ngui
