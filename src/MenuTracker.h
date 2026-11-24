/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The tracking of a menu bar's menus (W3b2): Haiku's BMenuBar::_Track()
// and BMenu::_Track(), driven by the window's events instead of polling
// the mouse in a thread of their own. Internal.


#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string_view>

#include <ngui/Clock.h>
#include <ngui/MessageRunner.h>
#include <ngui/gfx/Rect.h>

#include "MenuPrivate.h"
#include "compat/Rect.h"


namespace ngui {


class BMenu;
class BMenuBar;
class BMenuItem;


// The what of the tick that re-evaluates the mouse while it stands still
// (the navigation area's timeout).
inline constexpr std::uint32_t kMenuTrackerTick = FourCharCode("_MTT");


class MenuTracker : public MenuTracking {
public:
	explicit					MenuTracker(BMenuBar* bar);
								~MenuTracker() override;

			bool				IsTracking() const;
			void				Start(int32 menuIndex, bool sticky, bool showMenu,
									const gfx::Rect* specialRect, gfx::Point screen,
									std::uint32_t buttons);
			void				Tick();
			void				Stop(BMenuItem* chosen);

			void				TrackMouse(std::uint32_t what, gfx::Point screen,
									std::uint32_t buttons) override;
			bool				TrackKey(std::uint32_t what, std::string_view bytes) override;
			void				CancelTracking() override;

private:
	// What Haiku's _Track() of each menu keeps on its stack.
	struct Level {
		compat::BRect		navAreaRectAbove;
		compat::BRect		navAreaRectBelow;
		Clock::Time			selectedTime{};
		// NewGUI: unset instead of Haiku's 0, which a clock may well show
		std::optional<Clock::Time> navigationAreaTime;
	};

			void				_Evaluate(bool buttonsChanged);
			BMenu*				_DeepestShownMenu() const;
			bool				_OnScreen(const BMenu* menu, gfx::Point screen) const;
			void				_UpdateStateOpenSelect(BMenu* menu, BMenuItem* item,
									gfx::Point screen);
			void				_UpdateNavigationArea(BMenu* menu, gfx::Point position,
									Level& level);
			void				_ScheduleTick(Clock::Time when);

			BMenuBar*			fBar;
			bool				fTracking = false;
			bool				fSticky = false;
			gfx::Point			fWhere = {0, 0};
			std::uint32_t		fButtons = 0;
			std::optional<gfx::Rect> fExtraRect;
			std::map<const BMenu*, Level> fLevels;
			std::unique_ptr<BMessageRunner> fTicker;
};


} // namespace ngui
