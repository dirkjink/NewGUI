/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/Platform.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>

#include "support/FakePlatform.h"
#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// An application that records what reaches it.
class RecordingApp : public BApplication {
public:
	explicit RecordingApp(std::shared_ptr<LooperRecord> record)
		:
		BApplication("application/x-vnd.ngui-platform-test"),
		fRecord(std::move(record))
	{
		SetPulseRate(Clock::Time::zero());
	}

	void ReadyToRun() override
	{
		fRecord->Add("ready");
	}

	void MessageReceived(BMessage* message) override
	{
		fRecord->Add("message:" + std::to_string(message->what));
	}

private:
	std::shared_ptr<LooperRecord> fRecord;
};


TEST(PlatformTest, TheMainLoopServesPlatformAndMessages)
{
	// What is tested: An application with a platform whose events are
	// injected from another thread, interleaved with messages sent through
	// AppMessenger() while the loop waits for platform events.
	// Success criteria: Events and messages are all handled on the main
	// thread; a message wakes the loop out of WaitForEvents(); the
	// platform's events reach their target as messages; platform::Current()
	// is the platform while the application runs and nullptr afterwards.
	// Why it matters: On macOS and with SDL the window system's events must
	// be fetched on the main thread, which is also the application's
	// thread; neither may starve the other (module P1).
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<FakePlatform>();
	FakePlatform* platform = owned.get();
	RecordingApp app(record);
	ASSERT_TRUE(app.UsePlatform(std::move(owned)).has_value());
	EXPECT_EQ(app.Platform(), platform);
	platform::Platform* during = nullptr;
	std::thread::id mainThread;

	// When
	std::thread runner([&] {
		mainThread = std::this_thread::get_id();
		(void)app.Run();
	});
	ASSERT_TRUE(record->WaitForEntry("ready", 1));
	platform->Inject([&] {
		during = platform::Current();
		(void)AppMessenger().SendMessage(1);
	});
	ASSERT_TRUE(record->WaitForEntry("message:1", 1));
	// Once the loop has gone back into WaitForEvents(), a message must
	// wake it; without the waker this would hang until the timeout.
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (!platform->IsBlocked() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	const bool idle = platform->IsBlocked();
	ASSERT_TRUE(AppMessenger().SendMessage(2).has_value());
	ASSERT_TRUE(record->WaitForEntry("message:2", 1));
	platform->Inject([] { (void)AppMessenger().SendMessage(kQuitRequested); });
	runner.join();

	// Then
	EXPECT_EQ(during, platform);
	EXPECT_EQ(platform::Current(), nullptr);
	EXPECT_TRUE(idle);
	for (const std::thread::id thread : record->threads)
		EXPECT_EQ(thread, mainThread);
	for (const std::thread::id thread : platform->ProcessThreads())
		EXPECT_EQ(thread, mainThread);
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"ready", "message:1",
		"message:2"}));
}


TEST(PlatformTest, UsePlatformIsForBeforeRun)
{
	// What is tested: UsePlatform() with nullptr, with a platform before
	// Run(), and again after the application has run.
	// Success criteria: nullptr gives InvalidArgument; before Run() the
	// platform is taken; once the application has run it is refused with
	// InvalidArgument; an application without a platform has none.
	// Why it matters: The loop decides at start whether it waits for a
	// platform; changing it later would leave events unserved.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	RecordingApp app(record);

	// When / Then
	EXPECT_EQ(app.Platform(), nullptr);
	EXPECT_EQ(app.UsePlatform(nullptr).error().code, ErrorCode::InvalidArgument);
	ASSERT_TRUE(app.UsePlatform(std::make_unique<FakePlatform>()).has_value());
	EXPECT_EQ(app.Platform()->Name(), "fake");
	std::thread runner([&] { (void)app.Run(); });
	ASSERT_TRUE(record->WaitForEntry("ready", 1));
	ASSERT_TRUE(AppMessenger().SendMessage(kQuitRequested).has_value());
	runner.join();
	EXPECT_EQ(app.UsePlatform(std::make_unique<FakePlatform>()).error().code,
		ErrorCode::InvalidArgument);
}


} // namespace
