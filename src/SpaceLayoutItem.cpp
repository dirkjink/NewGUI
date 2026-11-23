/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/SpaceLayoutItem.cpp (commit
 * b1d497bb69), without archiving and without ComposeSpacing() (see
 * SpaceLayoutItem.h). Sizes are pixel counts: Haiku's -1 (no pixel) is 0.
 */


#include <ngui/SpaceLayoutItem.h>

#include <ngui/LayoutUtils.h>


namespace ngui {


BSpaceLayoutItem::BSpaceLayoutItem(gfx::Size minSize, gfx::Size maxSize,
	gfx::Size preferredSize, BAlignment alignment)
	:
	fMinSize(minSize),
	fMaxSize(maxSize),
	fPreferredSize(preferredSize),
	fAlignment(alignment)
{
}


BSpaceLayoutItem::~BSpaceLayoutItem()
{
}


std::unique_ptr<BSpaceLayoutItem>
BSpaceLayoutItem::CreateGlue()
{
	return std::make_unique<BSpaceLayoutItem>(
		gfx::Size{0, 0},
		gfx::Size{kSizeUnlimited, kSizeUnlimited},
		gfx::Size{0, 0},
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle});
}


std::unique_ptr<BSpaceLayoutItem>
BSpaceLayoutItem::CreateHorizontalStrut(float width)
{
	return std::make_unique<BSpaceLayoutItem>(
		gfx::Size{width, 0},
		gfx::Size{width, kSizeUnlimited},
		gfx::Size{width, 0},
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle});
}


std::unique_ptr<BSpaceLayoutItem>
BSpaceLayoutItem::CreateVerticalStrut(float height)
{
	return std::make_unique<BSpaceLayoutItem>(
		gfx::Size{0, height},
		gfx::Size{kSizeUnlimited, height},
		gfx::Size{0, height},
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle});
}


gfx::Size
BSpaceLayoutItem::MinSize()
{
	return fMinSize;
}


gfx::Size
BSpaceLayoutItem::MaxSize()
{
	return fMaxSize;
}


gfx::Size
BSpaceLayoutItem::PreferredSize()
{
	return fPreferredSize;
}


BAlignment
BSpaceLayoutItem::Alignment()
{
	return fAlignment;
}


void
BSpaceLayoutItem::SetExplicitMinSize(gfx::Size size)
{
	if (BLayoutUtils::IsSet(size.w))
		fMinSize.w = size.w;
	if (BLayoutUtils::IsSet(size.h))
		fMinSize.h = size.h;

	InvalidateLayout();
}


void
BSpaceLayoutItem::SetExplicitMaxSize(gfx::Size size)
{
	if (BLayoutUtils::IsSet(size.w))
		fMaxSize.w = size.w;
	if (BLayoutUtils::IsSet(size.h))
		fMaxSize.h = size.h;

	InvalidateLayout();
}


void
BSpaceLayoutItem::SetExplicitPreferredSize(gfx::Size size)
{
	if (BLayoutUtils::IsSet(size.w))
		fPreferredSize.w = size.w;
	if (BLayoutUtils::IsSet(size.h))
		fPreferredSize.h = size.h;

	InvalidateLayout();
}


void
BSpaceLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	if (alignment.horizontal != BHorizontalAlignment::Unset)
		fAlignment.horizontal = alignment.horizontal;
	if (alignment.vertical != BVerticalAlignment::Unset)
		fAlignment.vertical = alignment.vertical;

	InvalidateLayout();
}


bool
BSpaceLayoutItem::IsVisible()
{
	return fVisible;
}


void
BSpaceLayoutItem::SetVisible(bool visible)
{
	fVisible = visible;
}


gfx::Rect
BSpaceLayoutItem::Frame()
{
	return fFrame;
}


void
BSpaceLayoutItem::SetFrame(gfx::Rect frame)
{
	fFrame = frame;
}


} // namespace ngui
