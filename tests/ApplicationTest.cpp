/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Application.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kStopPulse = FourCharCode("STOP");
constexpr std::uint32_t kMarker = FourCharCode("MARK");


// An application that records its hooks.
class TestApp : public BApplication {
public:
	TestApp(std::shared_ptr<LooperRecord> record, int argc = 0, char** argv = nullptr)
		:
		BApplication("application/x-vnd.ngui-test", argc, argv),
		fRecord(std::move(record))
	{
	}

	void ArgvReceived(std::span<const std::string> arguments) override
	{
		std::string joined;
		for (const std::string& argument : arguments)
			joined += argument + ",";
		fRecord->Add("argv:" + joined);
	}

	void ReadyToRun() override
	{
		fRecord->Add(std::string("ready:") + (IsLaunching() ? "launching" : "launched"));
	}

	void Pulse() override
	{
		fRecord->Add("pulse");
	}

	void AboutRequested() override
	{
		fRecord->Add("about");
	}

	bool QuitRequested() override
	{
		fRecord->Add("quit?");
		return allowQuit;
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kStopPulse)
			SetPulseRate(Clock::Time::zero());
		fRecord->Add("message:" + std::to_string(message->what));
	}

	std::atomic<bool> allowQuit = true;

private:
	std::shared_ptr<LooperRecord> fRecord;
};


TEST(ApplicationTest, RunsOnTheCallingThreadUntilItQuits)
{
	// What is tested: An application with command line arguments, run on a
	// thread of the test as on a main thread, quit through AppMessenger().
	// Success criteria: ArgvReceived() gets the arguments after the program
	// name, then ReadyToRun() runs while IsLaunching(); all hooks run on the
	// thread that called Run(); a refused quit keeps it running; Run()
	// returns 0 after the accepted one; AppMessenger() is valid only while
	// the application exists.
	// Why it matters: The application loop must run on the main thread
	// (macOS, SDL) and belong to main (the decision for G4); other threads
	// reach it only through its messenger.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	std::string program = "demo";
	std::string first = "--one";
	std::string second = "two";
	std::vector<char*> argv = {program.data(), first.data(), second.data(), nullptr};
	auto app = std::make_unique<TestApp>(record, 3, argv.data());
	app->allowQuit = false;
	app->SetPulseRate(Clock::Time::zero());
	ASSERT_TRUE(app->InitCheck().has_value());
	EXPECT_TRUE(AppMessenger().IsValid());
	int result = -1;
	std::thread::id mainThread;

	// When
	std::thread runner([&] {
		mainThread = std::this_thread::get_id();
		result = app->Run();
	});
	ASSERT_TRUE(record->WaitFor(2));
	BMessage reply;
	ASSERT_TRUE(AppMessenger().SendMessage(BMessage(kQuitRequested), &reply).has_value());
	const bool refused = !reply.GetBool("result", true);
	ASSERT_TRUE(AppMessenger().SendMessage(kAboutRequested).has_value());
	ASSERT_TRUE(record->WaitForEntry("about", 1));
	app->allowQuit = true;
	ASSERT_TRUE(AppMessenger().SendMessage(kQuitRequested).has_value());
	runner.join();
	const BMessenger during = AppMessenger();
	app.reset();

	// Then
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"argv:--one,two,",
		"ready:launching", "quit?", "about", "quit?"}));
	for (const std::thread::id thread : record->threads)
		EXPECT_EQ(thread, mainThread);
	EXPECT_TRUE(refused);
	EXPECT_EQ(result, 0);
	EXPECT_FALSE(during.IsValid());
	EXPECT_FALSE(AppMessenger().IsValid());
}


TEST(ApplicationTest, ThereIsOneApplicationWithAProperSignature)
{
	// What is tested: A second application while one exists, an application
	// with the signature "text/plain", and a new one after the first is
	// gone.
	// Success criteria: The second and the badly signed one fail InitCheck()
	// with InvalidArgument and Run() returns 1 at once; AppMessenger() stays
	// with the first; after it is destroyed a new one can be made.
	// Why it matters: Haiku has one be_app per team; two would fight over
	// AppMessenger() and the main thread.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto first = std::make_unique<TestApp>(record);
	const BMessenger firstMessenger = AppMessenger();

	// When / Then
	{
		TestApp second(record);
		EXPECT_EQ(second.InitCheck().error().code, ErrorCode::InvalidArgument);
		EXPECT_EQ(second.Run(), 1);
		EXPECT_TRUE(AppMessenger() == firstMessenger);
	}
	first.reset();
	{
		BApplication badlySigned("text/plain");
		EXPECT_EQ(badlySigned.InitCheck().error().code, ErrorCode::InvalidArgument);
		EXPECT_EQ(badlySigned.Run(), 1);
	}
	BApplication third("application/x-vnd.ngui-third");
	EXPECT_TRUE(third.InitCheck().has_value());
	EXPECT_EQ(third.Signature(), "application/x-vnd.ngui-third");
	EXPECT_TRUE(third.IsLaunching());
}


TEST(ApplicationTest, PulseFollowsTheClock)
{
	// What is tested: An application with a pulse rate of 100 ms on a
	// ManualClock set as the default clock; the clock advanced by 250 ms,
	// the pulse stopped with SetPulseRate(0), the clock advanced again.
	// Success criteria: No pulse while launching; two pulses after 250 ms;
	// none after the pulse was stopped.
	// Why it matters: Pulse() drives periodic work of an application; with
	// the test clock it is exact and needs no waiting (rule T1).
	//
	// Given
	ManualClock clock;
	Clock::SetDefault(&clock);
	auto record = std::make_shared<LooperRecord>();
	TestApp app(record);
	app.SetPulseRate(Clock::Time(100ms));
	EXPECT_EQ(app.PulseRate(), Clock::Time(100ms));
	std::thread runner([&] { (void)app.Run(); });
	ASSERT_TRUE(record->WaitForEntry("ready:launching", 1));
	const auto marked = [&](std::size_t times) {
		EXPECT_TRUE(AppMessenger().SendMessage(kMarker).has_value());
		EXPECT_TRUE(record->WaitForEntry("message:" + std::to_string(kMarker), times));
		const std::vector<std::string> received = record->Received();
		return std::count(received.begin(), received.end(), "pulse");
	};
	// The pulse runner is made after ReadyToRun(); the marker makes sure
	// that it exists before the clock moves.
	EXPECT_EQ(marked(1), 0);

	// When / Then
	clock.Advance(250ms);
	EXPECT_EQ(marked(2), 2);
	ASSERT_TRUE(AppMessenger().SendMessage(kStopPulse).has_value());
	EXPECT_EQ(marked(3), 2);
	clock.Advance(1000ms);
	EXPECT_EQ(marked(4), 2);
	ASSERT_TRUE(AppMessenger().SendMessage(kQuitRequested).has_value());
	runner.join();
	Clock::SetDefault(nullptr);
}


} // namespace
