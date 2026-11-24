/*
 * Copyright 2001-2015, Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/MenuBar.cpp (commit b1d497bb69),
 * without archiving and scripting; the tracking is MenuTracker (see
 * MenuBar.h). Keyboard activation (SetKeyMenuBar()) and the focus that
 * Haiku steals in sticky mode come with W3b2b.
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stefano Ceccherini (burton666@libero.it)
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include <ngui/MenuBar.h>

#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/MenuItem.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "MenuTracker.h"


namespace ngui {


BMenuBar::BMenuBar(gfx::Rect frame, std::string_view name, BResizingMode resizingMode,
	BMenuLayout layout, bool resizeToFit)
	:
	BMenu(frame, name, resizingMode, BViewFlag::WillDraw | BViewFlag::FrameEvents
		| BViewFlag::FullUpdateOnResize, layout, resizeToFit)
{
	_InitData();
}


BMenuBar::BMenuBar(std::string_view name, BMenuLayout layout, BViewFlags flags)
	:
	BMenu(gfx::Rect{}, name, kFollowLeftTop, flags | BViewFlag::WillDraw
		| BViewFlag::FrameEvents | BViewFlag::SupportsLayout, layout, false)
{
	_InitData();
}


BMenuBar::~BMenuBar()
{
	fTracker->Stop(nullptr);
}


void
BMenuBar::DetachedFromWindow()
{
	fTracker->Stop(nullptr);
	BMenu::DetachedFromWindow();
}


void
BMenuBar::Draw(DrawContext& context)
{
	(void)_RelayoutIfNeeded();

	gfx::Rect rect = Bounds();
	const gfx::Color base = context.palette.Get(BColorWhich::MenuBackground);
	const ControlLook& look = Look();

	look.DrawBorder(context, rect, base, BBorderStyle::Plain, {}, Border::Bottom);
	look.DrawMenuBarBackground(context, rect, base, {}, fBorders);

	DrawItems(context);
}


void
BMenuBar::MessageReceived(BMessage* message)
{
	if (message->what == kMenuTrackerTick) {
		fTracker->Tick();
		return;
	}
	BMenu::MessageReceived(message);
}


void
BMenuBar::MouseDown(gfx::Point where)
{
	if (fTracker->IsTracking())
		return;

	// NewGUI: the position and the buttons of the event (Haiku: GetMouse())
	const BMessage* current = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	const auto buttons = static_cast<std::uint32_t>(
		current != nullptr ? current->GetInt32("buttons", 1) : 1);
	fTracker->Start(-1, false, false, nullptr, ConvertToScreen(where), buttons);
}


gfx::Size
BMenuBar::MaxSize()
{
	gfx::Size size = BMenu::MaxSize();
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), gfx::Size{kSizeUnlimited, size.h});
}


void
BMenuBar::SetBorder(BMenuBarBorder border)
{
	fBorder = border;
}


BMenuBarBorder
BMenuBar::Border() const
{
	return fBorder;
}


void
BMenuBar::SetBorders(BorderFlags borders)
{
	fBorders = borders;
	Invalidate();
}


BorderFlags
BMenuBar::Borders() const
{
	return fBorders;
}


bool
BMenuBar::IsTracking() const
{
	return fTracker->IsTracking();
}


void
BMenuBar::StartMenuBar(int32 menuIndex, bool sticky, bool showMenu, gfx::Rect* specialRect)
{
	if (fTracker->IsTracking() || Window() == nullptr)
		return;

	const BMessage* current = Window()->CurrentMessage();
	const gfx::Point where = current != nullptr
		? ConvertFromWindow(current->GetPoint("where", {0, 0})) : gfx::Point{0, 0};
	fTracker->Start(menuIndex, sticky, showMenu, specialRect, ConvertToScreen(where), 0);
}


void
BMenuBar::_InitData()
{
	const float fontSize = TextSystem::Default().FontSize(FontRole::Plain);
	float lr = fontSize * 2.0f / 3.0f, tb = fontSize / 6.0f;
	SetItemMargins(lr, tb, lr, tb);

	fBorders = kAllBorders;
	fTracker = std::make_unique<MenuTracker>(this);
}


} // namespace ngui
