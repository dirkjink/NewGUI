/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The tracking of a menu bar's menus (W3b2) or of a stand-alone pop-up
// menu (W3c): Haiku's BMenuBar::_Track(), BMenu::_Track() and
// BMenu::KeyDown(), driven by the window's events instead of polling the
// mouse in a thread of their own. Internal.


#include <cstdint>
#include <functional>
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
class BView;


class MenuTracker : public MenuTracking {
public:
	// \a root is a menu bar or a stand-alone pop-up menu.
	explicit					MenuTracker(BMenu* root);
								~MenuTracker() override;

			bool				IsTracking() const;
			// \a screen is the mouse position, if known (not when the
			// keyboard starts).
			void				Start(int32 menuIndex, bool sticky, bool showMenu,
									const gfx::Rect* specialRect,
									std::optional<gfx::Point> screen, std::uint32_t buttons);
			// Shows the stand-alone root in a popup of \a window and tracks
			// it (Haiku's BPopUpMenu::_StartTrack()); \a done gets the
			// chosen item or nullptr. False if it cannot be shown.
			bool				StartPopUp(BWindow* window, bool sticky,
									const gfx::Rect* clickToOpen,
									std::optional<gfx::Point> screen, std::uint32_t buttons,
									bool autoInvoke, std::function<void(BMenuItem*)> done);
			void				Tick() override;
			void				Stop(BMenuItem* chosen);

			void				TrackMouse(std::uint32_t what, gfx::Point screen,
									std::uint32_t buttons) override;
			bool				TrackKey(std::uint32_t what, std::string_view bytes) override;
			void				CancelTracking() override;
			void				ViewDetached(BView* view) override;

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
			void				_SetSticky(bool sticky);
			void				_StealFocus();
			void				_RestoreFocus();
			void				_KeyDown(BMenu* menu, char key, std::string_view bytes);
			bool				_SelectNextItem(BMenu* menu, BMenuItem* item, bool forward);
			BMenuItem*			_NextItem(const BMenu* menu, BMenuItem* item,
									bool forward) const;
			bool				_OpenOrChoose(BMenu* menu, BMenuItem* item);
			BMenu*				_DeepestShownMenu() const;
			bool				_OnScreen(const BMenu* menu, gfx::Point screen) const;
			void				_UpdateStateOpenSelect(BMenu* menu, BMenuItem* item,
									gfx::Point screen);
			void				_UpdateNavigationArea(BMenu* menu, gfx::Point position,
									Level& level);
			void				_ScheduleTick(Clock::Time when);

			void				_Begin(BWindow* window, bool sticky,
									const gfx::Rect* specialRect,
									std::optional<gfx::Point> screen, std::uint32_t buttons);

			BMenu*				fRoot;
			BMenuBar*			fBar;
									// the root if it is a menu bar
			BWindow*			fWindow = nullptr;
			bool				fTracking = false;
			bool				fAutoInvoke = true;
			std::function<void(BMenuItem*)> fDone;
			// the end of the click time of a pop-up menu: released before,
			// it stays open (Haiku's second Track() in _StartTrack())
			std::optional<Clock::Time> fClickEnd;
			bool				fSticky = false;
			gfx::Point			fWhere = {0, 0};
			bool				fMouseKnown = false;
			// Haiku's MENU_STATE_KEY_TO_SUBMENU: the keyboard navigates, the
			// mouse outside the menus does not deselect
			bool				fKeyNavigation = false;
			// Haiku's fPrevFocusToken: the focus view before the bar stole
			// the focus
			BView*				fPrevFocus = nullptr;
			bool				fFocusStolen = false;
			std::uint32_t		fButtons = 0;
			std::optional<gfx::Rect> fExtraRect;
			std::map<const BMenu*, Level> fLevels;
			std::unique_ptr<BMessageRunner> fTicker;
};


} // namespace ngui
