/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Clock.h>

#include <algorithm>
#include <atomic>


namespace ngui {


namespace {


std::atomic<Clock*> sDefault = nullptr;


// Removes the timer \a id from \a timers; whether it was there.
template<typename Timers>
bool
Erase(Timers& timers, Clock::TimerId id)
{
	const auto found = std::find_if(timers.begin(), timers.end(),
		[id](const auto& entry) { return entry.second.first == id; });
	if (found == timers.end())
		return false;
	timers.erase(found);
	return true;
}


} // namespace


Clock&
Clock::Default()
{
	Clock* clock = sDefault.load();
	return clock != nullptr ? *clock : SystemClock::Instance();
}


void
Clock::SetDefault(Clock* clock)
{
	sDefault.store(clock);
}


SystemClock::SystemClock()
	:
	fThread([this] { _Run(); })
{
}


SystemClock::~SystemClock()
{
	{
		std::scoped_lock lock(fLock);
		fStopping = true;
		fTimers.clear();
	}
	fChanged.notify_all();
	fThread.join();
}


SystemClock&
SystemClock::Instance()
{
	static SystemClock clock;
	return clock;
}


Clock::Time
SystemClock::Now() const
{
	return std::chrono::duration_cast<Time>(
		std::chrono::steady_clock::now().time_since_epoch());
}


Clock::TimerId
SystemClock::Schedule(Time when, std::function<void()> action)
{
	TimerId id;
	{
		std::scoped_lock lock(fLock);
		id = fNextId++;
		fTimers.emplace(when, std::make_pair(id, std::move(action)));
	}
	fChanged.notify_all();
	return id;
}


bool
SystemClock::Cancel(TimerId timer)
{
	std::scoped_lock lock(fLock);
	return Erase(fTimers, timer);
}


std::optional<Clock::Time>
SystemClock::NextTimer() const
{
	std::scoped_lock lock(fLock);
	if (fTimers.empty())
		return std::nullopt;
	return fTimers.begin()->first;
}


void
SystemClock::_Run()
{
	std::unique_lock lock(fLock);
	while (!fStopping) {
		if (fTimers.empty()) {
			fChanged.wait(lock);
			continue;
		}
		const Time due = fTimers.begin()->first;
		if (due > Now()) {
			fChanged.wait_until(lock, std::chrono::steady_clock::time_point(
				std::chrono::duration_cast<std::chrono::steady_clock::duration>(due)));
			continue;
		}
		std::function<void()> action = std::move(fTimers.begin()->second.second);
		fTimers.erase(fTimers.begin());
		lock.unlock();
		action();
		lock.lock();
	}
}


ManualClock::ManualClock(Time start)
	:
	fNow(start)
{
}


Clock::Time
ManualClock::Now() const
{
	std::scoped_lock lock(fLock);
	return fNow;
}


Clock::TimerId
ManualClock::Schedule(Time when, std::function<void()> action)
{
	std::scoped_lock lock(fLock);
	const TimerId id = fNextId++;
	fTimers.emplace(when, std::make_pair(id, std::move(action)));
	return id;
}


bool
ManualClock::Cancel(TimerId timer)
{
	std::scoped_lock lock(fLock);
	return Erase(fTimers, timer);
}


std::optional<Clock::Time>
ManualClock::NextTimer() const
{
	std::scoped_lock lock(fLock);
	if (fTimers.empty())
		return std::nullopt;
	return fTimers.begin()->first;
}


void
ManualClock::Advance(Time duration)
{
	std::unique_lock lock(fLock);
	const Time end = fNow + std::max(duration, Time::zero());
	while (!fTimers.empty() && fTimers.begin()->first <= end) {
		fNow = std::max(fNow, fTimers.begin()->first);
		std::function<void()> action = std::move(fTimers.begin()->second.second);
		fTimers.erase(fTimers.begin());
		lock.unlock();
		action();
		lock.lock();
	}
	fNow = end;
}


} // namespace ngui
