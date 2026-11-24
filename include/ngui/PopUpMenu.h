/*
 * Copyright 2003-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include <ngui/Error.h>
#include <ngui/Menu.h>


namespace ngui {


class MenuTracker;


/*!	How BPopUpMenu::Go() tracks the menu (Haiku's arguments of Go()). */
struct BPopUpOptions {
	bool					autoInvoke = false;
								/*!< Invoke the chosen item (Haiku's autoInvoke). */
	bool					keepOpen = false;
								/*!< Open in sticky mode, until a click (Haiku's
									keepOpen). */
	std::optional<gfx::Rect> clickToOpen = std::nullopt;
								/*!< On the screen: releasing the mouse in it keeps the
									menu open (Haiku's openRect). */
};


/*!	A menu that opens on its own, e.g. as a context menu, or as the menu of
	a BMenuField (Haiku's BPopUpMenu). By default in radio mode, its
	superitem showing the marked item's label.

	Go() opens it at a point of the screen, in a popup of the window whose
	message is being handled, and returns at once; the menu follows the
	mouse and the keyboard as a menu bar's menus do. Pressed and released
	within the click time, it stays open until the next click (sticky).

	Differences to Haiku: Go() is asynchronous only (Haiku waits for the
	choice and returns the item, unless asked to be asynchronous): the
	choice arrives through the \a done callback and, with \a autoInvoke,
	through the item's message. Haiku's SetAsyncAutoDestruct() is the
	overload that takes ownership. Items without a target send to the window
	Go() was called in (Haiku: to the menu's own window). The menu's item
	shortcuts are not installed in the window.
*/
class BPopUpMenu : public BMenu {
public:
	/*!	How Go() tracks the menu. */
	using GoOptions = BPopUpOptions;

	/*!	Called when the menu closes, with the chosen item or nullptr. The
		item is valid during the call. */
	using DoneFunction = std::function<void(BMenuItem* item)>;

								BPopUpMenu(std::string_view name, bool radioMode = true,
									bool labelFromMarked = true,
									BMenuLayout layout = BMenuLayout::InColumn);
									/*!< A pop-up menu named \a name. */
								~BPopUpMenu() override;
									/*!< Closes it first if it is open. */

			std::expected<void, Error> Go(gfx::Point where, GoOptions options = {},
									DoneFunction done = {});
									/*!< Opens the menu with its left top corner at \a where
										(on the screen, moved onto it if needed) and tracks
										it; returns at once. Call it in the thread of the
										window it opens in, while that window handles a
										message (e.g. in MouseDown()). Fails if there is no
										such window, if menus of that window are open, or if
										this menu is. */
	static	std::expected<void, Error> Go(std::unique_ptr<BPopUpMenu> menu, gfx::Point where,
									GoOptions options = {}, DoneFunction done = {});
									/*!< As above, owning \a menu: it is destroyed after it
										closed and \a done was called (Haiku's
										SetAsyncAutoDestruct()). */

			bool				IsTracking() const;
									/*!< Whether it is open after Go(). */

protected:
			gfx::Point			ScreenLocation() override;
									/*!< Where Go() opens it; in a BMenuField, so that the
										marked item lies over the field. */

private:
			gfx::Point			fWhere = {0, 0};
			bool				fUseWhere = false;
			std::unique_ptr<MenuTracker> fTracker;
};


} // namespace ngui
