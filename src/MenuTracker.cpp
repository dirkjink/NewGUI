/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/MenuBar.cpp (_Track(),
 * _StealFocus(), _RestoreFocus()) and Menu.cpp (_Track(),
 * _UpdateStateOpenSelect(), _UpdateNavigationArea(), _UpdateStateClose(),
 * KeyDown(), _SelectNextItem(), _NextItem()), commit b1d497bb69. NewGUI:
 * - One object follows the mouse for the bar and all its open menus, on
 *   the window's thread: each mouse event (in screen coordinates) is
 *   evaluated as Haiku's polling loops would evaluate it, at the level of
 *   the deepest open menu under the mouse, else at the bar's.
 * - The close rules (sticky mode, choosing on release) are applied when
 *   the buttons change, as Haiku's loops did after waiting for a change.
 * - Submenus open at once (Haiku's kOpenSubmenuDelay is 0); while the
 *   mouse rests in a navigation area, a tick re-evaluates it after Haiku's
 *   timeout of one second (on Clock::Default()).
 * - The keys go to the deepest open menu, else to the bar (Haiku's
 *   app_server gave them to the frontmost menu window): BMenu::KeyDown()
 *   is evaluated here. While the keyboard navigates (Haiku's
 *   MENU_STATE_KEY_TO_SUBMENU), the mouse outside the menus does not
 *   deselect; the mouse over an item takes over again.
 * - Enter, Space and a trigger open the submenu of an item that has one,
 *   with its first item selected, instead of invoking the item and
 *   closing (Haiku); a disabled item's trigger does nothing; triggers
 *   ignore the case of ASCII letters.
 * - _NextItem() also reaches the last item when none is selected.
 * - No scrolling menus (Page Up/Down) and no ObscureCursor().
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Marc Flerackers, mflerackers@androme.be
 */


#include "MenuTracker.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

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


// The first character of \a bytes, as Haiku's BUnicodeChar::FromUTF8(), in
// lower case for ASCII letters (the case of the triggers).
uint32
TriggerOf(std::string_view bytes)
{
	if (bytes.empty())
		return 0;
	const auto lead = static_cast<unsigned char>(bytes[0]);
	if (lead < 0x80)
		return static_cast<uint32>((lead >= 'A' && lead <= 'Z') ? lead - 'A' + 'a' : lead);

	std::size_t length = 0;
	uint32 c = 0;
	if ((lead & 0xe0) == 0xc0) {
		length = 2;
		c = lead & 0x1fu;
	} else if ((lead & 0xf0) == 0xe0) {
		length = 3;
		c = lead & 0x0fu;
	} else if ((lead & 0xf8) == 0xf0) {
		length = 4;
		c = lead & 0x07u;
	} else {
		return 0;
	}
	if (bytes.size() < length)
		return 0;
	for (std::size_t i = 1; i < length; i++)
		c = (c << 6) | (static_cast<unsigned char>(bytes[i]) & 0x3fu);
	return c;
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
	const gfx::Rect* specialRect, std::optional<gfx::Point> screen, std::uint32_t buttons)
{
	BWindow* window = fBar->Window();
	if (fTracking || window == nullptr)
		return;

	fTracking = true;
	fLevels.clear();
	fSticky = false;
	fKeyNavigation = false;
	fMouseKnown = screen.has_value();
	fWhere = screen.value_or(gfx::Point{0, 0});
	fButtons = buttons;
	if (specialRect != nullptr)
		fExtraRect = *specialRect;
	else
		fExtraRect.reset();

	window->MenusBeginning();
	window->fMenuTracking = this;
	_SetSticky(sticky);

	if (menuIndex != -1)
		fBar->_SelectItem(fBar->ItemAt(menuIndex), true, false);

	if (fMouseKnown)
		_Evaluate(false);
}


void
MenuTracker::Tick()
{
	if (fTracking && fMouseKnown)
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

	fLevels.clear();
	fExtraRect.reset();
	fKeyNavigation = false;

	if (chosen != nullptr)
		(void)chosen->Invoke();

	_RestoreFocus();
	fSticky = false;

	if (window != nullptr)
		window->MenusEnded();
}


void
MenuTracker::TrackMouse(std::uint32_t /*what*/, gfx::Point screen, std::uint32_t buttons)
{
	if (!fTracking)
		return;

	const bool changed = buttons != fButtons;
	fMouseKnown = true;
	fWhere = screen;
	fButtons = buttons;
	_Evaluate(changed);
}


bool
MenuTracker::TrackKey(std::uint32_t what, std::string_view bytes)
{
	if (!fTracking)
		return false;

	if (what == kKeyDown && !bytes.empty()) {
		BMenu* menu = _DeepestShownMenu();
		_KeyDown(menu != nullptr ? menu : fBar, bytes[0], bytes);
	}

	// The keys belong to the menus while they are open.
	return true;
}


void
MenuTracker::CancelTracking()
{
	Stop(nullptr);
}


void
MenuTracker::ViewDetached(BView* view)
{
	if (view == fPrevFocus)
		fPrevFocus = nullptr;
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
			fKeyNavigation = false;
			_UpdateStateOpenSelect(level, item, fWhere);
		} else if (!fKeyNavigation && level->fSelected != nullptr
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
		if (deepest != nullptr && !overBar && !fKeyNavigation && deepest->fSelected != nullptr
			&& (deepest->fSelected->Submenu() == nullptr
				|| !deepest->fSelected->Submenu()->_IsShown())) {
			deepest->_SelectItem(nullptr);
		}

		if (item != nullptr) {
			fKeyNavigation = false;
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
			_SetSticky(false);
		} else if (fButtons == 0 && !fSticky) {
			if (fExtraRect.has_value() && fExtraRect->Contains(fWhere)) {
				_SetSticky(true);
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
			_SetSticky(false);
		} else if (fButtons == 0 && !fSticky) {
			BMenuItem* selected = fBar->fSelected;
			if ((selected != nullptr && selected->Submenu() == nullptr) || item == nullptr) {
				// clicked on an item without a submenu or clicked and
				// released the mouse button outside the menu bar
				Stop(selected);
				return;
			}
			_SetSticky(true);
		}
	}
}


void
MenuTracker::_SetSticky(bool sticky)
{
	// Haiku's BMenu::_SetStickyMode(), for the whole hierarchy at once.
	if (fSticky == sticky)
		return;

	fSticky = sticky;
	if (sticky) {
		// If we are switching to sticky mode,
		// steal the focus from the current focus view
		// (needed to handle keyboard navigation)
		_StealFocus();
	}
}


void
MenuTracker::_StealFocus()
{
	// We already stole the focus, don't do anything
	if (fFocusStolen)
		return;

	BWindow* window = fBar->Window();
	if (window == nullptr)
		return;

	BView* focusView = window->CurrentFocus();
	fPrevFocus = (focusView != nullptr && focusView != fBar) ? focusView : nullptr;
	fFocusStolen = true;
	fBar->MakeFocus();
}


void
MenuTracker::_RestoreFocus()
{
	if (fBar->Window() != nullptr && fFocusStolen) {
		if (fPrevFocus != nullptr)
			fPrevFocus->MakeFocus();
		else if (fBar->IsFocus())
			fBar->MakeFocus(false);
	}
	fPrevFocus = nullptr;
	fFocusStolen = false;
}


void
MenuTracker::_KeyDown(BMenu* menu, char key, std::string_view bytes)
{
	// Haiku's BMenu::KeyDown() of the menu that has the keys.

	// turn on sticky mode on nav key press
	switch (key) {
		case keys::kUpArrow:
		case keys::kDownArrow:
		case keys::kLeftArrow:
		case keys::kRightArrow:
		case keys::kHome:
		case keys::kEnd:
		case keys::kPageUp:
		case keys::kPageDown:
			_SetSticky(true);
			fKeyNavigation = true;
			break;
		default:
			break;
	}

	switch (key) {
		case keys::kUpArrow:
		case keys::kDownArrow:
			if (menu->Layout() == BMenuLayout::InColumn) {
				(void)_SelectNextItem(menu, menu->fSelected, key == keys::kDownArrow);
			} else if (menu == fBar && menu->fSelected != nullptr
				&& menu->fSelected->Submenu() != nullptr) {
				// NewGUI: Haiku's bar tracked the menu of its selected
				// item (MENU_STATE_KEY_TO_SUBMENU); here it opens.
				menu->_SelectItem(menu->fSelected, true, true);
			}
			break;

		case keys::kLeftArrow:
			if (menu->Layout() == BMenuLayout::InRow) {
				(void)_SelectNextItem(menu, menu->fSelected, false);
			} else if (menu->Superitem() != nullptr) {
				// this case has to be handled a bit specially.
				BMenu* super = menu->Supermenu();
				if (super == fBar) {
					// If we're at the top menu below the menu bar, pass
					// the keypress to the menu bar so we can move to
					// another top level menu.
					_KeyDown(fBar, key, bytes);
				} else {
					// Haiku's MENU_STATE_KEY_LEAVE_SUBMENU in the supermenu:
					// close the submenu, but reselect the item itself for
					// user.
					BMenuItem* temp = super->fSelected;
					super->_SelectItem(nullptr);
					super->_SelectItem(temp, false);
				}
			}
			break;

		case keys::kRightArrow:
			if (menu->Layout() == BMenuLayout::InRow) {
				(void)_SelectNextItem(menu, menu->fSelected, true);
			} else if (menu->fSelected != nullptr && menu->fSelected->Submenu() != nullptr) {
				menu->_SelectItem(menu->fSelected, true, true);
			} else if (menu->Supermenu() == fBar) {
				// if we have no submenu and we're an
				// item in the top menu below the menubar,
				// pass the keypress to the menubar
				// so you can use the keypress to switch menus.
				_KeyDown(fBar, key, bytes);
			}
			break;

		case keys::kEnter:
		case ' ':
			if (menu->fSelected != nullptr)
				(void)_OpenOrChoose(menu, menu->fSelected);
			break;

		case keys::kEscape:
			Stop(nullptr);
			break;

		default:
		{
			if (menu->AreTriggersEnabled()) {
				const uint32 trigger = TriggerOf(bytes);
				for (int32 i = menu->CountItems(); i-- > 0;) {
					BMenuItem* item = menu->ItemAt(i);
					if (item->fTriggerIndex < 0 || item->fTrigger != trigger)
						continue;

					(void)_OpenOrChoose(menu, item);
					break;
				}
			}
			break;
		}
	}
}


bool
MenuTracker::_SelectNextItem(BMenu* menu, BMenuItem* item, bool forward)
{
	if (menu->CountItems() == 0) // cannot select next item in an empty menu
		return false;

	BMenuItem* nextItem = _NextItem(menu, item, forward);
	if (nextItem == nullptr)
		return false;

	menu->_SelectItem(nextItem, menu == fBar);
	return true;
}


BMenuItem*
MenuTracker::_NextItem(const BMenu* menu, BMenuItem* item, bool forward) const
{
	const int32 numItems = menu->CountItems();
	if (numItems == 0)
		return nullptr;

	int32 index = menu->IndexOf(item);
	// NewGUI: one round more without a selected item, so that the last
	// item is reached too
	int32 loopCount = index < 0 ? numItems + 1 : numItems;

	while (--loopCount) {
		// Cycle through menu items in the given direction...
		if (forward)
			index++;
		else
			index--;

		// ... wrap around...
		if (index < 0)
			index = numItems - 1;
		else if (index >= numItems)
			index = 0;

		// ... and return the first suitable item found.
		BMenuItem* nextItem = menu->ItemAt(index);
		if (nextItem->IsEnabled())
			return nextItem;
	}

	// If no other suitable item was found, return NULL.
	return nullptr;
}


bool
MenuTracker::_OpenOrChoose(BMenu* menu, BMenuItem* item)
{
	if (!item->IsEnabled())
		return false;

	if (item->Submenu() != nullptr) {
		// NewGUI: open it, with its first item selected
		menu->_SelectItem(item, true, true);
		return true;
	}

	if (item != menu->fSelected)
		menu->_SelectItem(item, false);
	Stop(item);
	return true;
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
