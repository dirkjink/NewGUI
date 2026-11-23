/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ViewLayoutItem.cpp (commit
 * b1d497bb69), without archiving.
 */


#include "ViewLayoutItem.h"

#include <ngui/View.h>


namespace ngui {


BViewLayoutItem::BViewLayoutItem(BView* view)
	:
	fView(view),
	fAncestorsVisible(true)
{
}


BViewLayoutItem::~BViewLayoutItem()
{
}


gfx::Size
BViewLayoutItem::MinSize()
{
	return fView->MinSize();
}


gfx::Size
BViewLayoutItem::MaxSize()
{
	return fView->MaxSize();
}


gfx::Size
BViewLayoutItem::PreferredSize()
{
	return fView->PreferredSize();
}


BAlignment
BViewLayoutItem::Alignment()
{
	return fView->LayoutAlignment();
}


void
BViewLayoutItem::SetExplicitMinSize(gfx::Size size)
{
	fView->SetExplicitMinSize(size);
}


void
BViewLayoutItem::SetExplicitMaxSize(gfx::Size size)
{
	fView->SetExplicitMaxSize(size);
}


void
BViewLayoutItem::SetExplicitPreferredSize(gfx::Size size)
{
	fView->SetExplicitPreferredSize(size);
}


void
BViewLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	fView->SetExplicitAlignment(alignment);
}


bool
BViewLayoutItem::IsVisible()
{
	int showLevel = fView->fHideLevel;
	return showLevel - (fAncestorsVisible ? 0 : 1) <= 0;
}


void
BViewLayoutItem::SetVisible(bool visible)
{
	if (visible != IsVisible()) {
		if (visible)
			fView->Show();
		else
			fView->Hide();
	}
}


gfx::Rect
BViewLayoutItem::Frame()
{
	return fView->Frame();
}


void
BViewLayoutItem::SetFrame(gfx::Rect frame)
{
	fView->MoveTo(frame.LeftTop());
	fView->ResizeTo(frame.Width(), frame.Height());
}


bool
BViewLayoutItem::HasHeightForWidth()
{
	return fView->HasHeightForWidth();
}


void
BViewLayoutItem::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	fView->GetHeightForWidth(width, min, max, preferred);
}


BView*
BViewLayoutItem::View()
{
	return fView;
}


void
BViewLayoutItem::Relayout(bool immediate)
{
	if (immediate)
		fView->Layout(false);
	else
		fView->Relayout();
}


void
BViewLayoutItem::LayoutInvalidated(bool children)
{
	fView->InvalidateLayout(children);
}


void
BViewLayoutItem::AncestorVisibilityChanged(bool shown)
{
	if (fAncestorsVisible == shown)
		return;

	fAncestorsVisible = shown;
	if (shown)
		fView->Show();
	if (!shown)
		fView->Hide();
}


} // namespace ngui
