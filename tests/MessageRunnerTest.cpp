/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/MessageRunner.h>

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;
using Time = Clock::Time;


constexpr std::uint32_t kTick = 7;
constexpr std::uint32_t kMarker = 99;


// A looper recording what it receives, with helpers to count the ticks
// once everything sent so far has arrived.
class Target {
public:
	Target()
		:
		fRecord(std::make_shared<LooperRecord>()),
		fMessenger(*BLooper::Run(std::make_unique<RecordingLooper>(fRecord)))
	{
	}

	~Target()
	{
		(void)fMessenger.SendMessage(kQuitRequested);
		(void)fMessenger.WaitForLooperQuit(5s);
	}

	const BMessenger& Messenger() const
	{
		return fMessenger;
	}

	// The ticks received; the marker sent behind them makes sure that
	// every tick sent before has arrived (the queue keeps its order).
	int Ticks()
	{
		fMarkers++;
		EXPECT_TRUE(fMessenger.SendMessage(kMarker).has_value());
		EXPECT_TRUE(fRecord->WaitForEntry("looper:" + std::to_string(kMarker), fMarkers));
		int ticks = 0;
		for (const std::string& entry : fRecord->Received())
			ticks += entry == "looper:" + std::to_string(kTick) ? 1 : 0;
		return ticks;
	}

private:
	std::shared_ptr<LooperRecord> fRecord;
	BMessenger fMessenger;
	std::size_t fMarkers = 0;
};


TEST(MessageRunnerTest, SendsEveryIntervalUntilItsCount)
{
	// What is tested: A runner with an interval of 100 ms and a count of 3
	// on a ManualClock, advanced in steps.
	// Success criteria: No message before the first interval has passed;
	// one per interval after it; none after the third; Count() goes down
	// to 0 and no timer is left.
	// Why it matters: BMessageRunner drives periodic work (cursor blinking,
	// pulse, repeat); with the manual clock its timing is exact in tests.
	//
	// Given
	ManualClock clock;
	Target target;
	BMessageRunner runner(target.Messenger(), BMessage(kTick), Time(100ms), 3, {}, &clock);
	ASSERT_TRUE(runner.InitCheck().has_value());

	// When / Then
	clock.Advance(99ms);
	EXPECT_EQ(target.Ticks(), 0);
	clock.Advance(1ms);
	EXPECT_EQ(target.Ticks(), 1);
	EXPECT_EQ(runner.Count(), 2);
	clock.Advance(250ms);
	EXPECT_EQ(target.Ticks(), 3);
	EXPECT_EQ(runner.Count(), 0);
	clock.Advance(1000ms);
	EXPECT_EQ(target.Ticks(), 3);
	EXPECT_EQ(clock.NextTimer(), std::nullopt);
}


TEST(MessageRunnerTest, DestructionAndChangesTakeEffect)
{
	// What is tested: An unlimited runner: SetInterval() to 50 ms,
	// SetCount(1), SetCount(-1) again, then its destruction; and a runner
	// whose target quits.
	// Success criteria: The new interval counts from the change; a count of
	// 1 sends one more message and stops; destroying the runner stops it at
	// once; a runner stops when its target is gone.
	// Why it matters: Controls change their repeat rate and stop runners
	// when they are done; a runner must never send after its owner let go.
	//
	// Given
	ManualClock clock;
	Target target;
	auto runner = std::make_unique<BMessageRunner>(target.Messenger(), BMessage(kTick),
		Time(100ms), -1, BMessenger(), &clock);

	// When / Then
	clock.Advance(250ms);
	EXPECT_EQ(target.Ticks(), 2);
	ASSERT_TRUE(runner->SetInterval(Time(50ms)).has_value());
	EXPECT_EQ(runner->Interval(), Time(50ms));
	clock.Advance(100ms);
	EXPECT_EQ(target.Ticks(), 4);
	ASSERT_TRUE(runner->SetCount(1).has_value());
	clock.Advance(200ms);
	EXPECT_EQ(target.Ticks(), 5);
	ASSERT_TRUE(runner->SetCount(-1).has_value());
	clock.Advance(50ms);
	EXPECT_EQ(target.Ticks(), 6);
	runner.reset();
	clock.Advance(500ms);
	EXPECT_EQ(target.Ticks(), 6);

	auto gone = std::make_unique<Target>();
	BMessageRunner orphan(gone->Messenger(), BMessage(kTick), Time(10ms), -1, {}, &clock);
	gone.reset();
	clock.Advance(10ms);
	EXPECT_EQ(orphan.Count(), 0);
	EXPECT_EQ(clock.NextTimer(), std::nullopt);
}


TEST(MessageRunnerTest, InvalidRunnersAndStartSending)
{
	// What is tested: Runners with interval 0, count 0 and an invalid
	// target; StartSending() with a count of 2 and with an unlimited count.
	// Success criteria: InitCheck() gives InvalidArgument for the invalid
	// ones and they schedule nothing; StartSending() sends exactly two
	// messages without a runner object, and refuses an unlimited count.
	// Why it matters: Haiku's InitCheck() pattern and its fire-and-forget
	// form; a detached runner must neither stop early nor leak.
	//
	// Given
	ManualClock clock;
	Target target;

	// When / Then
	EXPECT_EQ(BMessageRunner(target.Messenger(), BMessage(kTick), Time(0), -1, {}, &clock)
		.InitCheck().error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(BMessageRunner(target.Messenger(), BMessage(kTick), Time(10ms), 0, {}, &clock)
		.InitCheck().error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(BMessageRunner(BMessenger(), BMessage(kTick), Time(10ms), -1, {}, &clock)
		.InitCheck().error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(clock.NextTimer(), std::nullopt);

	ASSERT_TRUE(BMessageRunner::StartSending(target.Messenger(), BMessage(kTick), Time(10ms), 2,
		&clock).has_value());
	EXPECT_EQ(BMessageRunner::StartSending(target.Messenger(), BMessage(kTick), Time(10ms), -1,
		&clock).error().code, ErrorCode::InvalidArgument);
	clock.Advance(100ms);
	EXPECT_EQ(target.Ticks(), 2);
	EXPECT_EQ(clock.NextTimer(), std::nullopt);
}


} // namespace
