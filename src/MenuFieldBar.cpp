/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/BMCPrivate.cpp (commit
 * b1d497bb69), _BMCMenuBar_, without archiving. NewGUI:
 * - _BMCFilter_ (a mouse down on the bar goes to the menu field) is
 *   MouseDown().
 * - Draw() does not resize the bar (Haiku corrected its width there in
 *   frame mode); BMenuField::FrameResized() keeps a fixed bar's width.
 * - FrameResized() invalidates the whole menu field instead of its edges.
 * - No 'TICK' message (no BMessageRunner sends it).
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Marc Flerackers, mflerackers@androme.be
 *		John Scipione, jscipione@gmail.com
 */


#include "MenuFieldBar.h"

#include <ngui/LayoutUtils.h>
#include <ngui/MenuField.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>


namespace ngui {


namespace {


constexpr float kPopUpIndicatorWidth = 13.0f;


} // namespace


MenuFieldBar::MenuFieldBar(gfx::Rect frame, bool fixedSize, BMenuField* menuField)
	:
	BMenuBar(frame, "_mc_mb_", kFollowLeftTop, BMenuLayout::InRow, !fixedSize),
	fMenuField(menuField),
	fFixedSize(fixedSize)
{
	_Init();
}


MenuFieldBar::MenuFieldBar(BMenuField* menuField)
	:
	BMenuBar("_mc_mb_", BMenuLayout::InRow),
	fMenuField(menuField),
	fFixedSize(true)
{
	_Init();
}


void
MenuFieldBar::AttachedToWindow()
{
	fMenuField = static_cast<BMenuField*>(Parent());

	// Don't cause the KeyMenuBar to change by being attached
	BMenuBar* menuBar = Window()->KeyMenuBar();
	BMenuBar::AttachedToWindow();
	Window()->SetKeyMenuBar(menuBar);

	if (fFixedSize && !Flags().Has(BViewFlag::SupportsLayout))
		SetResizingMode({BHorizontalFollow::LeftRight, BVerticalFollow::Top});
}


void
MenuFieldBar::Draw(DrawContext& context)
{
	(void)_RelayoutIfNeeded();

	gfx::Rect rect(Bounds());
	ControlStateFlags flags;
	if (!IsEnabled())
		flags |= ControlState::Disabled;
	if (IsFocus())
		flags |= ControlState::Focused;
	if (fIsInside)
		flags |= ControlState::Hover;

	Look().DrawMenuFieldBackground(context, rect,
		context.palette.Get(BColorWhich::ControlBackground), fShowPopUpMarker, flags);

	DrawItems(context);
}


void
MenuFieldBar::MouseDown(gfx::Point where)
{
	// Haiku's _BMCFilter_: the menu field handles it.
	if (fMenuField != nullptr)
		fMenuField->MouseDown(ConvertToParent(where));
}


void
MenuFieldBar::MouseMoved(gfx::Point where, BMouseTransit transit,
	const BMessage* /*dragMessage*/)
{
	bool inside = transit != BMouseTransit::Exited && Bounds().Contains(where);
	if (inside != fIsInside) {
		fIsInside = inside;
		Invalidate();
	}
}


void
MenuFieldBar::FrameResized(float width, float height)
{
	// we need to take care of cleaning up the parent menu field
	if (Window() != nullptr && fMenuField != nullptr)
		fMenuField->Invalidate();

	BMenuBar::FrameResized(width, height);
}


void
MenuFieldBar::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BMenuBar::MakeFocus(focused);
}


void
MenuFieldBar::SetMaxContentWidth(float width)
{
	float left;
	float right;
	GetItemMargins(&left, nullptr, &right, nullptr);

	BMenuBar::SetMaxContentWidth(width - (left + right));
}


void
MenuFieldBar::SetEnabled(bool enabled)
{
	if (fMenuField != nullptr)
		fMenuField->SetEnabled(enabled);

	BMenuBar::SetEnabled(enabled);
}


gfx::Size
MenuFieldBar::MinSize()
{
	gfx::Size size;
	BMenuBar::GetPreferredSize(&size.w, &size.h);
	if (fShowPopUpMarker) {
		// account for popup indicator + a few pixels margin
		size.w += kPopUpIndicatorWidth;
	}

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


gfx::Size
MenuFieldBar::MaxSize()
{
	// The maximum width of a normal BMenuBar is unlimited, but we want it
	// limited.
	gfx::Size size;
	BMenuBar::GetPreferredSize(&size.w, &size.h);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


void
MenuFieldBar::_Init()
{
	SetFlags(Flags() | BViewFlag::FrameEvents | BViewFlag::FullUpdateOnResize);
	SetBorder(BMenuBarBorder::Contents);

	float left, top, right, bottom;
	GetItemMargins(&left, &top, &right, &bottom);

	// TODO: Fix content location properly. This is just a quick fix to
	// make the BMenuField label and the super-item of the BMenuBar
	// align vertically.
	top++;
	bottom--;

	left = right = Look().DefaultLabelSpacing(TextSystem::Default());
	SetItemMargins(left, top, right + (fShowPopUpMarker ? kPopUpIndicatorWidth : 0), bottom);
}


void
MenuFieldBar::GetAccessibleInfo(AccessibleInfo* info) const
{
	if (info != nullptr)
		info->role = AccessibleRole::None;
}


} // namespace ngui
