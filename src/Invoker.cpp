/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Invoker.h>

#include <utility>

#include <ngui/Handler.h>


namespace ngui {


BInvoker::BInvoker() = default;


BInvoker::BInvoker(std::unique_ptr<BMessage> message, BMessenger target)
	:
	fMessage(std::move(message)),
	fTarget(std::move(target))
{
}


BInvoker::~BInvoker() = default;


void
BInvoker::SetMessage(std::unique_ptr<BMessage> message)
{
	fMessage = std::move(message);
}


BMessage*
BInvoker::Message() const
{
	return fMessage.get();
}


std::uint32_t
BInvoker::Command() const
{
	return fMessage != nullptr ? fMessage->what : 0;
}


void
BInvoker::SetTarget(BMessenger target)
{
	fTarget = std::move(target);
}


void
BInvoker::SetTarget(const BHandler* handler, const BLooper* looper)
{
	fTarget = BMessenger(handler, looper);
}


const BMessenger&
BInvoker::Messenger() const
{
	return fTarget;
}


bool
BInvoker::IsTargetLocal() const
{
	return true;
}


void
BInvoker::SetHandlerForReply(BHandler* handler)
{
	fReplyTo = handler != nullptr ? BMessenger(handler) : BMessenger();
}


const BMessenger&
BInvoker::HandlerForReply() const
{
	return fReplyTo;
}


std::expected<void, Error>
BInvoker::Invoke(const BMessage* message)
{
	const BMessage* sent = message != nullptr ? message : fMessage.get();
	if (sent == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to send");
	return fTarget.SendMessage(*sent, fReplyTo);
}


} // namespace ngui
