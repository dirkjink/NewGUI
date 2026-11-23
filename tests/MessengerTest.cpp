/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Messenger.h>

#include <future>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK_");
constexpr std::uint32_t kAnswer = FourCharCode("ANSR");


// A running looper whose MessageReceived() runs \a action.
BMessenger
RunLooper(const std::shared_ptr<LooperRecord>& record, std::function<void(BMessage&)> action)
{
	auto looper = std::make_unique<RecordingLooper>(record);
	looper->action = std::move(action);
	return *BLooper::Run(std::move(looper));
}


void
QuitAndWait(const BMessenger& messenger)
{
	ASSERT_TRUE(messenger.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());
}


TEST(MessengerTest, SynchronousSendWaitsForTheReply)
{
	// What is tested: SendMessage() with a reply message, answered by the
	// looper with SendReply(), and not answered at all.
	// Success criteria: The answer arrives with its field and IsReply();
	// inside the handler the message was delivered and its source waiting;
	// an unanswered message gives kNoReply when it is destroyed.
	// Why it matters: Haiku's synchronous messaging; B_NO_REPLY keeps a
	// sender from waiting for ever on a handler that ignores the message.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	bool delivered = false;
	bool waiting = false;
	const BMessenger looper = RunLooper(record, [&](BMessage& message) {
		if (message.what != kAsk)
			return;
		delivered = message.WasDelivered();
		waiting = message.IsSourceWaiting();
		BMessage answer(kAnswer);
		(void)answer.AddInt32("value", 42);
		(void)message.SendReply(answer);
	});
	BMessage reply;
	BMessage ignored;

	// When
	ASSERT_TRUE(looper.SendMessage(BMessage(kAsk), &reply).has_value());
	ASSERT_TRUE(looper.SendMessage(BMessage(FourCharCode("IGNR")), &ignored).has_value());
	QuitAndWait(looper);

	// Then
	EXPECT_EQ(reply.what, kAnswer);
	EXPECT_EQ(reply.GetInt32("value", 0), 42);
	EXPECT_TRUE(reply.IsReply());
	EXPECT_TRUE(delivered);
	EXPECT_TRUE(waiting);
	EXPECT_EQ(ignored.what, kNoReply);
}


TEST(MessengerTest, RepliesCanGoToAnotherLooper)
{
	// What is tested: Looper A sends to looper B with A as reply address; B
	// answers with SendReply(); a second answer to the same message.
	// Success criteria: A receives the answer as a reply; B saw A as the
	// return address and its source not waiting; the second answer fails
	// with InvalidArgument (B_DUPLICATE_REPLY).
	// Why it matters: Asynchronous replies are how windows talk to each
	// other without blocking (CONVENTIONS.md 3.4).
	//
	// Given
	auto recordA = std::make_shared<LooperRecord>();
	auto recordB = std::make_shared<LooperRecord>();
	std::promise<bool> replied;
	const BMessenger a = RunLooper(recordA, [&](BMessage& message) {
		if (message.what == kAnswer)
			replied.set_value(message.IsReply());
	});
	bool waiting = true;
	bool rightAddress = false;
	ErrorCode second{};
	const BMessenger b = RunLooper(recordB, [&](BMessage& message) {
		if (message.what != kAsk)
			return;
		waiting = message.IsSourceWaiting();
		rightAddress = message.ReturnAddress() == a;
		(void)message.SendReply(kAnswer);
		second = message.SendReply(kAnswer).error().code;
	});

	// When
	ASSERT_TRUE(b.SendMessage(BMessage(kAsk), a).has_value());
	auto future = replied.get_future();
	ASSERT_EQ(future.wait_for(5s), std::future_status::ready);
	QuitAndWait(a);
	QuitAndWait(b);

	// Then
	EXPECT_TRUE(future.get());
	EXPECT_FALSE(waiting);
	EXPECT_TRUE(rightAddress);
	EXPECT_EQ(second, ErrorCode::InvalidArgument);
}


TEST(MessengerTest, WaitingEndsOnTimeoutQuitAndDeadlock)
{
	// What is tested: A synchronous send to a busy looper with a timeout, a
	// message still queued when its looper quits, a synchronous send from a
	// looper to itself, and WaitForLooperQuit() with a timeout.
	// Success criteria: TimedOut after the timeout; the sender of the
	// message sent around the quit gets kNoReply if it was queued, or
	// NotFound if the looper had closed already, and never waits for ever;
	// InvalidArgument instead of a deadlock; TimedOut while the looper
	// still runs.
	// Why it matters: A UI thread must never hang for ever on another
	// thread, nor on itself.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	std::promise<void> release;
	std::shared_future<void> released = release.get_future().share();
	ErrorCode self{};
	BMessenger looper;
	looper = RunLooper(record, [&](BMessage& message) {
		if (message.what == 1)
			released.wait();
		if (message.what == 2) {
			BMessage reply;
			self = looper.SendMessage(BMessage(3), &reply).error().code;
		}
		if (message.what == 4)
			(void)looper.SendMessage(kQuitRequested);
	});

	// When
	BMessage late;
	ASSERT_TRUE(looper.SendMessage(1).has_value());
	const ErrorCode timedOut = looper.SendMessage(BMessage(5), &late, 20ms).error().code;
	const ErrorCode stillRunning = looper.WaitForLooperQuit(10ms).error().code;
	ASSERT_TRUE(looper.SendMessage(2).has_value());
	ASSERT_TRUE(looper.SendMessage(4).has_value());
	BMessage dropped;
	ErrorCode droppedResult{};
	std::thread waiter([&] {
		const auto sent = looper.SendMessage(BMessage(6), &dropped, 5s);
		droppedResult = sent.has_value() ? ErrorCode{} : sent.error().code;
	});
	// Mostly the message is queued before the looper goes on; whether it
	// is depends on the scheduler, so both outcomes are accepted below.
	std::this_thread::sleep_for(10ms);
	release.set_value();
	waiter.join();
	ASSERT_TRUE(looper.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(timedOut, ErrorCode::TimedOut);
	EXPECT_EQ(stillRunning, ErrorCode::TimedOut);
	EXPECT_EQ(self, ErrorCode::InvalidArgument);
	if (droppedResult == ErrorCode{}) {
		EXPECT_EQ(dropped.what, kNoReply);
	} else {
		EXPECT_EQ(droppedResult, ErrorCode::NotFound);
	}
}


TEST(MessengerTest, CopiesCarryNoDelivery)
{
	// What is tested: A delivered message, a copy of it and a message that
	// was never sent: WasDelivered(), SendReply(); the reply comes from a
	// detached message after the handler returned.
	// Success criteria: The copy is not delivered and owes no reply
	// (NotFound); an unsent message has nobody to reply to (NotFound); the
	// detached message can still answer the waiting sender.
	// Why it matters: The obligation to reply belongs to one message only,
	// as Haiku's; keeping it for later goes through DetachCurrentMessage().
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	std::unique_ptr<BMessage> kept;
	bool copyDelivered = true;
	ErrorCode copyReply{};
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingLooper* looperObject = owned.get();
	owned->action = [&](BMessage& message) {
		if (message.what == kAsk) {
			const BMessage copy = message;
			copyDelivered = copy.WasDelivered();
			BMessage writable = copy;
			copyReply = writable.SendReply(kAnswer).error().code;
			kept = looperObject->DetachCurrentMessage();
		} else if (message.what == 2 && kept != nullptr) {
			(void)kept->SendReply(kAnswer);
		}
	};
	const BMessenger looper = *BLooper::Run(std::move(owned));
	BMessage reply;

	// When
	std::thread asker([&] { (void)looper.SendMessage(BMessage(kAsk), &reply); });
	ASSERT_TRUE(record->WaitFor(1));
	ASSERT_TRUE(looper.SendMessage(2).has_value());
	asker.join();
	QuitAndWait(looper);
	BMessage unsent(kAsk);

	// Then
	EXPECT_FALSE(copyDelivered);
	EXPECT_EQ(copyReply, ErrorCode::NotFound);
	EXPECT_EQ(reply.what, kAnswer);
	EXPECT_EQ(unsent.SendReply(kAnswer).error().code, ErrorCode::NotFound);
	EXPECT_FALSE(unsent.WasDelivered());
}


TEST(MessengerTest, MessengersAreValues)
{
	// What is tested: A default messenger, messengers to the same and to
	// different targets, and a messenger to a handler outside any looper.
	// Success criteria: The default one and the one to a free handler are
	// invalid and sending fails with NotFound; messengers to the same target
	// compare equal, to different handlers not.
	// Why it matters: Messengers are copied freely between threads and kept
	// in messages and fields; they must be safe without a target.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingHandler handler("handler", record);
	owned->AddHandler(&handler);
	const BMessenger toHandler(&handler);
	const BMessenger toPreferred(nullptr, owned.get());
	const BMessenger looper = *BLooper::Run(std::move(owned));
	const BHandler freeHandler("free");

	// When / Then
	EXPECT_FALSE(BMessenger().IsValid());
	EXPECT_EQ(BMessenger().SendMessage(1).error().code, ErrorCode::NotFound);
	EXPECT_FALSE(BMessenger(&freeHandler).IsValid());
	EXPECT_TRUE(toHandler.IsValid());
	EXPECT_TRUE(toHandler.IsTargetLocal());
	EXPECT_TRUE(toHandler == BMessenger(toHandler));
	EXPECT_FALSE(toHandler == looper);
	EXPECT_FALSE(toPreferred == looper);
	QuitAndWait(looper);
	EXPECT_FALSE(toHandler.IsValid());
}


} // namespace
