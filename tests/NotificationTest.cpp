/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Notifications of the system (module P5): BNotification and what it hands
// to the platform, on the headless platform, which keeps them and clicks
// them as a user would.


#include <ngui/Notification.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Bitmap.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/test/Driver.h>


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOpenMail = 0x6f6d616c;	// 'omal'


// An application that counts the kOpenMail it gets.
class MailApp : public BApplication {
public:
	MailApp()
		:
		BApplication("application/x-vnd.example-mail")
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kOpenMail) {
			if (message->GetString("mailbox", "") == "inbox")
				clicks++;
			return;
		}
		BApplication::MessageReceived(message);
	}

	std::atomic<int> clicks = 0;	// with "mailbox" "inbox"
};


TEST(NotificationTest, ANotificationKeepsWhatItIsGiven)
{
	// What is tested: A BNotification of type Progress made while the
	// application "application/x-vnd.example-mail" exists: group, title,
	// content and id set; progress set to 0.5, then 1.5, -1 and NaN; a 2 × 2
	// icon set, the notification copied, the icon removed and an invalid
	// bitmap given as icon.
	// Success criteria: It keeps the values; its source is the application's
	// signature with the name "example-mail"; the progress stays within 0
	// to 1 (1.5 gives 1, -1 and NaN give 0); the copy has an icon of its
	// own; SetIcon(nullptr) removes it; the invalid bitmap fails with
	// InvalidArgument.
	// Why it matters: BNotification is Haiku's (module P5); what it keeps is
	// what the system shows.
	//
	// Given
	MailApp app;
	BNotification notification(BNotificationType::Progress);
	const BBitmap icon(gfx::Rect{0, 0, 2, 2}, BColorSpace::RGBA32);
	const BBitmap invalid(gfx::Rect{0, 0, 0, 0}, BColorSpace::RGBA32);

	// When
	notification.SetGroup("Mail");
	notification.SetTitle("New mail");
	notification.SetContent("3 messages");
	notification.SetMessageID("fetch");
	notification.SetProgress(0.5f);
	const float half = notification.Progress();
	notification.SetProgress(1.5f);
	const float over = notification.Progress();
	notification.SetProgress(-1);
	const float under = notification.Progress();
	notification.SetProgress(std::nanf(""));
	const float nan = notification.Progress();
	const bool iconSet = notification.SetIcon(&icon).has_value();
	const BNotification copy(notification);
	notification.SetIcon(nullptr).value();
	const auto refused = notification.SetIcon(&invalid);

	// Then
	EXPECT_EQ(notification.Type(), BNotificationType::Progress);
	EXPECT_EQ(notification.SourceSignature(), "application/x-vnd.example-mail");
	EXPECT_EQ(notification.SourceName(), "example-mail");
	EXPECT_EQ(notification.Group(), "Mail");
	EXPECT_EQ(notification.Title(), "New mail");
	EXPECT_EQ(notification.Content(), "3 messages");
	EXPECT_EQ(notification.MessageID(), "fetch");
	EXPECT_FLOAT_EQ(half, 0.5f);
	EXPECT_FLOAT_EQ(over, 1);
	EXPECT_FLOAT_EQ(under, 0);
	EXPECT_FLOAT_EQ(nan, 0);
	EXPECT_TRUE(iconSet);
	ASSERT_NE(copy.Icon(), nullptr);
	EXPECT_NE(copy.Icon(), &icon);
	EXPECT_EQ(copy.Title(), "New mail");
	EXPECT_EQ(notification.Icon(), nullptr);
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
}


TEST(NotificationTest, SendHandsItToThePlatform)
{
	// What is tested: The application "application/x-vnd.example-mail" on
	// the headless platform; an Important notification with group, title,
	// content, a click message (kOpenMail with "mailbox" "inbox") and a 2 × 1
	// icon in B_RGB32 (red, then half blue) sent for 5 s; an Information
	// notification without anything more sent with the default time.
	// Success criteria: The platform shows two: the first of kind Important
	// from "example-mail" with the texts, 5000 ms, the application as target
	// and the click message, and the icon as RGBA bytes 255 0 0 255, 0 0 128
	// 255; the second of kind Information with a timeout of -1 ms, no click
	// and no icon.
	// Why it matters: The platform's notifications show what Send() hands
	// on (module P5); the icon goes to the systems as plain RGBA.
	//
	// Given
	TestEnvironment environment;
	MailApp app;
	auto owned = std::make_unique<HeadlessPlatform>();
	HeadlessPlatform* platform = owned.get();
	ASSERT_TRUE(app.UsePlatform(std::move(owned)).has_value());
	Driver driver(app);
	BBitmap icon(gfx::Rect{0, 0, 2, 1}, BColorSpace::RGB32);
	// B_RGB32 lies as B, G, R, unused
	const std::uint8_t pixels[] = {0, 0, 255, 0, 128, 0, 0, 0};
	ASSERT_TRUE(icon.ImportBits(pixels, 8, 0, BColorSpace::RGB32).has_value());
	BNotification mail(BNotificationType::Important);
	mail.SetGroup("Mail");
	mail.SetTitle("New mail");
	mail.SetContent("From Ada");
	BMessage open(kOpenMail);
	(void)open.AddString("mailbox", "inbox");
	mail.SetOnClickMessage(open);
	ASSERT_TRUE(mail.SetIcon(&icon).has_value());
	BNotification plain(BNotificationType::Information);
	plain.SetTitle("Done");

	// When
	const auto sent = mail.Send(5s);
	const auto sentPlain = plain.Send();
	const std::vector<NotificationSpec> shown = platform->Notifications();

	// Then
	ASSERT_TRUE(sent.has_value()) << sent.error().message;
	ASSERT_TRUE(sentPlain.has_value());
	ASSERT_EQ(shown.size(), 2u);
	EXPECT_EQ(shown[0].kind, NotificationSpec::Kind::Important);
	EXPECT_EQ(shown[0].application, "example-mail");
	EXPECT_EQ(shown[0].group, "Mail");
	EXPECT_EQ(shown[0].title, "New mail");
	EXPECT_EQ(shown[0].content, "From Ada");
	EXPECT_EQ(shown[0].timeout, 5000ms);
	EXPECT_EQ(shown[0].target, AppMessenger());
	ASSERT_TRUE(shown[0].onClick.has_value());
	EXPECT_EQ(shown[0].onClick->what, kOpenMail);
	EXPECT_EQ(shown[0].iconWidth, 2);
	EXPECT_EQ(shown[0].iconHeight, 1);
	EXPECT_EQ(shown[0].icon, (std::vector<std::uint8_t>{255, 0, 0, 255, 0, 0, 128, 255}));
	EXPECT_EQ(shown[1].kind, NotificationSpec::Kind::Information);
	EXPECT_EQ(shown[1].timeout, -1ms);
	EXPECT_FALSE(shown[1].onClick.has_value());
	EXPECT_TRUE(shown[1].icon.empty());
}


TEST(NotificationTest, AClickReachesTheApplication)
{
	// What is tested: The application on the headless platform sends a
	// progress notification with the id "fetch" (progress 0.25) and a
	// click message, then again with the same id (progress 0.75), then one
	// without a click; the user clicks the first and the last
	// (ClickNotification()).
	// Success criteria: The platform shows two notifications: the second
	// sending took the place of the first (progress 0.75). The click on it
	// gives the application kOpenMail with "mailbox" "inbox" once; the
	// click on the one without a click message fails with InvalidArgument.
	// Why it matters: A progress is one notification that changes, and a
	// click must reach the application (module P5), as Haiku's notifications
	// do.
	//
	// Given
	TestEnvironment environment;
	MailApp app;
	auto owned = std::make_unique<HeadlessPlatform>();
	HeadlessPlatform* platform = owned.get();
	ASSERT_TRUE(app.UsePlatform(std::move(owned)).has_value());
	Driver driver(app);
	BNotification progress(BNotificationType::Progress);
	progress.SetMessageID("fetch");
	BMessage open(kOpenMail);
	(void)open.AddString("mailbox", "inbox");
	progress.SetOnClickMessage(open);
	BNotification plain(BNotificationType::Information);

	// When
	progress.SetProgress(0.25f);
	ASSERT_TRUE(progress.Send().has_value());
	progress.SetProgress(0.75f);
	ASSERT_TRUE(progress.Send().has_value());
	ASSERT_TRUE(plain.Send().has_value());
	const std::vector<NotificationSpec> shown = platform->Notifications();
	const auto clicked = platform->ClickNotification(0);
	const auto refused = platform->ClickNotification(1);
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// Then
	ASSERT_EQ(shown.size(), 2u);
	EXPECT_EQ(shown[0].id, "fetch");
	EXPECT_FLOAT_EQ(shown[0].progress, 0.75f);
	EXPECT_TRUE(clicked.has_value());
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::InvalidArgument);
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (app.clicks == 0 && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(5ms);
	EXPECT_EQ(app.clicks, 1);
}


TEST(NotificationTest, SendNeedsARunningApplication)
{
	// What is tested: A notification sent while no application runs.
	// Success criteria: Send() fails with NotSupported.
	// Why it matters: Without a platform there are no notifications to show
	// it; the caller must hear so rather than lose it silently.
	//
	// Given
	BNotification notification(BNotificationType::Error);
	notification.SetTitle("Failed");

	// When
	const auto sent = notification.Send();

	// Then
	ASSERT_FALSE(sent.has_value());
	EXPECT_EQ(sent.error().code, ErrorCode::NotSupported);
}


} // namespace
