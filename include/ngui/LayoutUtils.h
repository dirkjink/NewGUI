/*
 * Copyright 2006-2014, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/SupportDefs.h>


namespace ngui {


/*!	A size that is not set, as Haiku's B_SIZE_UNSET. */
inline constexpr float kSizeUnset = -2;

/*!	A size without limit, as Haiku's B_SIZE_UNLIMITED: 2^30 pixels. Sums
	that reach it stay at it. */
inline constexpr float kSizeUnlimited = 1024 * 1024 * 1024;


/*!	Size arithmetic of the layout engine (module G10), as Haiku's
	BLayoutUtils.

	Sizes and distances are **pixel counts**, as the widths of gfx::Rect: a
	minimum width of 80 is 80 pixels. Haiku counts one less (a width of 79
	is 80 pixels, because its rectangles include their right edge) and adds
	or subtracts 1 when it combines sizes; NewGUI's layout engine is ported
	without these corrections, everything else follows Haiku.
*/
class BLayoutUtils {
public:
	static	int32				AddSizesInt32(int32 a, int32 b);
									/*!< \a a + \a b, at most kSizeUnlimited. */
	static	int32				AddSizesInt32(int32 a, int32 b, int32 c);
									/*!< \a a + \a b + \a c, at most kSizeUnlimited. */
	static	float				AddDistances(float a, float b);
									/*!< \a a + \a b, at most kSizeUnlimited. */
	static	float				AddDistances(float a, float b, float c);
									/*!< \a a + \a b + \a c, at most kSizeUnlimited. */
	static	int32				SubtractSizesInt32(int32 a, int32 b);
									/*!< \a a - \a b, at least 0. */
	static	float				SubtractDistances(float a, float b);
									/*!< \a a - \a b, at least 0. */
	static	void				FixSizeConstraints(float& min, float& max,
									float& preferred);
									/*!< Raises \a max to \a min and puts \a preferred
										between them. */
};


} // namespace ngui
