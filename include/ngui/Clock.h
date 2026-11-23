/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>


namespace ngui {


/*!	The time source of the toolkit (module G5, rule T1 of specification
	20.3): everything that depends on time (timers of loopers, cursor
	blinking, tooltips, key repeat, double clicks, animations) asks a Clock
	and never the system clock, so that tests can control time.

	Times are microseconds since the clock's own epoch, as Haiku's
	bigtime_t. Actions run on the clock's own thread (SystemClock) or on
	the thread that advances it (ManualClock), never with a lock of the
	clock held; they must be short and should only send messages.
*/
class Clock {
public:
	/*!	A point in time: microseconds since the clock's epoch. */
	using Time = std::chrono::microseconds;

	/*!	Identifies a scheduled action; never 0. */
	using TimerId = std::uint64_t;

	virtual						~Clock() = default;

	virtual	Time				Now() const = 0;
									/*!< The current time. */
	virtual	TimerId				Schedule(Time when, std::function<void()> action) = 0;
									/*!< Runs \a action once at \a when, or as soon as
										possible if that has passed. */
	virtual	bool				Cancel(TimerId timer) = 0;
									/*!< Cancels \a timer; false if it ran or is unknown. An
										action that is running is not waited for. */
	virtual	std::optional<Time> NextTimer() const = 0;
									/*!< When the earliest scheduled action is due, for the
										idle check of the test driver (20.4, T3). */

	static	Clock&				Default();
									/*!< The clock of the process: the system clock, unless
										SetDefault() replaced it. */
	static	void				SetDefault(Clock* clock);
									/*!< Replaces the default clock, e.g. with a ManualClock
										in tests (M22); nullptr restores the system clock.
										Set it before anything schedules on it. */
};


/*!	The real time (steady, not affected by changes of the system time);
	actions run on a thread of the clock. */
class SystemClock final : public Clock {
public:
								SystemClock();
									/*!< Starts the timer thread. */
								~SystemClock() override;
									/*!< Drops pending actions and joins the thread. */

	static	SystemClock&		Instance();
									/*!< The clock of the process. */

			Time				Now() const override;
									/*!< Microseconds since the steady clock's epoch. */
			TimerId				Schedule(Time when, std::function<void()> action) override;
									/*!< See Clock. */
			bool				Cancel(TimerId timer) override;
									/*!< See Clock. */
			std::optional<Time> NextTimer() const override;
									/*!< See Clock. */

private:
			void				_Run();

	mutable	std::mutex			fLock;
			std::condition_variable fChanged;
			std::multimap<Time, std::pair<TimerId, std::function<void()>>> fTimers;
			TimerId				fNextId = 1;
			bool				fStopping = false;
			std::thread			fThread;
};


/*!	A clock that stands still until the test advances it (specification
	20.3, T1): `clock.Advance(500ms)` runs every action due until then, in
	order of time, on the calling thread. */
class ManualClock final : public Clock {
public:
	explicit					ManualClock(Time start = Time::zero());
									/*!< A clock at \a start. */

			Time				Now() const override;
									/*!< The time the clock was advanced to. */
			TimerId				Schedule(Time when, std::function<void()> action) override;
									/*!< See Clock; nothing runs until Advance(). */
			bool				Cancel(TimerId timer) override;
									/*!< See Clock. */
			std::optional<Time> NextTimer() const override;
									/*!< See Clock. */

			void				Advance(Time duration);
									/*!< Moves time forward by \a duration. Each due action
										runs with Now() at its own time, so that actions it
										schedules in turn are due when they should be. */

private:
	mutable	std::mutex			fLock;
			Time				fNow;
			std::multimap<Time, std::pair<TimerId, std::function<void()>>> fTimers;
			TimerId				fNextId = 1;
};


} // namespace ngui
