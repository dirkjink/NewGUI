/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Messenger.h>

#include <optional>
#include <thread>
#include <utility>

#include <ngui/Looper.h>
#include <ngui/Message.h>

#include "LooperPort.h"


namespace ngui {


BMessenger::BMessenger(const BHandler* handler, const BLooper* looper)
{
	if (handler != nullptr) {
		if (handler->fLooper == nullptr)
			return;
		fPort = handler->fLooper->fPort;
		// The looper itself as target means its preferred handler only if
		// asked for with nullptr; as a handler it has a token of its own.
		fToken = handler->fToken;
	} else if (looper != nullptr) {
		fPort = looper->fPort;
		fToken = 0;
	}
}


bool
BMessenger::IsValid() const
{
	const std::shared_ptr<LooperPort> port = fPort.lock();
	return port != nullptr && port->IsOpen();
}


bool
BMessenger::IsTargetLocal() const
{
	return true;
}


std::expected<void, Error>
BMessenger::_Send(std::unique_ptr<BMessage> message) const
{
	const std::shared_ptr<LooperPort> port = fPort.lock();
	if (port == nullptr)
		return Failure(ErrorCode::NotFound, "the looper has quit");
	if (message->fDelivery == nullptr)
		message->fDelivery = std::make_unique<MessageDelivery>();
	message->fDelivery->handlerToken = fToken;
	return port->Push(std::move(message));
}


std::expected<void, Error>
BMessenger::ForwardMessage(std::unique_ptr<BMessage> message) const
{
	if (message == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to forward");
	// its reply token stays; the new looper delivers it anew
	if (message->fDelivery != nullptr)
		message->fDelivery->delivered = false;
	return _Send(std::move(message));
}


std::expected<void, Error>
BMessenger::SendMessage(std::uint32_t command, BHandler* replyTo) const
{
	return SendMessage(BMessage(command), replyTo != nullptr ? BMessenger(replyTo) : BMessenger());
}


std::expected<void, Error>
BMessenger::SendMessage(const BMessage& message) const
{
	return SendMessage(message, BMessenger());
}


std::expected<void, Error>
BMessenger::SendMessage(const BMessage& message, const BMessenger& replyTo) const
{
	auto copy = std::make_unique<BMessage>(message);
	copy->fDelivery = std::make_unique<MessageDelivery>();
	if (replyTo.fPort.lock() != nullptr)
		copy->fDelivery->reply = std::make_unique<ReplyToken>(std::weak_ptr<ReplyWaiter>(),
			replyTo);
	return _Send(std::move(copy));
}


std::expected<void, Error>
BMessenger::SendMessage(const BMessage& message, BMessage* reply,
	std::chrono::microseconds replyTimeout) const
{
	if (reply == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no reply message");
	const std::shared_ptr<LooperPort> port = fPort.lock();
	if (port == nullptr)
		return Failure(ErrorCode::NotFound, "the looper has quit");
	if (port->Thread() == std::this_thread::get_id())
		return Failure(ErrorCode::InvalidArgument, "waiting for its own looper would deadlock");

	const auto waiter = std::make_shared<ReplyWaiter>();
	auto copy = std::make_unique<BMessage>(message);
	copy->fDelivery = std::make_unique<MessageDelivery>();
	copy->fDelivery->reply = std::make_unique<ReplyToken>(waiter, BMessenger());
	const std::expected<void, Error> sent = _Send(std::move(copy));
	if (!sent.has_value())
		return sent;

	std::optional<BMessage> answer = waiter->Wait(replyTimeout);
	if (!answer.has_value())
		return Failure(ErrorCode::TimedOut, "no reply in time");
	*reply = std::move(*answer);
	return {};
}


std::expected<void, Error>
BMessenger::WaitForLooperQuit(std::chrono::microseconds timeout) const
{
	const std::shared_ptr<LooperPort> port = fPort.lock();
	if (port == nullptr)
		return {};
	if (port->Thread() == std::this_thread::get_id())
		return Failure(ErrorCode::InvalidArgument, "a looper cannot wait for itself");
	if (!port->WaitFinished(timeout))
		return Failure(ErrorCode::TimedOut, "the looper still runs");
	return {};
}


bool
BMessenger::operator==(const BMessenger& other) const
{
	return !fPort.owner_before(other.fPort) && !other.fPort.owner_before(fPort)
		&& fToken == other.fToken;
}


} // namespace ngui
