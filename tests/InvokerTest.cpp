/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Invoker.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


TEST(InvokerTest, InvokeSendsItsMessageToItsTarget)
{
	// What is tested: An invoker with message 10 aimed at a handler, a
	// reply handler, Invoke() without and with an explicit message, and an
	// invoker without a message.
	// Success criteria: Invoke() sends a copy of its own message, or the
	// given one; the reply of the target reaches the reply handler; without
	// a message Invoke() fails with InvalidArgument; Command() is what of
	// the message, 0 without one.
	// Why it matters: Every control and menu item is an invoker (W2, W3);
	// the message must stay with the invoker so that it can be sent again.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingHandler target("target", record);
	RecordingHandler replies("replies", record);
	target.action = [](BMessage& message) {
		if (message.what == 10)
			(void)message.SendReply(11);
	};
	owned->AddHandler(&target);
	owned->AddHandler(&replies);
	BInvoker invoker(std::make_unique<BMessage>(10), BMessenger());
	invoker.SetTarget(&target);
	invoker.SetHandlerForReply(&replies);
	const bool replyTarget = invoker.HandlerForReply() == BMessenger(&replies);
	const BMessenger looper = *BLooper::Run(std::move(owned));
	BInvoker empty;

	// When
	ASSERT_TRUE(invoker.Invoke().has_value());
	ASSERT_TRUE(invoker.Invoke().has_value());
	const BMessage other(12);
	ASSERT_TRUE(invoker.Invoke(&other).has_value());
	ASSERT_TRUE(record->WaitFor(5));
	ASSERT_TRUE(looper.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(looper.WaitForLooperQuit(5s).has_value());

	// Then
	const std::vector<std::string> received = record->Received();
	EXPECT_EQ(std::count(received.begin(), received.end(), "target:10"), 2);
	EXPECT_EQ(std::count(received.begin(), received.end(), "replies:11"), 2);
	EXPECT_EQ(std::count(received.begin(), received.end(), "target:12"), 1);
	EXPECT_EQ(invoker.Command(), 10u);
	ASSERT_NE(invoker.Message(), nullptr);
	EXPECT_TRUE(invoker.IsTargetLocal());
	EXPECT_TRUE(replyTarget);
	EXPECT_EQ(empty.Invoke().error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(empty.Command(), 0u);
}


} // namespace
