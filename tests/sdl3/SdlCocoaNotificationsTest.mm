/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The system's notifications on macOS (module P5): what a notification
// becomes for UNUserNotificationCenter, the refusal outside an application
// bundle (the test program has none) and the way a click goes. A delivered
// notification needs a bundle and the user's permission.


#include "platform/sdl3/CocoaNotifications.h"

#import <Foundation/Foundation.h>

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
using namespace ngui::platform;
using namespace ngui::platform::sdl3;
using namespace ngui::test;


constexpr std::uint32_t kOpened = 0x6f70656e;	// 'open'


TEST(SdlCocoaNotificationsTest, ANotificationBecomesARequest)
{
	// What is tested: Notifications as content of requests (ContentOf()):
	// an Important one from "example-mail" in the group "Mail" with the id
	// "mail"; a Progress one at 0.75 with the text "Fetching" and no id; an
	// Information one.
	// Success criteria: The first has the identifier "mail", its texts, the
	// thread "Mail" and is time sensitive; the second a new identifier of
	// 36 characters (a UUID), the body "Fetching\n75 %", the thread
	// "example-mail", not time sensitive; the third another new identifier.
	// Why it matters: The identifier makes a notification with the same id
	// take the place of the one delivered, and the thread groups them as
	// Haiku's groups do (module P5).
	//
	// Given
	NotificationSpec mail;
	mail.kind = NotificationSpec::Kind::Important;
	mail.application = "example-mail";
	mail.group = "Mail";
	mail.id = "mail";
	mail.title = "New mail";
	mail.content = "From Ada";
	NotificationSpec progress;
	progress.kind = NotificationSpec::Kind::Progress;
	progress.application = "example-mail";
	progress.content = "Fetching";
	progress.progress = 0.75f;
	NotificationSpec plain;

	// When
	const CocoaNotificationContent first = ContentOf(mail);
	const CocoaNotificationContent second = ContentOf(progress);
	const CocoaNotificationContent third = ContentOf(plain);

	// Then
	EXPECT_EQ(first.identifier, "mail");
	EXPECT_EQ(first.title, "New mail");
	EXPECT_EQ(first.body, "From Ada");
	EXPECT_EQ(first.thread, "Mail");
	EXPECT_TRUE(first.timeSensitive);
	EXPECT_EQ(second.identifier.size(), 36u);
	EXPECT_EQ(second.body, "Fetching\n75 %");
	EXPECT_EQ(second.thread, "example-mail");
	EXPECT_FALSE(second.timeSensitive);
	EXPECT_NE(third.identifier, second.identifier);
}


TEST(SdlCocoaNotificationsTest, WithoutABundleThereAreNone)
{
	// What is tested: CocoaNotifications::Create() in the test program,
	// which is no application bundle (no bundle identifier).
	// Success criteria: It fails with NotSupported (instead of
	// UNUserNotificationCenter throwing).
	// Why it matters: macOS gives notifications to bundles only; a program
	// without one must hear so from Send() rather than crash (module P5).
	//
	// Given
	if (NSBundle.mainBundle.bundleIdentifier != nil)
		GTEST_SKIP() << "the test runs in a bundle";

	// When
	const auto created = CocoaNotifications::Create();

	// Then
	ASSERT_FALSE(created.has_value());
	EXPECT_EQ(created.error().code, ErrorCode::NotSupported);
}


TEST(SdlCocoaNotificationsTest, AClickSendsTheClickMessage)
{
	// What is tested: The click messages kept by identifier (Remember(), as
	// Show() keeps them): "mail" with kOpened to a looper of the test, then
	// "done" without one; the user clicks "done", "mail" and "unknown"
	// (Clicked(), as the center's delegate does for the default action).
	// Success criteria: The looper gets kOpened once.
	// Why it matters: A click on a notification must reach the application
	// (module P5), and only for the notification that has a click message.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	const auto started = BLooper::Run(std::make_unique<RecordingLooper>(record));
	ASSERT_TRUE(started.has_value());
	const BMessenger looper = *started;
	NotificationSpec mail;
	mail.target = looper;
	mail.onClick = BMessage(kOpened);
	NotificationSpec done;

	// When
	CocoaNotifications::Remember("mail", mail);
	CocoaNotifications::Remember("done", done);
	CocoaNotifications::Clicked("done");
	CocoaNotifications::Clicked("mail");
	CocoaNotifications::Clicked("unknown");
	const bool clicked = record->WaitFor(1);

	// Then
	ASSERT_TRUE(clicked);
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"looper:" + std::to_string(kOpened)}));
	(void)looper.SendMessage(kQuitRequested);
}


} // namespace
