/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/PopUpMenu.cpp (commit
 * b1d497bb69), without archiving and scripting. NewGUI (see PopUpMenu.h):
 * Go() starts the tracking on the window's thread and returns at once
 * (Haiku: a tracking thread, and _WaitMenu() unless asynchronous); the
 * tracking is MenuTracker's (_StartTrack()).
 *
 * Authors (Haiku):
 *		Marc Flerackers, mflerackers@androme.be
 *		Stefano Ceccherini, burton666@libero.it
 */


#include <ngui/PopUpMenu.h>

#include <utility>

#include <ngui/MenuItem.h>
#include <ngui/Window.h>

#include "MenuTracker.h"


namespace ngui {


BPopUpMenu::BPopUpMenu(std::string_view name, bool radioMode, bool labelFromMarked,
	BMenuLayout layout)
	:
	BMenu(name, layout),
	fTracker(std::make_unique<MenuTracker>(this))
{
	if (radioMode)
		SetRadioMode(true);

	if (labelFromMarked)
		SetLabelFromMarked(true);
}


BPopUpMenu::~BPopUpMenu()
{
	// NewGUI: give the menu back from its popup before it goes (Haiku
	// waited for the tracking thread).
	fTracker->Stop(nullptr);
}


std::expected<void, Error>
BPopUpMenu::Go(gfx::Point where, GoOptions options, DoneFunction done)
{
	// Get a pointer to the window from which Go() was called
	BWindow* window = BWindow::_DispatchingWindow();
	if (window == nullptr)
		return Failure(ErrorCode::InvalidArgument, "Go() outside a window's message");
	if (fTracker->IsTracking() || window->fMenuTracking != nullptr)
		return Failure(ErrorCode::InvalidArgument, "menus are open");

	// I know, this doesn't look senseful, but don't be fooled,
	// fUseWhere is used in ScreenLocation(), which is a virtual
	// called by BMenu::Track()
	fWhere = where;
	fUseWhere = true;

	const gfx::Rect* clickToOpen = options.clickToOpen.has_value()
		? &*options.clickToOpen : nullptr;
	DoneFunction finished = [this, done = std::move(done)](BMenuItem* item) {
		fUseWhere = false;
		if (done)
			done(item);
	};
	if (!fTracker->StartPopUp(window, options.keepOpen, clickToOpen, window->fLastMouse,
			window->fButtons, options.autoInvoke, std::move(finished))) {
		fUseWhere = false;
		return Failure(ErrorCode::InvalidArgument, "the menu cannot be shown");
	}
	return {};
}


std::expected<void, Error>
BPopUpMenu::Go(std::unique_ptr<BPopUpMenu> menu, gfx::Point where, GoOptions options,
	DoneFunction done)
{
	if (menu == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no menu");

	BWindow* window = BWindow::_DispatchingWindow();
	BPopUpMenu* raw = menu.get();
	// The menu owns itself while it is open; the window destroys it after
	// the message that closed it (Haiku: the tracking thread deleted it).
	auto owner = std::make_shared<std::unique_ptr<BPopUpMenu>>(std::move(menu));
	DoneFunction finished = [owner, window, done = std::move(done)](BMenuItem* item) {
		if (done)
			done(item);
		if (window != nullptr && *owner != nullptr)
			window->_DisposeLater(std::move(*owner));
	};
	// Not opened, the menu goes with \c owner on return.
	return raw->Go(where, options, std::move(finished));
}


bool
BPopUpMenu::IsTracking() const
{
	return fTracker->IsTracking();
}


gfx::Point
BPopUpMenu::ScreenLocation()
{
	// This case is when the BPopUpMenu is standalone
	if (fUseWhere)
		return fWhere;

	// This case is when the BPopUpMenu is inside a BMenuField
	BMenuItem* superItem = Superitem();
	BMenu* superMenu = Supermenu();
	// NewGUI: neither (Haiku: a crash)
	if (superItem == nullptr || superMenu == nullptr)
		return BMenu::ScreenLocation();

	BMenuItem* selectedItem = FindItem(superItem->Label());
	const gfx::Rect superFrame = superItem->Frame();
	gfx::Point point = superMenu->ConvertToScreen({superFrame.x0, superFrame.y0});

	if (selectedItem != nullptr) {
		while (selectedItem->Menu() != this
			&& selectedItem->Menu()->Superitem() != nullptr) {
			selectedItem = selectedItem->Menu()->Superitem();
		}
		point.y -= selectedItem->Frame().y0;
	}

	return point;
}


} // namespace ngui
