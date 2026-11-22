/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/gfx/Rect.h>


namespace ngui::gfx {


/*!	The corner radii of a rounded rectangle, one per corner, in logical
	pixels. A corner is a quarter circle with this radius (specification 6.1).
*/
struct Radii {
			float				tl;	/*!< Top left. */
			float				tr;	/*!< Top right. */
			float				br;	/*!< Bottom right. */
			float				bl;	/*!< Bottom left. */

	static	constexpr Radii		Uniform(float radius);
									/*!< The same \a radius at all four corners. */

			constexpr bool		operator==(const Radii& other) const = default;
									/*!< Exact comparison of all four radii. */
};


/*!	A rectangle with rounded corners (specification 5).

	The radii as given may not fit the rectangle; Normalized() returns the
	radii that are actually drawn. Every drawing operation normalizes.

	An aggregate and trivially copyable, as the DisplayList requires:
	`RRect{rect, radii}`.
*/
struct RRect {
			Rect				rect;	/*!< The rectangle, half-open. */
			Radii				radii;	/*!< The requested corner radii. */

			constexpr bool		operator==(const RRect& other) const = default;
									/*!< Exact comparison of rectangle and radii. */

	[[nodiscard]] RRect			Normalized() const;
									/*!< The radii made to fit, in this order: a negative, NaN
										or infinite radius becomes 0 (as in Skia), an empty
										rectangle has no radii; if two adjacent radii add up to
										more than their edge, all radii shrink by the same
										factor (as in CSS and Skia); finally every radius is
										limited to `min(width, height) / 2`. */
};


constexpr Radii
Radii::Uniform(float radius)
{
	return {radius, radius, radius, radius};
}


} // namespace ngui::gfx
