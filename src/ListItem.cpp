/*
 * Copyright 2001-2013 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ListItem.cpp (commit b1d497bb69),
 * without archiving (see ListItem.h).
 *
 * Authors (Haiku):
 *		Ulrich Wimboeck
 *		Marc Flerackers (mflerackers@androme.be)
 *		Rene Gollent
 */


#include <ngui/ListItem.h>

#include <ngui/TextSystem.h>
#include <ngui/View.h>


namespace ngui {


BListItem::BListItem(uint32 level, bool expanded)
	:
	fLevel(level),
	fExpanded(expanded)
{
}


BListItem::~BListItem()
{
}


float
BListItem::Height() const
{
	return fHeight;
}


float
BListItem::Width() const
{
	return fWidth;
}


bool
BListItem::IsSelected() const
{
	return fSelected;
}


void
BListItem::Select()
{
	fSelected = true;
}


void
BListItem::Deselect()
{
	fSelected = false;
}


void
BListItem::SetEnabled(bool on)
{
	fEnabled = on;
}


bool
BListItem::IsEnabled() const
{
	return fEnabled;
}


void
BListItem::SetHeight(float height)
{
	fHeight = height;
}


void
BListItem::SetWidth(float width)
{
	fWidth = width;
}


void
BListItem::Update(BView* owner, const TextSystem& text)
{
	const FontHeight fh = text.Height(FontRole::Plain);
	SetWidth(owner->Bounds().Width());
	SetHeight(std::ceil(fh.ascent + fh.descent + fh.leading));
}


void
BListItem::SetExpanded(bool expanded)
{
	fExpanded = expanded;
}


bool
BListItem::IsExpanded() const
{
	return fExpanded;
}


uint32
BListItem::OutlineLevel() const
{
	return fLevel;
}


void
BListItem::SetOutlineLevel(uint32 level)
{
	fLevel = level;
}


bool
BListItem::HasSubitems() const
{
	return fHasSubitems;
}


bool
BListItem::IsItemVisible() const
{
	return fVisible;
}


void
BListItem::SetTop(float top)
{
	fTop = top;
}


void
BListItem::SetItemVisible(bool visible)
{
	fVisible = visible;
}


} // namespace ngui
