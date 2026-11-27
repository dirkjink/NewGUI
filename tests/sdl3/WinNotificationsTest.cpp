/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The system's notifications on Windows (module P5): what a notification
// becomes as a balloon, and the icon in the notification area with its
// click. The click is sent to the icon's window as the shell sends it (a
// real one needs a user's mouse); without a taskbar the icon test is
// skipped.


#include "platform/sdl3/WinNotifications.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>

#include "support/RecordingLooper.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
// windows.h names these as macros (SendMessageW ...); BMessenger has them
#undef SendMessage
#undef DispatchMessage


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::platform::sdl3;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOpened = 0x6f70656e;	// 'open'


TEST(WinNotificationsTest, ANotificationBecomesABalloon)
{
	// What is tested: Notifications as balloons (BalloonOf()): an Important
	// one from "example-mail" in the group "Mail"; an Error one without a
	// group; a Progress one at 0.75 with the text "Fetching"; one with a
	// 32 × 32 icon; one with a title of 70 "é" and a text of 254 "a" and
	// an emoji (a surrogate pair) at characters 255 and 256.
	// Success criteria: The tips are "Mail", then "example-mail"; the flags
	// NIIF_WARNING, NIIF_ERROR, NIIF_INFO for the progress, NIIF_USER |
	// NIIF_LARGE_ICON for the icon; the progress text is "Fetching\n75 %";
	// the long title is cut to 63 characters, the long text to 254 (the
	// pair stays whole, so it goes).
	// Why it matters: The shell takes at most 63 and 255 characters and
	// shows no balloon without text; a cut surrogate pair would show as a
	// broken character (module P5).
	//
	// Given
	NotificationSpec important;
	important.kind = NotificationSpec::Kind::Important;
	important.application = "example-mail";
	important.group = "Mail";
	important.title = "New mail";
	NotificationSpec error;
	error.kind = NotificationSpec::Kind::Error;
	error.application = "example-mail";
	NotificationSpec progress;
	progress.kind = NotificationSpec::Kind::Progress;
	progress.content = "Fetching";
	progress.progress = 0.75f;
	NotificationSpec icon;
	icon.iconWidth = 32;
	icon.iconHeight = 32;
	icon.icon.assign(32 * 32 * 4, 255);
	NotificationSpec longer;
	for (int i = 0; i < 70; i++)
		longer.title += "\xC3\xA9";
	longer.content = std::string(254, 'a') + "\xF0\x9F\x98\x80";

	// When
	const Balloon first = BalloonOf(important);
	const Balloon second = BalloonOf(error);
	const Balloon third = BalloonOf(progress);
	const Balloon fourth = BalloonOf(icon);
	const Balloon fifth = BalloonOf(longer);

	// Then
	EXPECT_EQ(first.tip, L"Mail");
	EXPECT_EQ(first.title, L"New mail");
	EXPECT_EQ(first.flags, static_cast<std::uint32_t>(NIIF_WARNING));
	EXPECT_EQ(second.tip, L"example-mail");
	EXPECT_EQ(second.flags, static_cast<std::uint32_t>(NIIF_ERROR));
	EXPECT_EQ(second.text, L" ");
	EXPECT_EQ(third.flags, static_cast<std::uint32_t>(NIIF_INFO));
	EXPECT_EQ(third.text, L"Fetching\n75 %");
	EXPECT_EQ(fourth.flags, static_cast<std::uint32_t>(NIIF_USER | NIIF_LARGE_ICON));
	EXPECT_EQ(fifth.title.size(), 63u);
	EXPECT_EQ(fifth.text, std::wstring(254, L'a'));
}


TEST(WinNotificationsTest, AClickOnTheBalloonReachesTheApplication)
{
	// What is tested: The icon in the notification area (WinNotifications)
	// shows a notification with a click message (kOpened to a looper of the
	// test) and a 16 × 16 icon; the shell's click on the balloon
	// (NIN_BALLOONUSERCLICK to the icon's window) comes; then a
	// notification without a click message and a click on it.
	// Success criteria: Both balloons are shown; the looper gets kOpened
	// once, after the first click only. Skipped without a taskbar.
	// Why it matters: On Windows the balloon is the notification (module
	// P5); its click must reach the application, and a balloon without a
	// click message must not send the one before it.
	//
	// Given
	auto created = WinNotifications::Create();
	if (!created.has_value())
		GTEST_SKIP() << created.error().message;
	WinNotifications& notifications = **created;
	auto record = std::make_shared<LooperRecord>();
	const auto started = BLooper::Run(std::make_unique<RecordingLooper>(record));
	ASSERT_TRUE(started.has_value());
	const BMessenger looper = *started;
	NotificationSpec mail;
	mail.application = "example-mail";
	mail.title = "New mail";
	mail.content = "From Ada";
	mail.iconWidth = 16;
	mail.iconHeight = 16;
	mail.icon.assign(16 * 16 * 4, 200);
	mail.target = looper;
	mail.onClick = BMessage(kOpened);
	NotificationSpec plain;
	plain.title = "Done";
	auto* window = static_cast<HWND>(notifications.Window());
	const auto click = [window] {
		(void)SendMessageW(window, kNotificationIconMessage, 0,
			MAKELPARAM(NIN_BALLOONUSERCLICK, 1));
	};
	const auto waitShown = [&notifications](std::size_t count) {
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (notifications.CountShown() < count && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(5ms);
		return notifications.CountShown() >= count;
	};

	// When
	notifications.Show(mail);
	const bool shownFirst = waitShown(1);
	click();
	const bool clicked = record->WaitFor(1);
	notifications.Show(plain);
	const bool shownSecond = waitShown(2);
	click();
	std::this_thread::sleep_for(100ms);

	// Then
	EXPECT_TRUE(shownFirst);
	EXPECT_TRUE(shownSecond);
	ASSERT_TRUE(clicked);
	const std::vector<std::string> received = record->Received();
	EXPECT_EQ(received, (std::vector<std::string>{"looper:" + std::to_string(kOpened)}));
	(void)looper.SendMessage(kQuitRequested);
}


} // namespace
