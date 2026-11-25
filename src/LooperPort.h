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


#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <thread>

#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>


namespace ngui {


class BLooper;


// Quits a looper without asking QuitRequested() (Haiku's _QUIT_); the
// application sends it to the windows left when it ends.
inline constexpr std::uint32_t kQuitNow = FourCharCode("_QIT");

// Runs a function on the looper's thread (its "work" pointer, a
// std::function<void()>) and replies; for RunOnLooper().
inline constexpr std::uint32_t kRunOnLooper = FourCharCode("_RUN");


// The queue of a looper. Push() from any thread, Pop() only on the looper
// thread. Closed when the loop ends: further messages are refused, queued
// ones are destroyed, which answers waiting senders with kNoReply.
class LooperPort {
public:
	// What a looper is doing, for the test kit's WaitForIdle().
	struct State {
		std::string					name;
		std::size_t					queued = 0;
		bool						dispatching = false;
		std::uint32_t				handling = 0;	// the what in hand while dispatching
		std::vector<std::uint32_t>	waiting;	// the what of each queued message
	};

	// A new port, known to States() while it lives.
	static std::shared_ptr<LooperPort> Create();
	// The open ports, of loopers running or about to.
	static std::vector<State> States();
	// Grows with every message pushed and every one dispatched: unchanged
	// over a look at all States(), nothing happened in between.
	static std::uint64_t Activity();

	void SetName(std::string name);
	std::expected<void, Error> Push(std::unique_ptr<BMessage> message);
	std::unique_ptr<BMessage> Pop();
	// The next message, or nullptr at once if there is none.
	std::unique_ptr<BMessage> TryPop();
	// Called after each Push(), outside the lock, e.g. to wake a platform
	// event loop that waits for its own events (BApplication). Returns only
	// when no call of the previous waker is running any more, so that what
	// it refers to may go away afterwards.
	void SetWaker(std::function<void()> waker);
	void Close();
	bool IsOpen() const;

	void SetThread(std::thread::id thread);
	std::thread::id Thread() const;

	void Finish();
	bool WaitFinished(std::chrono::microseconds timeout);

private:
	// Ends the dispatching of the last message popped; under fLock.
	void _Dispatched();

	static inline std::atomic<std::uint64_t> sActivity{0};

	mutable std::mutex fLock;
	std::condition_variable fAvailable;
	std::condition_variable fFinished;
	std::deque<std::unique_ptr<BMessage>> fQueue;
	std::thread::id fThread;
	std::string fName;
	bool fDispatching = false;
	std::uint32_t fHandling = 0;
	std::function<void()> fWaker;
	std::condition_variable fWakersDone;
	int fWakersRunning = 0;
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


// Runs \a work on the thread of \a looper, with the looper, locked, and
// waits for it, at most
// \a timeout; NotFound if the looper is gone (or goes first), TimedOut if
// it is busy for longer. For the test kit, whose queries read views on
// their window's thread.
std::expected<void, Error> RunOnLooper(const BMessenger& looper,
	std::function<void(BLooper&)> work, std::chrono::microseconds timeout);

// Runs the work of a kRunOnLooper message and replies; false for other
// messages. BLooper calls it before dispatching.
bool RunRequestedWork(BLooper& looper, BMessage& message);


} // namespace ngui
