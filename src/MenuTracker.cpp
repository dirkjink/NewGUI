/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/MenuBar.cpp (_Track()) and
 * Menu.cpp (_Track(), _UpdateStateOpenSelect(), _UpdateNavigationArea(),
 * _UpdateStateClose()), commit b1d497bb69. NewGUI:
 * - One object follows the mouse for the bar and all its open menus, on
 *   the window's thread: each mouse event (in screen coordinates) is
 *   evaluated as Haiku's polling loops would evaluate it, at the level of
 *   the deepest open menu under the mouse, else at the bar's.
 * - The close rules (sticky mode, choosing on release) are applied when
 *   the buttons change, as Haiku's loops did after waiting for a change.
 * - Submenus open at once (Haiku's kOpenSubmenuDelay is 0); while the
 *   mouse rests in a navigation area, a tick re-evaluates it after Haiku's
 *   timeout of one second (on Clock::Default()).
 * - Escape closes the menus; the other keys come with W3b2b.
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Marc Flerackers, mflerackers@androme.be
 */


#include "MenuTracker.h"

#include <algorithm>
#include <chrono>

#include <ngui/AppDefs.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/Window.h>


namespace ngui {


using compat::BPoint;
using compat::BRect;
using namespace std::chrono_literals;


namespace {


constexpr Clock::Time kNavigationAreaTimeout = 1s;
constexpr float kNavAreaThreshold = 8;


// \a menu on the screen, as Haiku's ConvertToScreen(Bounds()).
BRect
ScreenBounds(const BMenu* menu)
{
	const gfx::Rect bounds = menu->Bounds();
	const gfx::Point origin = menu->ConvertToScreen({bounds.x0, bounds.y0});
	return BRect(origin.x, origin.y, origin.x + bounds.Width() - 1,
		origin.y + bounds.Height() - 1);
}


} // namespace


MenuTracker::MenuTracker(BMenuBar* bar)
	:
	fBar(bar)
{
}


MenuTracker::~MenuTracker()
{
	Stop(nullptr);
}


bool
MenuTracker::IsTracking() const
{
	return fTracking;
}


void
MenuTracker::Start(int32 menuIndex, bool sticky, bool /*showMenu*/,
	const gfx::Rect* specialRect, gfx::Point screen, std::uint32_t buttons)
{
	BWindow* window = fBar->Window();
	if (fTracking || window == nullptr)
		return;

	fTracking = true;
	fLevels.clear();
	fSticky = sticky;
	fWhere = screen;
	fButtons = buttons;
	if (specialRect != nullptr)
		fExtraRect = *specialRect;
	else
		fExtraRect.reset();

	window->MenusBeginning();
	window->fMenuTracking = this;

	if (menuIndex != -1)
		fBar->_SelectItem(fBar->ItemAt(menuIndex), true, false);

	_Evaluate(false);
}


void
MenuTracker::Tick()
{
	if (fTracking)
		_Evaluate(false);
}


void
MenuTracker::Stop(BMenuItem* chosen)
{
	if (!fTracking)
		return;

	fTracking = false;
	fTicker.reset();
	BWindow* window = fBar->Window();

	if (fBar->fSelected != nullptr)
		fBar->_SelectItem(nullptr);
	if (window != nullptr && window->fMenuTracking == this)
		window->fMenuTracking = nullptr;

	fSticky = false;
	fLevels.clear();
	fExtraRect.reset();

	if (chosen != nullptr)
		(void)chosen->Invoke();

	if (window != nullptr)
		window->MenusEnded();
}


void
MenuTracker::TrackMouse(std::uint32_t /*what*/, gfx::Point screen, std::uint32_t buttons)
{
	if (!fTracking)
		return;

	const bool changed = buttons != fButtons;
	fWhere = screen;
	fButtons = buttons;
	_Evaluate(changed);
}


bool
MenuTracker::TrackKey(std::uint32_t what, std::string_view bytes)
{
	if (!fTracking)
		return false;

	// NewGUI: Escape for now; keyboard navigation comes with W3b2b.
	if (what == kKeyDown && bytes.size() == 1 && bytes[0] == keys::kEscape)
		Stop(nullptr);

	// The keys belong to the menus while they are open.
	return true;
}


void
MenuTracker::CancelTracking()
{
	Stop(nullptr);
}


// #pragma mark - private


BMenu*
MenuTracker::_DeepestShownMenu() const
{
	BMenu* menu = fBar;
	while (menu->fSelected != nullptr && menu->fSelected->Submenu() != nullptr
		&& menu->fSelected->Submenu()->_IsShown()) {
		menu = menu->fSelected->Submenu();
	}
	return menu == fBar ? nullptr : menu;
}


bool
MenuTracker::_OnScreen(const BMenu* menu, gfx::Point screen) const
{
	return ScreenBounds(menu).Contains(BPoint(screen.x, screen.y));
}


void
MenuTracker::_Evaluate(bool buttonsChanged)
{
	// The order of the checks is Haiku's: first the open submenus, deepest
	// first, then the bar.
	BMenu* deepest = _DeepestShownMenu();
	BMenu* level = nullptr;
	for (BMenu* menu = deepest; menu != nullptr && menu != fBar; menu = menu->Supermenu()) {
		if (_OnScreen(menu, fWhere)) {
			level = menu;
			break;
		}
	}

	BMenuItem* item = nullptr;
	if (level != nullptr) {
		item = level->_HitTestItems(level->ConvertFromScreen(fWhere));
		if (item != nullptr) {
			_UpdateStateOpenSelect(level, item, fWhere);
		} else if (level->fSelected != nullptr
			&& (level->fSelected->Submenu() == nullptr
				|| !level->fSelected->Submenu()->_IsShown())) {
			// over a separator: as outside the menu
			level->_SelectItem(nullptr);
		}
	} else {
		const gfx::Point local = fBar->ConvertFromScreen(fWhere);
		const bool overBar = fBar->Bounds().Contains(local);
		item = overBar ? fBar->_HitTestItems(local) : nullptr;

		// Mouse pointer outside the menus: if there's no other submenu
		// opened, the deepest menu deselects its item.
		if (deepest != nullptr && !overBar && deepest->fSelected != nullptr
			&& (deepest->fSelected->Submenu() == nullptr
				|| !deepest->fSelected->Submenu()->_IsShown())) {
			deepest->_SelectItem(nullptr);
		}

		if (item != nullptr) {
			fBar->_SelectItem(item);
		} else if (fBar->fSelected != nullptr && !fSticky && overBar) {
			fBar->_SelectItem(nullptr);
		}
	}

	if (!fTracking || !buttonsChanged)
		return;

	if (level != nullptr) {
		// Haiku's BMenu::_UpdateStateClose()
		if (fButtons != 0 && fSticky) {
			if (item == nullptr) {
				Stop(nullptr);
				return;
			}
			fSticky = false;
		} else if (fButtons == 0 && !fSticky) {
			if (fExtraRect.has_value() && fExtraRect->Contains(fWhere)) {
				fSticky = true;
				fExtraRect.reset();
			} else {
				if (item != level->fSelected)
					level->_SelectItem(item, false);
				Stop(item);
				return;
			}
		}
	} else {
		// Haiku's BMenuBar::_Track()
		if (fButtons != 0 && fSticky) {
			if (item == nullptr || (item->Submenu() != nullptr && item->Submenu()->_IsShown())) {
				// clicked outside the menu bar or on item with already
				// open sub menu
				Stop(nullptr);
				return;
			}
			fSticky = false;
		} else if (fButtons == 0 && !fSticky) {
			BMenuItem* selected = fBar->fSelected;
			if ((selected != nullptr && selected->Submenu() == nullptr) || item == nullptr) {
				// clicked on an item without a submenu or clicked and
				// released the mouse button outside the menu bar
				Stop(selected);
				return;
			}
			fSticky = true;
		}
	}
}


void
MenuTracker::_UpdateStateOpenSelect(BMenu* menu, BMenuItem* item, gfx::Point screen)
{
	Level& level = fLevels[menu];
	const Clock::Time now = Clock::Default().Now();
	const BPoint position(screen.x, screen.y);

	if (item != menu->fSelected) {
		if (!level.navigationAreaTime.has_value())
			level.navigationAreaTime = now;

		bool inNavAreaRectAbove = level.navAreaRectAbove.Contains(position);
		bool inNavAreaRectBelow = level.navAreaRectBelow.Contains(position);

		if (menu->fSelected == nullptr || (!inNavAreaRectAbove && !inNavAreaRectBelow)) {
			menu->_SelectItem(item, false);
			level.navAreaRectAbove = BRect();
			level.navAreaRectBelow = BRect();
			level.selectedTime = now;
			level.navigationAreaTime.reset();
		} else {
			const BPoint aboveLeftTop = level.navAreaRectAbove.LeftTop();
			bool isLeft = menu->ConvertFromScreen({aboveLeftTop.x, aboveLeftTop.y}).x == 0;
			BPoint p1, p2;
			if (inNavAreaRectAbove) {
				if (!isLeft) {
					p1 = level.navAreaRectAbove.LeftBottom();
					p2 = level.navAreaRectAbove.RightTop();
				} else {
					p2 = level.navAreaRectAbove.RightBottom();
					p1 = level.navAreaRectAbove.LeftTop();
				}
			} else {
				if (!isLeft) {
					p2 = level.navAreaRectBelow.LeftTop();
					p1 = level.navAreaRectBelow.RightBottom();
				} else {
					p1 = level.navAreaRectBelow.RightTop();
					p2 = level.navAreaRectBelow.LeftBottom();
				}
			}
			bool inNavArea =
				  (p1.y - p2.y) * position.x + (p2.x - p1.x) * position.y
				+ (p1.x - p2.x) * p1.y + (p2.y - p1.y) * p1.x >= 0;

			if (!inNavArea || now - *level.navigationAreaTime > kNavigationAreaTimeout) {
				// Don't delay opening of submenu if the user had
				// to wait for the navigation area timeout anyway
				menu->_SelectItem(item, inNavArea);

				if (inNavArea) {
					_UpdateNavigationArea(menu, screen, level);
				} else {
					level.navAreaRectAbove = BRect();
					level.navAreaRectBelow = BRect();
				}

				level.selectedTime = now;
				level.navigationAreaTime.reset();
			} else {
				// NewGUI: look again when the timeout is over, also if the
				// mouse rests
				_ScheduleTick(*level.navigationAreaTime + kNavigationAreaTimeout + 1ms);
				return;
			}
		}
	}

	// kOpenSubmenuDelay is 0: the submenu of the selected item opens at once
	// (Haiku: in the loop's next round).
	if (menu->fSelected != nullptr && menu->fSelected->Submenu() != nullptr
		&& !menu->fSelected->Submenu()->_IsShown()) {
		menu->_SelectItem(menu->fSelected, true);
		if (!level.navAreaRectAbove.IsValid() && !level.navAreaRectBelow.IsValid())
			_UpdateNavigationArea(menu, screen, level);
	}
}


void
MenuTracker::_UpdateNavigationArea(BMenu* menu, gfx::Point screen, Level& level)
{
	// The navigation area is a region in which mouse-overs won't select
	// the item under the cursor. This makes it easier to navigate to
	// submenus, as the cursor can be moved to submenu items directly instead
	// of having to move it horizontally into the submenu first.
	if (menu->fSelected == nullptr)
		return;

	const BPoint position(screen.x, screen.y);
	BMenu* submenu = menu->fSelected->Submenu();
	if (submenu != nullptr && submenu->_IsShown()) {
		BRect menuBounds = ScreenBounds(menu);
		BRect submenuBounds = ScreenBounds(submenu);

		if (menuBounds.left < submenuBounds.left) {
			level.navAreaRectAbove.Set(position.x + kNavAreaThreshold,
				submenuBounds.top, menuBounds.right,
				position.y);
			level.navAreaRectBelow.Set(position.x + kNavAreaThreshold,
				position.y, menuBounds.right,
				submenuBounds.bottom);
		} else {
			level.navAreaRectAbove.Set(menuBounds.left,
				submenuBounds.top, position.x - kNavAreaThreshold,
				position.y);
			level.navAreaRectBelow.Set(menuBounds.left,
				position.y, position.x - kNavAreaThreshold,
				submenuBounds.bottom);
		}
	} else {
		level.navAreaRectAbove = BRect();
		level.navAreaRectBelow = BRect();
	}
}


void
MenuTracker::_ScheduleTick(Clock::Time when)
{
	const Clock::Time now = Clock::Default().Now();
	const Clock::Time delay = std::max(when - now, Clock::Time(1ms));
	fTicker = std::make_unique<BMessageRunner>(BMessenger(fBar), BMessage(kMenuTrackerTick),
		delay, 1);
}


} // namespace ngui
