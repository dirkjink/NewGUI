/*
 * Copyright 2006-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/LayoutUtils.cpp and
 * src/kits/interface/Alignment.cpp (commit b1d497bb69), only the alignment
 * of rectangles the looks use. Internal to the compatibility layer
 * (specification 17).
 *
 * Authors (Haiku):
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */
#pragma once


#include <cmath>

#include <ngui/InterfaceDefs.h>

#include "InterfaceDefs.h"
#include "Rect.h"
#include "Size.h"


namespace ngui::compat {


// Haiku casts with (int), which truncates; truncf() is the same without
// the conversion back to float.


// BAlignment::RelativeHorizontal() and RelativeVertical(); the unset and
// full values are Haiku's -1 and -2.
inline float
RelativeHorizontal(const BAlignment& alignment)
{
	switch (alignment.horizontal) {
		case B_ALIGN_LEFT:
			return 0;
		case B_ALIGN_RIGHT:
			return 1;
		case B_ALIGN_HORIZONTAL_CENTER:
			return 0.5f;
		case B_ALIGN_HORIZONTAL_UNSET:
			return -1;
		case B_ALIGN_USE_FULL_WIDTH:
			return -2;
	}
	return 0;
}


inline float
RelativeVertical(const BAlignment& alignment)
{
	switch (alignment.vertical) {
		case B_ALIGN_TOP:
			return 0;
		case B_ALIGN_BOTTOM:
			return 1;
		case B_ALIGN_VERTICAL_CENTER:
			return 0.5f;
		case B_ALIGN_VERTICAL_UNSET:
			return -1;
		case B_ALIGN_USE_FULL_HEIGHT:
			return -2;
	}
	return 0;
}


class BLayoutUtils {
public:
	static	BRect				AlignInFrame(BRect frame, BSize maxSize,
									BAlignment alignment);
	static	BRect				AlignOnRect(BRect rect, BSize size,
									BAlignment alignment);
};


inline BRect
BLayoutUtils::AlignInFrame(BRect frame, BSize maxSize, BAlignment alignment)
{
	// align according to the given alignment
	if (maxSize.width < frame.Width()
		&& alignment.horizontal != B_ALIGN_USE_FULL_WIDTH) {
		frame.left += truncf((frame.Width() - maxSize.width)
			* RelativeHorizontal(alignment));
		frame.right = frame.left + maxSize.width;
	}
	if (maxSize.height < frame.Height()
		&& alignment.vertical != B_ALIGN_USE_FULL_HEIGHT) {
		frame.top += truncf((frame.Height() - maxSize.height)
			* RelativeVertical(alignment));
		frame.bottom = frame.top + maxSize.height;
	}

	return frame;
}


inline BRect
BLayoutUtils::AlignOnRect(BRect rect, BSize size, BAlignment alignment)
{
	rect.left += truncf((rect.Width() - size.width)
		* RelativeHorizontal(alignment));
	rect.top += truncf(((rect.Height() - size.height))
		* RelativeVertical(alignment));
	rect.right = rect.left + size.width;
	rect.bottom = rect.top + size.height;

	return rect;
}


} // namespace ngui::compat
