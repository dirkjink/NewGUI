/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>


namespace ngui::gfx {


/*!	A width and a height in logical pixels.

	Unlike Haiku's BSize, which stores the inclusive width (a one pixel wide
	BRect has the width 0), Size is the extent of a half-open rectangle: one
	pixel is `Size{1, 1}` (specification 4.1).
*/
struct Size {
			float				w;	/*!< Width. */
			float				h;	/*!< Height. */

			constexpr bool		operator==(const Size& other) const = default;
									/*!< Exact comparison of both extents. */

			constexpr bool		IsEmpty() const;
									/*!< Not both extents are positive (NaN counts as empty). */
			bool				IsFinite() const;
									/*!< Neither extent is NaN or infinite. */
};


constexpr bool
Size::IsEmpty() const
{
	return !(w > 0 && h > 0);
}


inline bool
Size::IsFinite() const
{
	return std::isfinite(w) && std::isfinite(h);
}


} // namespace ngui::gfx
