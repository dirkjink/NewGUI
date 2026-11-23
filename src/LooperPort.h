/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The shared parts of a looper (module G3): its message queue, which
// messengers reach through a weak reference and which outlives the looper
// object until the last messenger lets go, and what a delivered message
// carries: its target handler and whom to reply to.


#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


// The queue of a looper. Push() from any thread, Pop() only on the looper
// thread. Closed when the loop ends: further messages are refused, queued
// ones are destroyed, which answers waiting senders with kNoReply.
class LooperPort {
public:
	std::expected<void, Error> Push(std::unique_ptr<BMessage> message);
	std::unique_ptr<BMessage> Pop();
	void Close();
	bool IsOpen() const;

	void SetThread(std::thread::id thread);
	std::thread::id Thread() const;

	void Finish();
	bool WaitFinished(std::chrono::microseconds timeout);

private:
	mutable std::mutex fLock;
	std::condition_variable fAvailable;
	std::condition_variable fFinished;
	std::deque<std::unique_ptr<BMessage>> fQueue;
	std::thread::id fThread;
	bool fClosed = false;
	bool fDone = false;
};


// Where a synchronous sender waits for its reply.
class ReplyWaiter {
public:
	void Deliver(BMessage reply);
	std::optional<BMessage> Wait(std::chrono::microseconds timeout);

private:
	std::mutex fLock;
	std::condition_variable fArrived;
	std::optional<BMessage> fReply;
};


// The obligation to reply, shared by nobody but the delivered message (a
// copy of a message does not carry it). If it ends unanswered, a waiting
// sender gets kNoReply.
class ReplyToken {
public:
	ReplyToken(std::weak_ptr<ReplyWaiter> waiter, BMessenger replyTo);
	~ReplyToken();

	ReplyToken(const ReplyToken&) = delete;
	ReplyToken& operator=(const ReplyToken&) = delete;

	bool IsWaiting() const;
	// Marks the token answered; false if it was already.
	bool Answer();

	std::weak_ptr<ReplyWaiter> waiter;
	BMessenger replyTo;

private:
	bool fAnswered = false;
};


// What a message carries on its way: BMessage::fDelivery.
struct MessageDelivery {
	std::int32_t handlerToken = 0;		// 0: the preferred handler
	std::unique_ptr<ReplyToken> reply;
	bool isReply = false;
	bool delivered = false;
};


} // namespace ngui
