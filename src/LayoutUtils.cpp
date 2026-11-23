/*
 * Copyright 2006-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/LayoutUtils.cpp and Alignment.cpp
 * (commit b1d497bb69). NewGUI counts sizes in pixels (see LayoutUtils.h), so
 * the "+ 1" and "- 1" of AddDistances() and SubtractDistances() are gone;
 * the rectangle functions use the half-open gfx::Rect, whose width is the
 * pixel count, with Haiku's formulas. The layout tree dump is not ported.
 *
 * Authors (Haiku):
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */


#include <ngui/LayoutUtils.h>

#include <algorithm>
#include <cmath>

#include <ngui/BeLook.h>
#include <ngui/ControlLook.h>
#include <ngui/LookRegistry.h>
#include <ngui/TextSystem.h>
#include <ngui/View.h>


namespace ngui {


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b)
{
	if (a >= (int32)kSizeUnlimited - b)
		return (int32)kSizeUnlimited;
	return a + b;
}


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b, int32 c)
{
	return AddSizesInt32(AddSizesInt32(a, b), c);
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b)
{
	float sum = a + b;
	if (sum >= kSizeUnlimited)
		return kSizeUnlimited;

	return sum;
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b, float c)
{
	return AddDistances(AddDistances(a, b), c);
}


// SubtractSizesInt32
int32
BLayoutUtils::SubtractSizesInt32(int32 a, int32 b)
{
	if (a < b)
		return 0;
	return a - b;
}


// SubtractDistances
float
BLayoutUtils::SubtractDistances(float a, float b)
{
	if (a < b)
		return 0;
	return a - b;
}


// FixSizeConstraints
void
BLayoutUtils::FixSizeConstraints(float& min, float& max, float& preferred)
{
	if (max < min)
		max = min;
	if (preferred < min)
		preferred = min;
	else if (preferred > max)
		preferred = max;
}


// FixSizeConstraints
void
BLayoutUtils::FixSizeConstraints(gfx::Size& min, gfx::Size& max,
	gfx::Size& preferred)
{
	FixSizeConstraints(min.w, max.w, preferred.w);
	FixSizeConstraints(min.h, max.h, preferred.h);
}


// IsSet
bool
BLayoutUtils::IsSet(float size)
{
	return size != kSizeUnset;
}


// ComposeSize
gfx::Size
BLayoutUtils::ComposeSize(gfx::Size size, gfx::Size layoutSize)
{
	if (!IsSet(size.w))
		size.w = layoutSize.w;
	if (!IsSet(size.h))
		size.h = layoutSize.h;

	return size;
}


// ComposeAlignment
BAlignment
BLayoutUtils::ComposeAlignment(BAlignment alignment, BAlignment layoutAlignment)
{
	if (alignment.horizontal == BHorizontalAlignment::Unset)
		alignment.horizontal = layoutAlignment.horizontal;
	if (alignment.vertical == BVerticalAlignment::Unset)
		alignment.vertical = layoutAlignment.vertical;

	return alignment;
}


// RelativeHorizontal
float
BLayoutUtils::RelativeHorizontal(BAlignment alignment)
{
	switch (alignment.horizontal) {
		case BHorizontalAlignment::Left:
			return 0;
		case BHorizontalAlignment::Right:
			return 1;
		case BHorizontalAlignment::Center:
			return 0.5f;
		case BHorizontalAlignment::Unset:
			return -1;
		case BHorizontalAlignment::UseFullWidth:
			return -2;
	}
	return 0;
}


// RelativeVertical
float
BLayoutUtils::RelativeVertical(BAlignment alignment)
{
	switch (alignment.vertical) {
		case BVerticalAlignment::Top:
			return 0;
		case BVerticalAlignment::Bottom:
			return 1;
		case BVerticalAlignment::Middle:
			return 0.5f;
		case BVerticalAlignment::Unset:
			return -1;
		case BVerticalAlignment::UseFullHeight:
			return -2;
	}
	return 0;
}


// AlignInFrame
// This method restricts the dimensions of the resulting rectangle according
// to the available size specified by maxSize.
gfx::Rect
BLayoutUtils::AlignInFrame(gfx::Rect frame, gfx::Size maxSize,
	BAlignment alignment)
{
	// align according to the given alignment
	if (maxSize.w < frame.Width()
		&& alignment.horizontal != BHorizontalAlignment::UseFullWidth) {
		frame.x0 += std::trunc((frame.Width() - maxSize.w)
			* RelativeHorizontal(alignment));
		frame.x1 = frame.x0 + maxSize.w;
	}
	if (maxSize.h < frame.Height()
		&& alignment.vertical != BVerticalAlignment::UseFullHeight) {
		frame.y0 += std::trunc((frame.Height() - maxSize.h)
			* RelativeVertical(alignment));
		frame.y1 = frame.y0 + maxSize.h;
	}

	return frame;
}


// AlignInFrame
void
BLayoutUtils::AlignInFrame(BView* view, gfx::Rect frame)
{
	gfx::Size maxSize = view->MaxSize();
	BAlignment alignment = view->LayoutAlignment();
	if (view->HasHeightForWidth()) {
		// The view has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.w < frame.Width()
			&& alignment.horizontal != BHorizontalAlignment::UseFullWidth) {
			const float offset = std::floor((frame.Width() - maxSize.w)
				* RelativeHorizontal(alignment));
			frame.x0 += offset;
			frame.x1 = frame.x0 + maxSize.w;
		}
		alignment.horizontal = BHorizontalAlignment::UseFullWidth;
		float minHeight;
		float maxHeight;
		float preferredHeight;
		view->GetHeightForWidth(frame.Width(), &minHeight, &maxHeight,
			&preferredHeight);
		frame.y1 = frame.y0 + std::max(frame.Height(), minHeight);
		maxSize.h = minHeight;
	}
	frame = AlignInFrame(frame, maxSize, alignment);
	view->MoveTo(frame.LeftTop());
	view->ResizeTo(frame.Width(), frame.Height());
}


// AlignOnRect
// This method, unlike AlignInFrame(), provides the possibility to return
// a rectangle with dimensions greater than the available size.
gfx::Rect
BLayoutUtils::AlignOnRect(gfx::Rect rect, gfx::Size size, BAlignment alignment)
{
	rect.x0 += std::trunc((rect.Width() - size.w) * RelativeHorizontal(alignment));
	rect.y0 += std::trunc((rect.Height() - size.h) * RelativeVertical(alignment));
	rect.x1 = rect.x0 + size.w;
	rect.y1 = rect.y0 + size.h;

	return rect;
}


/*!	Offsets a rectangle's location so that it lies fully in a given rectangular
	frame.

	If the rectangle is too wide/high to fully fit in the frame, its left/top
	edge is offset to 0. The rect's size always remains unchanged.
*/
gfx::Rect
BLayoutUtils::MoveIntoFrame(gfx::Rect rect, gfx::Size frameSize)
{
	gfx::Point leftTop = rect.LeftTop();

	// enforce horizontal limits; favor left edge
	if (rect.x1 > frameSize.w)
		leftTop.x -= rect.x1 - frameSize.w;
	if (leftTop.x < 0)
		leftTop.x = 0;

	// enforce vertical limits; favor top edge
	if (rect.y1 > frameSize.h)
		leftTop.y -= rect.y1 - frameSize.h;
	if (leftTop.y < 0)
		leftTop.y = 0;

	return {leftTop.x, leftTop.y, leftTop.x + rect.Width(), leftTop.y + rect.Height()};
}


// ComposeSpacing
// Haiku's BControlLook::ComposeSpacing() (src/kits/interface/ControlLook.cpp),
// with the look and the font given instead of be_control_look.
float
BLayoutUtils::ComposeSpacing(float spacing, const ControlLook& look,
	const TextSystem& text)
{
	switch ((int)spacing) {
		case (int)kUseDefaultSpacing:
		case (int)kUseItemSpacing:
			return look.DefaultItemSpacing(text);
		case (int)kUseHalfItemSpacing:
			return std::ceil(look.DefaultItemSpacing(text) * 0.5f);
		case (int)kUseWindowSpacing:
			return look.DefaultItemSpacing(text);
		case (int)kUseSmallSpacing:
			return std::ceil(look.DefaultItemSpacing(text) * 0.7f);
		case (int)kUseCornerSpacing:
			return std::ceil(look.DefaultItemSpacing(text) * 1.272f);
		case (int)kUseBigSpacing:
			return std::ceil(look.DefaultItemSpacing(text) * 1.8f);

		case (int)kUseBorderSpacing:
			return std::max(1.0f, std::floor(look.DefaultItemSpacing(text) / 11.0f));
	}

	return spacing;
}


// ComposeSpacing
float
BLayoutUtils::ComposeSpacing(float spacing)
{
	// only the constants need a look
	if (spacing > kUseDefaultSpacing || spacing < kUseBorderSpacing)
		return spacing;

	static const std::shared_ptr<const ControlLook> kFallback = CreateBeLook();
	const std::shared_ptr<const ControlLook> current = LookRegistry::Default().Current();
	return ComposeSpacing(spacing, current ? *current : *kFallback, TextSystem::Default());
}


} // namespace ngui
