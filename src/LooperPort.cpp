/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "LooperPort.h"

#include <algorithm>
#include <utility>

#include <ngui/AppDefs.h>


namespace ngui {


namespace {


// A wait of \a timeout from now; beyond the clock's range it is infinite.
template<typename Lock, typename Predicate>
bool
WaitFor(std::condition_variable& condition, Lock& lock, std::chrono::microseconds timeout,
	Predicate predicate)
{
	const auto now = std::chrono::steady_clock::now();
	const auto longest = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::time_point::max() - now);
	const std::chrono::microseconds wait = std::max(timeout, std::chrono::microseconds::zero());
	if (wait >= longest) {
		condition.wait(lock, predicate);
		return true;
	}
	return condition.wait_until(lock, now + wait, predicate);
}


} // namespace


std::expected<void, Error>
LooperPort::Push(std::unique_ptr<BMessage> message)
{
	{
		std::scoped_lock lock(fLock);
		if (!fClosed) {
			fQueue.push_back(std::move(message));
			fAvailable.notify_one();
			return {};
		}
	}
	// Refused: the message dies here, outside the lock, which may wake its
	// sender with kNoReply.
	return Failure(ErrorCode::NotFound, "the looper has quit");
}


std::unique_ptr<BMessage>
LooperPort::Pop()
{
	std::unique_lock lock(fLock);
	fAvailable.wait(lock, [this] { return !fQueue.empty() || fClosed; });
	if (fQueue.empty())
		return nullptr;
	std::unique_ptr<BMessage> message = std::move(fQueue.front());
	fQueue.pop_front();
	return message;
}


void
LooperPort::Close()
{
	std::deque<std::unique_ptr<BMessage>> dropped;
	{
		std::scoped_lock lock(fLock);
		fClosed = true;
		dropped.swap(fQueue);
	}
	fAvailable.notify_all();
	// The dropped messages are destroyed here, outside the lock.
}


bool
LooperPort::IsOpen() const
{
	std::scoped_lock lock(fLock);
	return !fClosed;
}


void
LooperPort::SetThread(std::thread::id thread)
{
	std::scoped_lock lock(fLock);
	fThread = thread;
}


std::thread::id
LooperPort::Thread() const
{
	std::scoped_lock lock(fLock);
	return fThread;
}


void
LooperPort::Finish()
{
	{
		std::scoped_lock lock(fLock);
		fDone = true;
	}
	fFinished.notify_all();
}


bool
LooperPort::WaitFinished(std::chrono::microseconds timeout)
{
	std::unique_lock lock(fLock);
	return WaitFor(fFinished, lock, timeout, [this] { return fDone; });
}


void
ReplyWaiter::Deliver(BMessage reply)
{
	{
		std::scoped_lock lock(fLock);
		if (fReply.has_value())
			return;
		fReply = std::move(reply);
	}
	fArrived.notify_all();
}


std::optional<BMessage>
ReplyWaiter::Wait(std::chrono::microseconds timeout)
{
	std::unique_lock lock(fLock);
	if (!WaitFor(fArrived, lock, timeout, [this] { return fReply.has_value(); }))
		return std::nullopt;
	return std::move(fReply);
}


ReplyToken::ReplyToken(std::weak_ptr<ReplyWaiter> waiterOfSender, BMessenger replyAddress)
	:
	waiter(std::move(waiterOfSender)),
	replyTo(std::move(replyAddress))
{
}


ReplyToken::~ReplyToken()
{
	// B_NO_REPLY: a waiting sender must not wait for ever.
	if (fAnswered)
		return;
	if (const std::shared_ptr<ReplyWaiter> sender = waiter.lock())
		sender->Deliver(BMessage(kNoReply));
}


bool
ReplyToken::IsWaiting() const
{
	return !fAnswered && !waiter.expired();
}


bool
ReplyToken::Answer()
{
	if (fAnswered)
		return false;
	fAnswered = true;
	return true;
}


} // namespace ngui
