/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <functional>
#include <optional>


namespace ngui {


class BHandler;
class BLooper;
class BMessage;


/*!	What a filter decides (Haiku's filter_result). */
enum class BFilterResult : std::uint8_t {
	Skip,		/*!< B_SKIP_MESSAGE: the message is not dispatched. */
	Dispatch	/*!< B_DISPATCH_MESSAGE: it goes on, possibly to another target. */
};


/*!	Which deliveries a filter looks at (Haiku's message_delivery). */
enum class BMessageDelivery : std::uint8_t {
	Any,		/*!< B_ANY_DELIVERY */
	Dropped,	/*!< B_DROPPED_DELIVERY: dropped by drag and drop (later). */
	Programmed	/*!< B_PROGRAMMED_DELIVERY: sent or posted. */
};


/*!	Which sources a filter looks at (Haiku's message_source). */
enum class BMessageSource : std::uint8_t {
	Any,	/*!< B_ANY_SOURCE */
	Remote,	/*!< B_REMOTE_SOURCE: another process; there are none yet. */
	Local	/*!< B_LOCAL_SOURCE */
};


/*!	Looks at a message before its handler gets it (module G3), as Haiku's
	BMessageFilter: on a handler for the messages to it, or on a looper for
	all of its messages (common filters). A filter may skip a message or
	change its target to another handler of the same looper.

	Subclasses override Filter(); or a hook function is given, as in Haiku.
	Handlers and loopers own their filters (std::unique_ptr).
*/
class BMessageFilter {
public:
	/*!	The hook form of Filter(). */
	using Hook = std::function<BFilterResult(BMessage* message, BHandler** target,
		BMessageFilter* filter)>;

	explicit					BMessageFilter(std::optional<std::uint32_t> command = {},
									Hook hook = {});
									/*!< Filters all messages, or those with what =
										\a command; any delivery and source. */
								BMessageFilter(BMessageDelivery delivery,
									BMessageSource source,
									std::optional<std::uint32_t> command = {},
									Hook hook = {});
									/*!< Filters only \a delivery from \a source. */
	virtual						~BMessageFilter();
									/*!< Default. */

								BMessageFilter(const BMessageFilter&) = delete;
									/*!< Not copyable. */
			BMessageFilter&		operator=(const BMessageFilter&) = delete;
									/*!< Not copyable. */

	virtual	BFilterResult		Filter(BMessage* message, BHandler** target);
									/*!< Decides about \a message, which goes to \a *target;
										calls the hook, or dispatches if there is none. */

			BMessageDelivery	MessageDelivery() const;
									/*!< The deliveries it looks at. */
			BMessageSource		MessageSource() const;
									/*!< The sources it looks at. */
			std::optional<std::uint32_t> Command() const;
									/*!< The command it looks at, all if none. */
			bool				FiltersAnyCommand() const;
									/*!< No command given. */
			BLooper*			Looper() const;
									/*!< The looper of the handler or looper it is on. */

			bool				Applies(const BMessage& message) const;
									/*!< Whether \a message matches command, delivery and
										source. */

private:
	friend class BHandler;
	friend class BLooper;

			std::optional<std::uint32_t> fCommand;
			BMessageDelivery	fDelivery;
			BMessageSource		fSource;
			Hook				fHook;
			BHandler*			fOwner = nullptr;
};


} // namespace ngui
