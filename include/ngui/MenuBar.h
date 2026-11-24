/*
 * Copyright 2003-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/ControlLook.h>
#include <ngui/Menu.h>


namespace ngui {


class MenuTracker;


/*!	Which border a menu bar has (Haiku's menu_bar_border); kept for ported
	code, it does not change the drawing (as in Haiku). */
enum class BMenuBarBorder : std::uint8_t {
	Frame,		/*!< B_BORDER_FRAME */
	Contents,	/*!< B_BORDER_CONTENTS */
	EachItem	/*!< B_BORDER_EACH_ITEM */
};


/*!	The menus of a window in a row (Haiku's BMenuBar). Pressing an item
	opens its menu in a popup of the window; the menus follow the mouse:
	release on an item to choose it, or click and release on the bar to keep
	the menus open and click again (sticky mode). A click outside, Escape or
	the window becoming inactive closes them.

	Keyboard: Command+Escape opens the first menu of the window's key menu
	bar (the last one attached, see BWindow::SetKeyMenuBar()). The arrow
	keys move through the items and menus, Enter or Space chooses, a
	trigger chooses its item if triggers are enabled. In sticky mode the
	bar takes the focus and gives it back when the menus close. The
	shortcuts of the items work while the menus are closed (see
	BWindow::AddShortcut()).

	Differences to Haiku: tracking runs on the window's thread, driven by
	events (Haiku: a thread of its own that polls the mouse); the window
	hands it all mouse events and the keys while it runs. Enter, Space and
	triggers open an item's submenu instead of closing the menus.
*/
class BMenuBar : public BMenu {
public:
								BMenuBar(gfx::Rect frame, std::string_view name,
									BResizingMode resizingMode = {
										BHorizontalFollow::LeftRight, BVerticalFollow::Top},
									BMenuLayout layout = BMenuLayout::InRow,
									bool resizeToFit = true);
									/*!< A menu bar at \a frame. */
								BMenuBar(std::string_view name,
									BMenuLayout layout = BMenuLayout::InRow,
									BViewFlags flags = {});
									/*!< A menu bar for a layout. */
								~BMenuBar() override;
									/*!< Closes its menus and destroys it. */

			void				AttachedToWindow() override;
									/*!< Becomes the window's key menu bar. */
			void				DetachedFromWindow() override;
									/*!< Closes its menus; is no longer the key menu bar. */
			void				Draw(DrawContext& context) override;
									/*!< Border, background and items. */
			void				MouseDown(gfx::Point where) override;
									/*!< Starts tracking the menus. */

			gfx::Size			MaxSize() override;
									/*!< Unlimited width. */

	virtual	void				SetBorder(BMenuBarBorder border);
									/*!< Kept for ported code. */
			BMenuBarBorder		Border() const;
									/*!< Kept for ported code. */
			void				SetBorders(BorderFlags borders);
									/*!< The sides of the bar's background frame. */
			BorderFlags			Borders() const;
									/*!< The sides of the bar's background frame. */

			bool				IsTracking() const;
									/*!< Whether its menus are open. */

private:
	friend class MenuTracker;
	friend class BWindow;
	friend class BMenuField;

			void				_InitData();
			void				StartMenuBar(int32 menuIndex, bool sticky = true,
									bool showMenu = false, gfx::Rect* specialRect = nullptr);

			BMenuBarBorder		fBorder = BMenuBarBorder::Frame;
			BorderFlags			fBorders = kAllBorders;
			std::unique_ptr<MenuTracker> fTracker;
};


} // namespace ngui
