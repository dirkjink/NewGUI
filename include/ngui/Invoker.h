/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>

#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


class BHandler;
class BLooper;


/*!	A message and where to send it (module G3), as Haiku's BInvoker; the
	base of controls and menu items, which send it when they are used.

	Differences to Haiku: the message is owned as std::unique_ptr; the
	target is a BMessenger (no Target() returning a handler, CONVENTIONS.md
	3.4); no observers (InvokeNotify() comes with the controls).
*/
class BInvoker {
public:
								BInvoker();
									/*!< No message, no target. */
								BInvoker(std::unique_ptr<BMessage> message,
									BMessenger target);
									/*!< Sends \a message to \a target. */
	virtual						~BInvoker();
									/*!< Frees the message. */

								BInvoker(const BInvoker&) = delete;
									/*!< Not copyable. */
			BInvoker&			operator=(const BInvoker&) = delete;
									/*!< Not copyable. */

			void				SetMessage(std::unique_ptr<BMessage> message);
									/*!< Replaces the message; nullptr for none. */
			BMessage*			Message() const;
									/*!< The message, nullptr if none. */
			std::uint32_t		Command() const;
									/*!< What of the message, 0 if none. */

			void				SetTarget(BMessenger target);
									/*!< Sets the target. */
			void				SetTarget(const BHandler* handler,
									const BLooper* looper = nullptr);
									/*!< Targets \a handler, as BMessenger(handler, looper). */
			const BMessenger&	Messenger() const;
									/*!< The target. */
			bool				IsTargetLocal() const;
									/*!< Always true. */

			void				SetHandlerForReply(BHandler* handler);
									/*!< Where replies go; nullptr for nowhere. */
			const BMessenger&	HandlerForReply() const;
									/*!< Where replies go. */

	virtual	std::expected<void, Error> Invoke(const BMessage* message = nullptr);
									/*!< Sends \a message, or a copy of the own one if it is
										nullptr; InvalidArgument if there is neither. */

private:
			std::unique_ptr<BMessage> fMessage;
			BMessenger			fTarget;
			BMessenger			fReplyTo;
};


} // namespace ngui
