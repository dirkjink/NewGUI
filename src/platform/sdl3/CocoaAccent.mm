/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The accent color on macOS: NSColor's controlAccentColor in sRGB, heard
// anew on NSSystemColorsDidChangeNotification, which AppKit posts when the
// user picks another one. Made and told on the main thread.


#include "platform/sdl3/SystemAccent.h"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

#import <AppKit/AppKit.h>


namespace ngui::platform::sdl3 {


namespace {


std::optional<gfx::Color>
ReadAccent()
{
	NSColor* accent = [[NSColor controlAccentColor]
		colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
	if (accent == nil)
		return std::nullopt;
	const auto byte = [](CGFloat component) {
		const double clamped = std::fmin(std::fmax(static_cast<double>(component), 0.0), 1.0);
		return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
	};
	return gfx::Color::FromRgba8(byte(accent.redComponent), byte(accent.greenComponent),
		byte(accent.blueComponent), 255);
}


class CocoaAccent final : public SystemAccent {
public:
	explicit CocoaAccent(Changed changed)
		:
		fChanged(std::move(changed)),
		fColor(ReadAccent())
	{
		fObserver = [[NSNotificationCenter defaultCenter]
			addObserverForName:NSSystemColorsDidChangeNotification
			object:nil
			queue:[NSOperationQueue mainQueue]
			usingBlock:^(NSNotification*) {
				_Changed();
			}];
	}

	~CocoaAccent() override
	{
		[[NSNotificationCenter defaultCenter] removeObserver:fObserver];
	}

	std::optional<gfx::Color> Color() const override
	{
		std::scoped_lock lock(fLock);
		return fColor;
	}

private:
	void _Changed()
	{
		const std::optional<gfx::Color> color = ReadAccent();
		{
			std::scoped_lock lock(fLock);
			if (fColor == color)
				return;
			fColor = color;
		}
		fChanged();
	}

	Changed					fChanged;
	mutable std::mutex		fLock;
	std::optional<gfx::Color> fColor;
	id						fObserver = nil;
};


} // namespace


std::unique_ptr<SystemAccent>
CreateSystemAccent(SystemAccent::Changed changed)
{
	return std::make_unique<CocoaAccent>(std::move(changed));
}


} // namespace ngui::platform::sdl3
