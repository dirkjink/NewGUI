/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The accent color on Linux (module G6), from the desktop's portal
// (org.freedesktop.appearance accent-color), without SDL: the test is the
// portal (FakePortal.h) on a session bus of its own (dbus-run-session in the
// CI); skipped without one or when a real portal owns the name.


#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/gfx/Color.h>

#include "platform/sdl3/SystemAccent.h"
#include "sdl3/FakePortal.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// Waits until \a changes reaches \a count; false after 5 s.
bool
WaitForChanges(const std::atomic<int>& changes, int count)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (changes < count && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(5ms);
	return changes >= count;
}


TEST(DbusAccentTest, ThePortalsAccentColorIsHeard)
{
	// What is tested: The portal says GNOME's blue accent (53, 132, 228 as
	// doubles); NewGUI asks it (CreateSystemAccent()); the user picks green
	// (46, 194, 126), then none (components outside 0 to 1), each a
	// SettingChanged.
	// Success criteria: The color is the blue one at once; after each change
	// the callback ran once more and the color is the green one, then none.
	// Why it matters: SDL does not report the accent color; this is how
	// NewGUI learns it on Linux and hears the user change it, in the portal's
	// own encoding.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr)
		GTEST_SKIP() << "no session bus";
	FakePortal portal(2, FakePortal::Rgb{53 / 255.0, 132 / 255.0, 228 / 255.0});
	if (!portal.IsOwner())
		GTEST_SKIP() << "a portal of the desktop runs";
	std::atomic<int> changes = 0;

	// When
	const std::unique_ptr<platform::sdl3::SystemAccent> accent
		= platform::sdl3::CreateSystemAccent([&changes] { ++changes; });
	ASSERT_NE(accent, nullptr);
	const std::optional<gfx::Color> blue = accent->Color();
	portal.SetAccent(FakePortal::Rgb{46 / 255.0, 194 / 255.0, 126 / 255.0});
	const bool heardGreen = WaitForChanges(changes, 1);
	const std::optional<gfx::Color> green = accent->Color();
	portal.SetAccent(std::nullopt);
	const bool heardNone = WaitForChanges(changes, 2);
	const std::optional<gfx::Color> none = accent->Color();

	// Then
	EXPECT_EQ(blue, gfx::Color::FromRgba8(53, 132, 228, 255));
	EXPECT_TRUE(heardGreen);
	EXPECT_EQ(green, gfx::Color::FromRgba8(46, 194, 126, 255));
	EXPECT_TRUE(heardNone);
	EXPECT_EQ(none, std::nullopt);
}


TEST(DbusAccentTest, ADesktopWithoutAnAccentColorHasNone)
{
	// What is tested: The portal knows no accent color (NotFound, as desktops
	// before GNOME 47 answer); NewGUI asks it; the user switches to dark (a
	// SettingChanged of another key).
	// Success criteria: The color is none; the callback never runs.
	// Why it matters: Without an accent color NewGUI keeps Haiku's own; a
	// change of light or dark is SDL's to report, not the accent's.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr)
		GTEST_SKIP() << "no session bus";
	FakePortal portal(2);
	if (!portal.IsOwner())
		GTEST_SKIP() << "a portal of the desktop runs";
	std::atomic<int> changes = 0;

	// When
	const std::unique_ptr<platform::sdl3::SystemAccent> accent
		= platform::sdl3::CreateSystemAccent([&changes] { ++changes; });
	ASSERT_NE(accent, nullptr);
	portal.SwitchScheme(1);
	std::this_thread::sleep_for(200ms);

	// Then
	EXPECT_EQ(accent->Color(), std::nullopt);
	EXPECT_EQ(changes, 0);
}


} // namespace
