/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The accent color on macOS (module G6): NSColor's controlAccentColor,
// heard anew on NSSystemColorsDidChangeNotification. The CI cannot change
// the user's accent color, so the test asks what AppKit says and posts the
// notification itself.


#include "platform/sdl3/SystemAccent.h"

#import <AppKit/AppKit.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <ngui/gfx/Color.h>


namespace {


using namespace ngui;


TEST(SdlCocoaAccentTest, TheAccentColorIsAppKits)
{
	// What is tested: NewGUI asks the system's accent color
	// (CreateSystemAccent(), on the main thread, as SdlPlatform does); then
	// AppKit's NSSystemColorsDidChangeNotification arrives with the color
	// unchanged.
	// Success criteria: The color is controlAccentColor in sRGB, in 8-bit
	// components; the notification does not call the callback, since the
	// color stayed.
	// Why it matters: The accent color becomes the color of focus and
	// selection; a notification of other system colors must not make the
	// application take its colors anew for nothing.
	//
	// Given
	NSColor* expected = [[NSColor controlAccentColor]
		colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
	ASSERT_NE(expected, nil);
	const auto byte = [](CGFloat component) {
		return static_cast<std::uint8_t>(std::lround(static_cast<double>(component) * 255.0));
	};
	int changes = 0;

	// When
	const std::unique_ptr<platform::sdl3::SystemAccent> accent
		= platform::sdl3::CreateSystemAccent([&changes] { ++changes; });
	ASSERT_NE(accent, nullptr);
	[[NSNotificationCenter defaultCenter]
		postNotificationName:NSSystemColorsDidChangeNotification object:nil];
	[[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];

	// Then
	EXPECT_EQ(accent->Color(), gfx::Color::FromRgba8(byte(expected.redComponent),
		byte(expected.greenComponent), byte(expected.blueComponent), 255));
	EXPECT_EQ(changes, 0);
}


} // namespace
