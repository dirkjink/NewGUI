/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/AbstractLayoutItem.cpp (commit
 * b1d497bb69), without archiving. PreferredSize() composes the explicit
 * preferred size; Haiku composed the explicit maximum there.
 */


#include <ngui/AbstractLayoutItem.h>


namespace ngui {


BAbstractLayoutItem::BAbstractLayoutItem()
{
}


BAbstractLayoutItem::~BAbstractLayoutItem()
{
}


gfx::Size
BAbstractLayoutItem::MinSize()
{
	return BLayoutUtils::ComposeSize(fMinSize, BaseMinSize());
}


gfx::Size
BAbstractLayoutItem::MaxSize()
{
	return BLayoutUtils::ComposeSize(fMaxSize, BaseMaxSize());
}


gfx::Size
BAbstractLayoutItem::PreferredSize()
{
	return BLayoutUtils::ComposeSize(fPreferredSize, BasePreferredSize());
}


BAlignment
BAbstractLayoutItem::Alignment()
{
	return BLayoutUtils::ComposeAlignment(fAlignment, BaseAlignment());
}


void
BAbstractLayoutItem::SetExplicitMinSize(gfx::Size size)
{
	fMinSize = size;
}


void
BAbstractLayoutItem::SetExplicitMaxSize(gfx::Size size)
{
	fMaxSize = size;
}


void
BAbstractLayoutItem::SetExplicitPreferredSize(gfx::Size size)
{
	fPreferredSize = size;
}


void
BAbstractLayoutItem::SetExplicitAlignment(BAlignment alignment)
{
	fAlignment = alignment;
}


gfx::Size
BAbstractLayoutItem::BaseMinSize()
{
	return {0, 0};
}


gfx::Size
BAbstractLayoutItem::BaseMaxSize()
{
	return {kSizeUnlimited, kSizeUnlimited};
}


gfx::Size
BAbstractLayoutItem::BasePreferredSize()
{
	return {0, 0};
}


BAlignment
BAbstractLayoutItem::BaseAlignment()
{
	return {BHorizontalAlignment::Center, BVerticalAlignment::Middle};
}


} // namespace ngui
