/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Internal parts of the menus (W3b): the frame a shown menu sits in, and
// the interface through which a window hands its events to menu tracking.


#include <cstdint>
#include <string_view>

#include <ngui/View.h>


namespace ngui {


class BMenu;


// The root view of a menu's popup (Haiku: BMenuWindow with
// B_BORDERED_WINDOW_LOOK and its BMenuFrame): one pixel of border, drawn
// here since there is no decorator, and the menu inside at (1, 1). An empty
// menu shows "<empty>", as Haiku's BMenuFrame.
class MenuFrame : public BView {
public:
								MenuFrame(gfx::Rect frame, BMenu* menu);

			void				Draw(DrawContext& context) override;

private:
			BMenu*				fMenu;
};


// What a window gives to the menu tracking while it runs: all mouse events
// on the screen, from every surface, and the keys.
class MenuTracking {
public:
	virtual						~MenuTracking() = default;

	virtual	void				TrackMouse(std::uint32_t what, gfx::Point screen,
									std::uint32_t buttons) = 0;
	// True if the key was used.
	virtual	bool				TrackKey(std::uint32_t what, std::string_view bytes) = 0;
	// The window was deactivated or a popup closed by the window system.
	virtual	void				CancelTracking() = 0;
	// \a view leaves the window.
	virtual	void				ViewDetached(BView* view) = 0;
};


} // namespace ngui
