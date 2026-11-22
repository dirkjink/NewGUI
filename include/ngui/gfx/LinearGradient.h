/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <array>
#include <cstdint>
#include <expected>
#include <span>

#include <ngui/Error.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/Point.h>


namespace ngui::gfx {


/*!	One stop of a LinearGradient (specification 8). */
struct GradientStop {
			float				offset;	/*!< Position in `[0, 1]` along the gradient. */
			Color				color;	/*!< Color at this position. */

			constexpr bool		operator==(const GradientStop& other) const = default;
									/*!< Same offset and color. */
};


/*!	The offset of a Haiku gradient stop, given in `[0, 255]`, as an offset in
	`[0, 1]` (specification 8). The stops seen in Haiku's looks are 0 and
	255, and 0, 132, 136, 255 for the glossy look.
*/
constexpr float
HaikuGradientOffset(std::uint8_t offset)
{
	return offset / 255.0f;
}


/*!	A linear gradient with 2 to 4 stops, the only gradient of the core
	profile (specification 8). Replaces Haiku's BGradientLinear.

	The color at a point `q` follows from `t = clamp(dot(q - p0, p1 - p0) /
	|p1 - p0|², 0, 1)` and piecewise linear interpolation between the stops,
	**not** premultiplied, in sRGB-encoded space. Before the first stop the
	color is the first stop's, after the last stop the last stop's; two stops
	at the same offset make a hard edge, and exactly at the edge the later
	stop's color applies (as in CSS). If `p0 == p1`, the color is the last
	stop's.

	Create() checks the stops. The aggregate itself accepts anything, so that
	the DisplayList can hold it as plain data; IsValid() tells.
*/
struct LinearGradient {
			Point				p0;		/*!< Start, in Canvas coordinates before translation. */
			Point				p1;		/*!< End, in Canvas coordinates before translation. */
			std::array<GradientStop, 4> stops;
									/*!< The stops; only the first #count are used. */
			std::uint8_t		count;	/*!< Number of stops, 2 to 4. */

	static	std::expected<LinearGradient, Error> Create(Point p0, Point p1,
									std::span<const GradientStop> stops);
									/*!< The gradient from \a p0 to \a p1 with \a stops;
										ErrorCode::InvalidArgument unless IsValid() would hold. */

			bool				operator==(const LinearGradient& other) const;
									/*!< Same points and the same used stops. */

			std::span<const GradientStop> Stops() const;
									/*!< The used stops. */
			bool				IsValid() const;
									/*!< 2 to 4 stops with finite, ascending offsets in
										`[0, 1]` and valid colors, and finite points. */
			Color				ColorAt(Point point) const;
									/*!< The color at \a point, see above. Requires IsValid(). */
};


} // namespace ngui::gfx
