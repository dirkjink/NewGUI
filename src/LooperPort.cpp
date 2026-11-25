/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "LooperPort.h"

#include <algorithm>
#include <unordered_map>
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


// The ports of the process, for States(); expired ones are dropped as
// they are found.
std::mutex sPortsLock;
std::vector<std::weak_ptr<LooperPort>>& Ports()
{
	static std::vector<std::weak_ptr<LooperPort>> ports;
	return ports;
}


} // namespace


std::shared_ptr<LooperPort>
LooperPort::Create()
{
	auto port = std::make_shared<LooperPort>();
	std::scoped_lock lock(sPortsLock);
	std::erase_if(Ports(), [](const std::weak_ptr<LooperPort>& old) { return old.expired(); });
	Ports().push_back(port);
	return port;
}


std::vector<LooperPort::State>
LooperPort::States()
{
	std::vector<std::shared_ptr<LooperPort>> ports;
	{
		std::scoped_lock lock(sPortsLock);
		for (const std::weak_ptr<LooperPort>& weak : Ports()) {
			if (auto port = weak.lock())
				ports.push_back(std::move(port));
		}
	}
	std::vector<State> states;
	for (const std::shared_ptr<LooperPort>& port : ports) {
		std::scoped_lock lock(port->fLock);
		// a closed looper takes no part; one that has not started yet does,
		// with the messages it will handle
		if (port->fClosed)
			continue;
		State state;
		state.name = port->fName;
		state.queued = port->fQueue.size();
		state.dispatching = port->fDispatching;
		state.handling = port->fHandling;
		for (const std::unique_ptr<BMessage>& message : port->fQueue)
			state.waiting.push_back(message->what);
		states.push_back(std::move(state));
	}
	return states;
}


std::uint64_t
LooperPort::Activity()
{
	return sActivity.load();
}


void
LooperPort::SetName(std::string name)
{
	std::scoped_lock lock(fLock);
	fName = std::move(name);
}


void
LooperPort::_Dispatched()
{
	if (fDispatching) {
		fDispatching = false;
		sActivity++;
	}
}


namespace {

// The work of RunOnLooper() by number. A request that times out takes its
// work back if it has not started; if it has, it waits for the end, since
// the work refers to the caller's variables.
struct RequestedWork {
	std::function<void(BLooper&)>	work;
	bool							running = false;
	bool							done = false;
};

std::mutex sWorkLock;
std::condition_variable sWorkChanged;
std::uint64_t sNextWork = 1;

std::unordered_map<std::uint64_t, RequestedWork>&
Work()
{
	static std::unordered_map<std::uint64_t, RequestedWork> work;
	return work;
}

} // namespace


std::expected<void, Error>
RunOnLooper(const BMessenger& looper, std::function<void(BLooper&)> work,
	std::chrono::microseconds timeout)
{
	std::uint64_t number = 0;
	{
		std::scoped_lock lock(sWorkLock);
		number = sNextWork++;
		Work().emplace(number, RequestedWork{std::move(work)});
	}
	BMessage request(kRunOnLooper);
	(void)request.AddInt64("work", static_cast<std::int64_t>(number));
	BMessage reply;
	const auto sent = looper.SendMessage(request, &reply, timeout);
	{
		std::unique_lock lock(sWorkLock);
		sWorkChanged.wait(lock, [&] {
			const auto found = Work().find(number);
			return found == Work().end() || !found->second.running;
		});
		Work().erase(number);
	}
	if (!sent.has_value())
		return std::unexpected(sent.error());
	if (reply.what != kRunOnLooper)
		return Failure(ErrorCode::NotFound, "the looper is gone");
	return {};
}


bool
RunRequestedWork(BLooper& looper, BMessage& message)
{
	if (message.what != kRunOnLooper)
		return false;
	const auto number = static_cast<std::uint64_t>(message.GetInt64("work", 0));
	std::function<void(BLooper&)>* work = nullptr;
	{
		std::scoped_lock lock(sWorkLock);
		const auto found = Work().find(number);
		if (found != Work().end() && !found->second.done) {
			found->second.running = true;
			work = &found->second.work;
		}
	}
	// the entry stays while it runs: its sender waits for that
	if (work != nullptr && *work)
		(*work)(looper);
	{
		std::scoped_lock lock(sWorkLock);
		if (const auto found = Work().find(number); found != Work().end()) {
			found->second.running = false;
			found->second.done = true;
		}
	}
	sWorkChanged.notify_all();
	(void)message.SendReply(BMessage(kRunOnLooper));
	return true;
}


std::expected<void, Error>
LooperPort::Push(std::unique_ptr<BMessage> message)
{
	bool queued = false;
	std::function<void()> waker;
	{
		std::scoped_lock lock(fLock);
		if (!fClosed) {
			sActivity++;
			fQueue.push_back(std::move(message));
			fAvailable.notify_one();
			waker = fWaker;
			if (waker)
				fWakersRunning++;
			queued = true;
		}
	}
	if (!queued) {
		// Refused: the message dies here, outside the lock, which may wake
		// its sender with kNoReply.
		return Failure(ErrorCode::NotFound, "the looper has quit");
	}
	if (waker) {
		waker();
		{
			std::scoped_lock lock(fLock);
			fWakersRunning--;
		}
		fWakersDone.notify_all();
	}
	return {};
}


std::unique_ptr<BMessage>
LooperPort::Pop()
{
	std::unique_lock lock(fLock);
	_Dispatched();
	fAvailable.wait(lock, [this] { return !fQueue.empty() || fClosed; });
	if (fQueue.empty())
		return nullptr;
	std::unique_ptr<BMessage> message = std::move(fQueue.front());
	fQueue.pop_front();
	fDispatching = true;
	fHandling = message->what;
	return message;
}


std::unique_ptr<BMessage>
LooperPort::TryPop()
{
	std::scoped_lock lock(fLock);
	_Dispatched();
	if (fQueue.empty())
		return nullptr;
	std::unique_ptr<BMessage> message = std::move(fQueue.front());
	fQueue.pop_front();
	fDispatching = true;
	fHandling = message->what;
	return message;
}


void
LooperPort::SetWaker(std::function<void()> waker)
{
	std::unique_lock lock(fLock);
	fWaker = std::move(waker);
	// A Push() may still be calling the old waker outside the lock.
	fWakersDone.wait(lock, [this] { return fWakersRunning == 0; });
}


void
LooperPort::Close()
{
	std::deque<std::unique_ptr<BMessage>> dropped;
	{
		std::scoped_lock lock(fLock);
		_Dispatched();
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
