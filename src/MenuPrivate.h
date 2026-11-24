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

#include <ngui/Message.h>
#include <ngui/View.h>


namespace ngui {


class BMenu;


// The arrows above and below a menu that is higher than the screen
// (Haiku's UpperScroller and LowerScroller): the mouse over an enabled one
// scrolls the menu.
class MenuScroller : public BView {
public:
								MenuScroller(gfx::Rect frame, bool upper);

			void				Draw(DrawContext& context) override;

			bool				IsEnabled() const;
			void				SetEnabled(bool enabled);

private:
			bool				fUpper;
			bool				fEnabled = false;
};


// The height of a scroller (Haiku's kScrollerHeight).
inline constexpr float kMenuScrollerHeight = 12;


// The root view of a menu's popup (Haiku: BMenuWindow with
// B_BORDERED_WINDOW_LOOK and its BMenuFrame): one pixel of border, drawn
// here since there is no decorator, and the menu inside at (1, 1). An empty
// menu shows "<empty>", as Haiku's BMenuFrame.
//
// A menu higher than the screen gets scrollers above and below it (Haiku's
// BMenuWindow::AttachScrollers()); the menu is then as high as the room
// between them and scrolls its bounds (Haiku: the menu keeps its height in
// a smaller BMenuFrame).
class MenuFrame : public BView {
public:
								MenuFrame(gfx::Rect frame, BMenu* menu);

			void				Draw(DrawContext& context) override;

			// The menu is a child, laid out at its full height.
			void				AttachScrollers();
			bool				HasScrollers() const;
			// The menu's items are laid out anew at \a height.
			void				ContentResized(float height);
			// Scrolls if \a screen is over an enabled scroller.
			bool				CheckForScrolling(gfx::Point screen);
			bool				TryScrollBy(float step);
			bool				TryScrollTo(float where);
			void				SetSmallStep(float step);
			void				GetSteps(float* smallStep, float* largeStep) const;
			// Scrolls so that \a top to \a bottom of the menu are visible.
			void				ScrollToShow(float top, float bottom);

private:
			void				_ScrollBy(float step);

			BMenu*				fMenu;
			MenuScroller*		fUpperScroller = nullptr;
			MenuScroller*		fLowerScroller = nullptr;
			float				fScrollStep = 19.0f;
			float				fValue = 0;
			float				fLimit = 0;
};


// What a window gives to the menu tracking while it runs: all mouse events
// on the screen, from every surface, and the keys.
// The what of the tick that re-evaluates the mouse while it stands still
// (the navigation area's timeout); the window passes it to its tracking.
inline constexpr std::uint32_t kMenuTrackerTick = FourCharCode("_MTT");


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
	// The tick the tracking asked for (kMenuTrackerTick to the window).
	virtual	void				Tick() = 0;
	// The mouse wheel turned by \a deltaY (kMouseWheelChanged).
	virtual	void				TrackWheel(float deltaY, bool shift) = 0;
};


} // namespace ngui
