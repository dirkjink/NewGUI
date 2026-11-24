/*
 * Copyright 2001-2015 Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/MenuField.cpp (commit b1d497bb69),
 * without archiving, scripting and the layout items. NewGUI (see
 * MenuField.h):
 * - Sizes are pixel counts; Haiku's inclusive rectangles become half-open
 *   ones with the same edges.
 * - The tracking runs on the window's thread (MenuTracker), which redraws
 *   the field when it starts and ends (Haiku: _MenuTask() polled it); no
 *   MouseDownFilter, since the tracking gets all mouse events anyway.
 * - The special rectangle given to StartMenuBar() is on the screen.
 * - AllAttached() fits the height only without layout.
 * - Colors from the palette: the label on the panel background (or the
 *   view color), the bar on the control background.
 *
 * Authors (Haiku):
 *		Marc Flerackers, mflerackers@androme.be
 *		Stephan Aßmus, superstippi@gmx.de
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/MenuField.h>

#include <algorithm>
#include <cmath>
#include <utility>

#include <ngui/ControlLook.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Menu.h>
#include <ngui/MenuItem.h>
#include <ngui/Palette.h>
#include <ngui/SeparatorItem.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "MenuFieldBar.h"


namespace ngui {


namespace {


constexpr float kMinMenuBarWidth = 20.0f;
	// found by experimenting on BeOS R5
constexpr float kVMargin = kMenuFieldMargin;


} // namespace


BMenuField::BMenuField(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMenu> menu, BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitObject(label);
	_InitMenuBar(std::move(menu), {0, 0, frame.Width(), frame.Height()}, false);
	_InitObject2();
}


BMenuField::BMenuField(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMenu> menu, bool fixedSize, BResizingMode resizingMode,
	BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitObject(label);
	fFixedSizeMB = fixedSize;
	_InitMenuBar(std::move(menu), {0, 0, frame.Width(), frame.Height()}, fixedSize);
	_InitObject2();
}


BMenuField::BMenuField(std::string_view name, std::string_view label,
	std::unique_ptr<BMenu> menu, BViewFlags flags)
	:
	BView(name, flags | BViewFlag::FrameEvents)
{
	_InitObject(label);
	_InitMenuBar(std::move(menu), {0, 0, 101, 16}, true);
	_InitObject2();
}


BMenuField::BMenuField(std::string_view name, std::string_view label,
	std::unique_ptr<BMenu> menu, bool fixedSize, BViewFlags flags)
	:
	BView(name, flags | BViewFlag::FrameEvents)
{
	_InitObject(label);
	fFixedSizeMB = fixedSize;
	_InitMenuBar(std::move(menu), {0, 0, 101, 16}, fixedSize);
	_InitObject2();
}


BMenuField::BMenuField(std::string_view label, std::unique_ptr<BMenu> menu,
	BViewFlags flags)
	:
	BView(std::string_view{}, flags | BViewFlag::FrameEvents)
{
	_InitObject(label);
	_InitMenuBar(std::move(menu), {0, 0, 101, 16}, true);
	_InitObject2();
}


BMenuField::~BMenuField()
{
}


void
BMenuField::Draw(DrawContext& context)
{
	_DrawLabel(context);
	_DrawMenuBar(context);
}


void
BMenuField::AllAttached()
{
	BView::AllAttached();

	// NewGUI: only without layout, which sizes the field otherwise.
	if (Flags().Has(BViewFlag::SupportsLayout))
		return;

	float width = Bounds().Width();
	if (!fFixedSizeMB && _MenuBarWidth() < kMinMenuBarWidth) {
		// The menu bar is too narrow, resize it to fit the menu items
		BMenuItem* item = fMenuBar->ItemAt(0);
		if (item != nullptr) {
			float right;
			fMenuBar->GetItemMargins(nullptr, nullptr, &right, nullptr);
			width = item->Frame().Width() + kVMargin + _MenuBarOffset() + right;
		}
	}

	ResizeTo(width, fMenuBar->Bounds().Height() + kVMargin * 2);
}


void
BMenuField::MouseDown(gfx::Point /*where*/)
{
	// NewGUI: not when disabled (Haiku: only the keyboard checked)
	if (!IsEnabled())
		return;

	const gfx::Rect bounds = Bounds();
	const gfx::Point origin = ConvertToScreen({bounds.x0, bounds.y0});
	gfx::Rect screen = {origin.x, origin.y, origin.x + bounds.Width(),
		origin.y + bounds.Height()};
	fMenuBar->StartMenuBar(-1, false, true, &screen);
}


void
BMenuField::KeyDown(std::string_view bytes)
{
	if (!bytes.empty()) {
		switch (bytes[0]) {
			case keys::kSpace:
			case keys::kRightArrow:
			case keys::kDownArrow:
			{
				if (!IsEnabled())
					break;

				const gfx::Rect bounds = Bounds();
				const gfx::Point origin = ConvertToScreen({bounds.x0, bounds.y0});
				gfx::Rect screen = {origin.x, origin.y, origin.x + bounds.Width(),
					origin.y + bounds.Height()};
				fMenuBar->StartMenuBar(0, true, true, &screen);
				Invalidate();
				break;
			}
			default:
				break;
		}
	}

	// As Haiku's (which fell through): Tab still navigates.
	BView::KeyDown(bytes);
}


void
BMenuField::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BView::MakeFocus(focused);

	if (Window() != nullptr)
		Invalidate(); // TODO: use fLayoutData.labelWidth
}


void
BMenuField::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


void
BMenuField::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	if (fFixedSizeMB) {
		// we have let the menubar resize itself, but
		// in fixed size mode, the menubar is supposed to
		// be at the right end of the view always. Since
		// the menu bar is in follow left/right mode then,
		// resizing ourselfs might have caused the menubar
		// to be outside now
		fMenuBar->ResizeTo(_MenuBarWidth(), fMenuBar->Frame().Height());
	}

	if (newHeight != fLayoutData.previousHeight && !fLabel.empty()) {
		// The height changed, which means the label has to move and we
		// probably also invalidate a part of the borders around the menu bar.
		// So don't be shy and invalidate the whole thing.
		Invalidate();
	}

	fLayoutData.previousHeight = newHeight;
}


BMenu*
BMenuField::Menu() const
{
	return fMenu;
}


BMenuBar*
BMenuField::MenuBar() const
{
	return fMenuBar;
}


BMenuItem*
BMenuField::MenuItem() const
{
	return fMenuBar->ItemAt(0);
}


void
BMenuField::SetLabel(std::string_view label)
{
	if (fLabel == label)
		return;

	fLabel = label;

	if (Window() != nullptr)
		Invalidate();

	InvalidateLayout();
}


const std::string&
BMenuField::Label() const
{
	return fLabel;
}


void
BMenuField::SetEnabled(bool on)
{
	if (fEnabled == on)
		return;

	fEnabled = on;
	fMenuBar->SetEnabled(on);

	if (Window() != nullptr) {
		fMenuBar->Invalidate();
		Invalidate();
	}
}


bool
BMenuField::IsEnabled() const
{
	return fEnabled;
}


void
BMenuField::SetAlignment(BHorizontalAlignment label)
{
	fAlign = label;
}


BHorizontalAlignment
BMenuField::Alignment() const
{
	return fAlign;
}


void
BMenuField::SetDivider(float position)
{
	position = std::round(position);

	float delta = fDivider - position;
	if (delta == 0.0f)
		return;

	fDivider = position;

	if (Flags().Has(BViewFlag::SupportsLayout)) {
		// We should never get here, since layout support means, we also
		// layout the divider, and don't use this method at all.
		Relayout();
	} else {
		fMenuBar->MoveTo({_MenuBarOffset(), kVMargin});
		if (fFixedSizeMB)
			fMenuBar->ResizeTo(_MenuBarWidth(), fMenuBar->Frame().Height());
		Invalidate();
	}
}


float
BMenuField::Divider() const
{
	return fDivider;
}


void
BMenuField::ShowPopUpMarker()
{
	if (auto* menuBar = dynamic_cast<MenuFieldBar*>(fMenuBar)) {
		menuBar->TogglePopUpMarker(true);
		menuBar->Invalidate();
	}
}


void
BMenuField::HidePopUpMarker()
{
	if (auto* menuBar = dynamic_cast<MenuFieldBar*>(fMenuBar)) {
		menuBar->TogglePopUpMarker(false);
		menuBar->Invalidate();
	}
}


void
BMenuField::ResizeToPreferred()
{
	fMenuBar->ResizeToPreferred();

	BView::ResizeToPreferred();

	Invalidate();
}


void
BMenuField::GetPreferredSize(float* width, float* height)
{
	_ValidateLayoutData();

	if (width != nullptr)
		*width = fLayoutData.min.w;

	if (height != nullptr)
		*height = fLayoutData.min.h;
}


gfx::Size
BMenuField::MinSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData.min);
}


gfx::Size
BMenuField::MaxSize()
{
	_ValidateLayoutData();

	gfx::Size max = fLayoutData.min;
	max.w = kSizeUnlimited;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), max);
}


gfx::Size
BMenuField::PreferredSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), fLayoutData.min);
}


void
BMenuField::LayoutInvalidated(bool /*descendants*/)
{
	fLayoutData.valid = false;
}


void
BMenuField::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return;

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != nullptr) {
		BView::DoLayout();
		return;
	}

	_ValidateLayoutData();

	// validate current size
	gfx::Size size = {Bounds().Width(), Bounds().Height()};
	if (size.w < fLayoutData.min.w)
		size.w = fLayoutData.min.w;

	if (size.h < fLayoutData.min.h)
		size.h = fLayoutData.min.h;

	// divider (NewGUI: no layout items yet)
	float divider = 0;
	if (fLayoutData.labelWidth > 0)
		divider = fLayoutData.labelWidth + Look().DefaultLabelSpacing(TextSystem::Default());

	// menu bar
	gfx::Rect menuBarFrame = {divider + kVMargin, kVMargin, size.w - kVMargin,
		size.h - kVMargin};

	// place the menu bar and set the divider
	BLayoutUtils::AlignInFrame(fMenuBar, menuBarFrame);

	fDivider = divider;

	Invalidate();
}


// #pragma mark - private


void
BMenuField::_InitObject(std::string_view label)
{
	SetLabel(label);

	// NewGUI: Haiku's Frame().Width() is one less than the pixel count
	if (!label.empty())
		fDivider = std::floor((Frame().Width() - 1) / 2.0f);
	else
		fDivider = 0;
}


void
BMenuField::_InitObject2()
{
	if (!fFixedSizeMB) {
		float height;
		fMenuBar->GetPreferredSize(nullptr, &height);
		fMenuBar->ResizeTo(_MenuBarWidth(), height);
	}
}


void
BMenuField::_DrawLabel(DrawContext& context)
{
	_ValidateLayoutData();

	if (fLabel.empty())
		return;

	// Haiku's inclusive rectangle up to the divider
	gfx::Rect rect = Bounds();
	rect.x1 = fDivider + 1;
	if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
		return;

	const ControlLook& look = Look();
	ControlStateFlags flags;
	if (!IsEnabled())
		flags |= ControlState::Disabled;

	gfx::Color base = ViewColor().value_or(context.palette.Get(BColorWhich::PanelBackground));
	gfx::Color textColor;
	if (fMenuBar->IsTracking()) {
		// highlight the background of the label grey (like BeOS R5)
		base = context.palette.Get(BColorWhich::MenuSelectedBackground);
		const gfx::Rect fillRect = {rect.x0, rect.y0 + kVMargin, rect.x1, rect.y1 - kVMargin};
		context.canvas.FillRect(fillRect, gfx::Paint{.color = base});
		textColor = context.palette.Get(BColorWhich::MenuSelectedItemText);
	} else
		textColor = context.palette.Get(BColorWhich::PanelText);

	look.DrawLabel(context, fLabel, rect, base, flags,
		BAlignment{fAlign, BVerticalAlignment::Middle}, &textColor);
}


void
BMenuField::_DrawMenuBar(DrawContext& context)
{
	const gfx::Rect frame = fMenuBar->Frame();
	gfx::Rect rect = {frame.x0 - kVMargin, frame.y0 - kVMargin, frame.x1 + kVMargin,
		frame.y1 + kVMargin};
	if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
		return;

	ControlStateFlags flags;
	if (!IsEnabled())
		flags |= ControlState::Disabled;

	if (IsFocus() && Window() != nullptr && Window()->IsActive())
		flags |= ControlState::Focused;

	Look().DrawMenuFieldFrame(context, rect,
		context.palette.Get(BColorWhich::ControlBackground),
		ViewColor().value_or(context.palette.Get(BColorWhich::PanelBackground)), flags);
}


void
BMenuField::_InitMenuBar(std::unique_ptr<BMenu> menu, gfx::Rect frame, bool fixedSize)
{
	std::unique_ptr<MenuFieldBar> menuBar;
	if (Flags().Has(BViewFlag::SupportsLayout)) {
		menuBar = std::make_unique<MenuFieldBar>(this);
	} else {
		frame.x0 = _MenuBarOffset();
		frame.y0 = kVMargin;
		frame.x1 -= kVMargin;
		frame.y1 -= kVMargin;

		menuBar = std::make_unique<MenuFieldBar>(frame, fixedSize, this);
	}

	if (fixedSize) {
		// align the menu bar in the full available space
		menuBar->SetExplicitAlignment({BHorizontalAlignment::UseFullWidth,
			BVerticalAlignment::Unset});
	} else {
		// align the menu bar left in the available space
		menuBar->SetExplicitAlignment({BHorizontalAlignment::Left,
			BVerticalAlignment::Unset});
	}

	fMenuBar = static_cast<BMenuBar*>(AddChild(std::move(menuBar)));

	_AddMenu(std::move(menu));
}


void
BMenuField::_AddMenu(std::unique_ptr<BMenu> menu)
{
	if (menu == nullptr || fMenuBar == nullptr)
		return;

	fMenu = menu.get();

	BMenuItem* item = nullptr;
	if (!menu->IsRadioMode() || (item = menu->FindMarked()) == nullptr) {
		// find the first enabled non-seperator item
		int32 itemCount = menu->CountItems();
		for (int32 i = 0; i < itemCount; i++) {
			item = menu->ItemAt(i);
			if (item == nullptr || !item->IsEnabled()
				|| dynamic_cast<BSeparatorItem*>(item) != nullptr) {
				item = nullptr;
				continue;
			}
			break;
		}
	}

	if (item == nullptr) {
		(void)fMenuBar->AddItem(std::move(menu));
		return;
	}

	// NewGUI: an item for the menu with the item's label (Haiku: an empty
	// archived copy of the item, uninstalled, with the menu as submenu)
	const std::string label = item->Label();
	auto newItem = std::make_unique<BMenuItem>(std::move(menu));
	newItem->SetLabel(label);
	(void)fMenuBar->AddItem(std::move(newItem));
}


void
BMenuField::_ValidateLayoutData()
{
	if (fLayoutData.valid)
		return;

	const TextSystem& text = TextSystem::Default();
	if (!fLabel.empty()) {
		const FontHeight fh = text.Height(FontRole::Plain);
		fLayoutData.labelWidth = std::ceil(text.StringWidth(fLabel, FontRole::Plain));
		// NewGUI: a pixel count, one more than Haiku's height
		fLayoutData.labelHeight = std::ceil(fh.ascent) + std::ceil(fh.descent) + 1;
	} else {
		fLayoutData.labelWidth = 0;
		fLayoutData.labelHeight = 0;
	}

	// compute the minimal divider
	float divider = 0;
	if (fLayoutData.labelWidth > 0)
		divider = fLayoutData.labelWidth + Look().DefaultLabelSpacing(text);

	// If we shan't do real layout, we let the current divider take influence.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		divider = std::max(divider, fDivider);

	// get the minimal (== preferred) menu bar size
	fLayoutData.menuBarMin = fMenuBar->MinSize();

	// compute our minimal (== preferred) size
	gfx::Size min(fLayoutData.menuBarMin);
	min.w += 2 * kVMargin;
	min.h += 2 * kVMargin;

	if (divider > 0)
		min.w += divider;

	if (fLayoutData.labelHeight > min.h)
		min.h = fLayoutData.labelHeight;

	fLayoutData.min = min;
	fLayoutData.valid = true;

	ResetLayoutInvalidation();
}


float
BMenuField::_MenuBarOffset() const
{
	return std::max(fDivider + kVMargin, kVMargin);
}


float
BMenuField::_MenuBarWidth() const
{
	return Bounds().Width() - (_MenuBarOffset() + kVMargin);
}


} // namespace ngui
