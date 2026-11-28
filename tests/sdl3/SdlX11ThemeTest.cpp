/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The system's colors on Linux (module G6): SDL reads light or dark from
// the desktop's portal (org.freedesktop.portal.Settings, namespace
// org.freedesktop.appearance, key color-scheme), NewGUI the accent color
// (key accent-color); both hear their change (SettingChanged). The test is
// the portal (FakePortal.h), on a session bus of its own; SDL's X11 driver
// needs an X server (Xvfb in the CI). Without either, or when a real portal
// owns the name, the tests are skipped.


#include <chrono>
#include <cstdlib>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/Application.h>
#include <ngui/Palette.h>

#include "sdl3/FakePortal.h"
#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// Waits until the application's colors are \a palette; false after 5 s.
bool
WaitForColors(const Palette& palette)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (Palette::Current() != palette && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(10ms);
	return Palette::Current() == palette;
}


TEST(SdlX11ThemeTest, TheColorsFollowTheDesktop)
{
	// What is tested: The desktop's portal says dark (color-scheme 1); an
	// application on SDL's X11 driver with the colors of the system's
	// choice (BColorScheme::System); then the user switches the desktop to
	// light (SettingChanged with 2).
	// Success criteria: The platform reports Dark and the application shows
	// Haiku's dark colors (the portal has no accent color); after the switch
	// it reports Light and shows the light ones, without being restarted.
	// Skipped without an X server or a session bus, or when a real portal
	// runs.
	// Why it matters: By default NewGUI follows the user's light or dark
	// choice, also while it runs (as decided for this block); on Linux the
	// desktop tells it through its portal.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr || std::getenv("DISPLAY") == nullptr)
		GTEST_SKIP() << "no session bus or no X server";
	FakePortal portal(1);
	if (!portal.IsOwner())
		GTEST_SKIP() << "a portal of the desktop runs";
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	BApplication::SetColorScheme(BColorScheme::System);

	// When
	RunningApp app;
	if (app.Platform() == nullptr)
		GTEST_SKIP() << "no X11 video";
	const bool dark = WaitForColors(Palette::Dark());
	const platform::SystemColorScheme reportedDark = app.Platform()->ColorScheme();
	portal.SwitchScheme(2);
	const bool light = WaitForColors(Palette::Default());
	const platform::SystemColorScheme reportedLight = app.Platform()->ColorScheme();

	// Then
	EXPECT_TRUE(dark);
	EXPECT_EQ(reportedDark, platform::SystemColorScheme::Dark);
	EXPECT_TRUE(light);
	EXPECT_EQ(reportedLight, platform::SystemColorScheme::Light);
}


TEST(SdlX11ThemeTest, TheAccentColorFollowsTheDesktop)
{
	// What is tested: The desktop's portal says dark with GNOME's blue accent
	// (53, 132, 228); an application on SDL's X11 driver with the colors of
	// the system's choice; then the user picks GNOME's green (46, 194, 126),
	// then no accent color (SettingChanged of accent-color).
	// Success criteria: The application shows Haiku's dark colors with the
	// blue accent (Palette::WithAccent()), then with the green one, then
	// without one, without being restarted. Skipped as above.
	// Why it matters: Following the system includes its accent color as the
	// color of focus and selection, also when the user changes it (as decided
	// for this block); SDL does not report it, NewGUI asks the portal itself.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr || std::getenv("DISPLAY") == nullptr)
		GTEST_SKIP() << "no session bus or no X server";
	FakePortal portal(1, FakePortal::Rgb{53 / 255.0, 132 / 255.0, 228 / 255.0});
	if (!portal.IsOwner())
		GTEST_SKIP() << "a portal of the desktop runs";
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	BApplication::SetColorScheme(BColorScheme::System);
	const gfx::Color blue = gfx::Color::FromRgba8(53, 132, 228, 255);
	const gfx::Color green = gfx::Color::FromRgba8(46, 194, 126, 255);

	// When
	RunningApp app;
	if (app.Platform() == nullptr)
		GTEST_SKIP() << "no X11 video";
	const bool withBlue = WaitForColors(Palette::Dark().WithAccent(blue));
	portal.SetAccent(FakePortal::Rgb{46 / 255.0, 194 / 255.0, 126 / 255.0});
	const bool withGreen = WaitForColors(Palette::Dark().WithAccent(green));
	portal.SetAccent(std::nullopt);
	const bool withNone = WaitForColors(Palette::Dark());

	// Then
	EXPECT_TRUE(withBlue);
	EXPECT_TRUE(withGreen);
	EXPECT_TRUE(withNone);
}


} // namespace
