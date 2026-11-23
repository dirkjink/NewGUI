/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Clock.h>

#include <atomic>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui;
using namespace std::chrono_literals;
using Time = Clock::Time;


TEST(ClockTest, ManualClockRunsDueActionsInOrder)
{
	// What is tested: A ManualClock with actions at 30, 10, 20 and 50 ms,
	// one of them cancelled, one scheduling another 5 ms later; Advance()
	// by 25 ms and by 10 ms.
	// Success criteria: Nothing runs before Advance(); the first advance
	// runs 10, 20 and the follow-up at 15 in order of time, each seeing
	// Now() at its own time; the second runs 30; the cancelled action never
	// runs; NextTimer() names the earliest pending action.
	// Why it matters: Rule T1 (specification 20.3): tests drive time
	// themselves, e.g. a blinking cursor or a message runner, without
	// waiting and without races.
	//
	// Given
	ManualClock clock;
	std::vector<std::string> ran;
	const auto log = [&](const std::string& name) {
		return [&ran, &clock, name] {
			ran.push_back(name + "@" + std::to_string(clock.Now().count() / 1000));
		};
	};
	clock.Schedule(Time(30ms), log("c"));
	clock.Schedule(Time(10ms), [&] {
		log("a")();
		clock.Schedule(clock.Now() + Time(5ms), log("follow"));
	});
	clock.Schedule(Time(20ms), log("b"));
	const Clock::TimerId cancelled = clock.Schedule(Time(50ms), log("never"));

	// When / Then
	EXPECT_TRUE(ran.empty());
	EXPECT_EQ(clock.NextTimer(), Time(10ms));
	clock.Advance(25ms);
	EXPECT_EQ(ran, (std::vector<std::string>{"a@10", "follow@15", "b@20"}));
	EXPECT_EQ(clock.Now(), Time(25ms));
	EXPECT_TRUE(clock.Cancel(cancelled));
	EXPECT_FALSE(clock.Cancel(cancelled));
	clock.Advance(100ms);
	EXPECT_EQ(ran.back(), "c@30");
	EXPECT_EQ(ran.size(), 4u);
	EXPECT_EQ(clock.NextTimer(), std::nullopt);
	EXPECT_EQ(clock.Now(), Time(125ms));
}


TEST(ClockTest, SystemClockRunsActionsOnItsThread)
{
	// What is tested: The system clock with an action 20 ms ahead and a
	// cancelled one.
	// Success criteria: The action runs once, not before its time, on a
	// thread other than the caller's; the cancelled one does not run.
	// Why it matters: The real clock drives timers in a running program;
	// the clock's own thread keeps the loopers free.
	//
	// Given
	SystemClock& clock = SystemClock::Instance();
	std::promise<std::pair<Time, std::thread::id>> ran;
	std::atomic<bool> cancelledRan = false;
	const Time start = clock.Now();

	// When
	const Clock::TimerId cancelled = clock.Schedule(start + Time(5ms), [&] {
		cancelledRan = true;
	});
	EXPECT_TRUE(clock.Cancel(cancelled));
	clock.Schedule(start + Time(20ms), [&] {
		ran.set_value({clock.Now(), std::this_thread::get_id()});
	});
	auto future = ran.get_future();
	ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
	const auto [when, thread] = future.get();
	std::this_thread::sleep_for(10ms);

	// Then
	EXPECT_GE(when - start, Time(20ms));
	EXPECT_NE(thread, std::this_thread::get_id());
	EXPECT_FALSE(cancelledRan);
}


TEST(ClockTest, TheDefaultClockCanBeReplaced)
{
	// What is tested: Clock::Default() before, during and after
	// SetDefault() with a ManualClock.
	// Success criteria: The system clock by default, the manual clock while
	// it is set, the system clock again after SetDefault(nullptr).
	// Why it matters: The test environment (M22) swaps in a manual clock
	// for the whole toolkit.
	//
	// Given
	ManualClock manual;

	// When / Then
	EXPECT_EQ(&Clock::Default(), &SystemClock::Instance());
	Clock::SetDefault(&manual);
	EXPECT_EQ(&Clock::Default(), &manual);
	Clock::SetDefault(nullptr);
	EXPECT_EQ(&Clock::Default(), &SystemClock::Instance());
}


} // namespace
