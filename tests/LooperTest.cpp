/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Looper.h>

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


// Quits the looper behind \a messenger and waits until it is destroyed.
void
QuitAndWait(const BMessenger& messenger)
{
	ASSERT_TRUE(messenger.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());
}


TEST(LooperTest, MessagesRunInOrderOnTheLoopersThread)
{
	// What is tested: 100 messages sent from the test thread to a running
	// looper, then kQuitRequested.
	// Success criteria: They arrive in order, all on one thread that is not
	// the sender's; after the quit the looper object is destroyed on that
	// same thread, the messenger is invalid and sending fails with NotFound.
	// Why it matters: A looper is a thread with a queue (G3); it owns
	// itself and dies on its own thread, so nobody else can hold a dangling
	// pointer to it (CONVENTIONS.md 3.3, the model chosen for G3).
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	const BMessenger looper = *BLooper::Run(std::make_unique<RecordingLooper>(record));

	// When
	for (std::uint32_t i = 0; i < 100; i++)
		ASSERT_TRUE(looper.SendMessage(i).has_value());
	ASSERT_TRUE(record->WaitFor(100));
	QuitAndWait(looper);

	// Then
	const std::vector<std::string> received = record->Received();
	for (std::uint32_t i = 0; i < 100; i++)
		EXPECT_EQ(received[i], "looper:" + std::to_string(i));
	EXPECT_NE(record->threads.front(), std::this_thread::get_id());
	for (const std::thread::id thread : record->threads)
		EXPECT_EQ(thread, record->threads.front());
	EXPECT_TRUE(record->destroyed);
	EXPECT_EQ(record->destroyedOn, record->threads.front());
	EXPECT_FALSE(looper.IsValid());
	EXPECT_EQ(looper.SendMessage(1).error().code, ErrorCode::NotFound);
}


TEST(LooperTest, HandlersReceiveTheirMessagesAndPassOnTheRest)
{
	// What is tested: A looper with two handlers: messengers to each, to the
	// preferred handler, a handler that passes a message on, PostMessage()
	// from within, and a message to a handler removed meanwhile.
	// Success criteria: Each message reaches its target; the preferred
	// handler gets what is sent to the looper's messenger; a passed-on
	// message reaches the looper as next handler; the message to the
	// removed handler is dropped; the handler list counts the looper.
	// Why it matters: The chain of handlers is how views and windows share
	// one thread (G8, G9): a view handles what it knows and passes on the
	// rest.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingLooper* looper = owned.get();
	RecordingHandler first("first", record);
	RecordingHandler second("second", record);
	second.passOn = true;
	looper->AddHandler(&first);
	looper->AddHandler(&second);
	looper->SetPreferredHandler(&first);
	EXPECT_EQ(looper->CountHandlers(), 3);
	EXPECT_EQ(looper->IndexOf(&second), 2);
	EXPECT_EQ(looper->HandlerAt(0), looper);
	EXPECT_EQ(second.NextHandler(), looper);
	const BMessenger toFirst(&first);
	const BMessenger toSecond(&second);
	const BMessenger toPreferred(nullptr, looper);
	first.action = [&](BMessage& message) {
		if (message.what == 1)
			(void)looper->PostMessage(BMessage(2), &second);
		if (message.what == 3)
			looper->RemoveHandler(&second);
	};

	// When
	const BMessenger toLooper = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(toPreferred.SendMessage(1).has_value());
	ASSERT_TRUE(record->WaitFor(3));
	ASSERT_TRUE(toLooper.SendMessage(5).has_value());
	ASSERT_TRUE(record->WaitFor(4));
	ASSERT_TRUE(toFirst.SendMessage(3).has_value());
	ASSERT_TRUE(toSecond.SendMessage(4).has_value());
	ASSERT_TRUE(toFirst.SendMessage(6).has_value());
	ASSERT_TRUE(record->WaitFor(6));
	QuitAndWait(toLooper);

	// Then
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"first:1", "second:2", "looper:2",
		"looper:5", "first:3", "first:6"}));
}


TEST(LooperTest, QuitRequestedMayRefuse)
{
	// What is tested: kQuitRequested sent synchronously to a looper whose
	// QuitRequested() says no the first time and yes the second.
	// Success criteria: The first reply is kReply with "result" false and
	// the looper keeps running; the second reply says true and the looper
	// ends.
	// Why it matters: A window asks the user before it closes; the sender
	// (e.g. BApplication when quitting, G4) learns the answer.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	int asked = 0;
	owned->quitRequested = [&asked] { return ++asked > 1; };
	const BMessenger looper = *BLooper::Run(std::move(owned));
	BMessage first;
	BMessage second;

	// When
	ASSERT_TRUE(looper.SendMessage(BMessage(kQuitRequested), &first).has_value());
	const bool stillRunning = looper.IsValid();
	ASSERT_TRUE(looper.SendMessage(BMessage(kQuitRequested), &second).has_value());
	ASSERT_TRUE(looper.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(first.what, kReply);
	EXPECT_FALSE(first.GetBool("result", true));
	EXPECT_TRUE(first.IsReply());
	EXPECT_TRUE(stillRunning);
	EXPECT_TRUE(second.GetBool("result", false));
	EXPECT_FALSE(looper.IsValid());
}


TEST(LooperTest, ManyThreadsCanSendAtOnce)
{
	// What is tested: Four threads send 1,000 messages each to one looper.
	// Success criteria: All 4,000 arrive, those of each sender in its order.
	// Why it matters: The queue is the only meeting point of threads; it
	// must stay consistent under load (run under ThreadSanitizer as well).
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	const BMessenger looper = *BLooper::Run(std::make_unique<RecordingLooper>(record));

	// When
	std::vector<std::thread> senders;
	for (std::uint32_t sender = 0; sender < 4; sender++) {
		senders.emplace_back([&, sender] {
			for (std::uint32_t i = 0; i < 1000; i++)
				(void)looper.SendMessage(sender * 10000 + i);
		});
	}
	for (std::thread& thread : senders)
		thread.join();
	ASSERT_TRUE(record->WaitFor(4000));
	QuitAndWait(looper);

	// Then
	std::vector<std::uint32_t> last(4, 0);
	std::vector<std::uint32_t> count(4, 0);
	for (const std::string& entry : record->Received()) {
		if (entry == "looper:" + std::to_string(kQuitRequested))
			continue;
		const auto what = static_cast<std::uint32_t>(std::stoul(entry.substr(7)));
		const std::uint32_t sender = what / 10000;
		ASSERT_LT(sender, 4u);
		if (count[sender] > 0) {
			EXPECT_GT(what % 10000, last[sender]);
		}
		last[sender] = what % 10000;
		count[sender]++;
	}
	EXPECT_EQ(count, (std::vector<std::uint32_t>{1000, 1000, 1000, 1000}));
}


} // namespace
