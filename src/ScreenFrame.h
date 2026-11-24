/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <vector>

#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/platform/Platform.h>

#include "compat/Rect.h"


namespace ngui {


// The screen \a where is on, as Haiku's inclusive BScreen::Frame(): the
// first screen if none contains it; without a platform a very large one.
// Used to place menus and tooltips.
inline compat::BRect
ScreenFrameAt(gfx::Point where)
{
	gfx::Rect frame{0, 0, 65536, 65536};
	if (platform::Platform* platform = platform::Current()) {
		const std::vector<platform::DisplayInfo> displays = platform->Displays();
		if (!displays.empty())
			frame = displays.front().frame;
		for (const platform::DisplayInfo& display : displays) {
			if (display.frame.Contains(where)) {
				frame = display.frame;
				break;
			}
		}
	}
	return compat::BRect(frame.x0, frame.y0, frame.x1 - 1, frame.y1 - 1);
}


} // namespace ngui
