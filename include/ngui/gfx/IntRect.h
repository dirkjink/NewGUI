/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <cmath>
#include <cstdint>

#include <ngui/gfx/Rect.h>


namespace ngui::gfx {


/*!	An axis-parallel rectangle in whole logical pixels, **half-open** like
	Rect: it covers `[x0, x1) × [y0, y1)`. The clip region (Region,
	specification 7) is a list of these.

	Replaces Haiku's clipping_rect, which is inclusive in its public use
	(`{0, 0, 9, 9}` covers 10 × 10 pixels) and exclusive inside BRegion.

	An aggregate and trivially copyable: `IntRect{x0, y0, x1, y1}`.
*/
struct IntRect {
			std::int32_t		x0;	/*!< Left edge, inclusive. */
			std::int32_t		y0;	/*!< Top edge, inclusive. */
			std::int32_t		x1;	/*!< Right edge, exclusive. */
			std::int32_t		y1;	/*!< Bottom edge, exclusive. */

	static	IntRect				RoundedFrom(const Rect& rect);
									/*!< Every edge of \a rect rounded with `floor(v + 0.5)`
										(specification 7), clamped to ±2^30 so that widths do
										not overflow; a rectangle with a NaN edge gives the
										empty `IntRect{0, 0, 0, 0}`. */

			constexpr bool		operator==(const IntRect& other) const = default;
									/*!< Exact comparison of all four edges. */

			constexpr std::int32_t Width() const;
									/*!< `x1 - x0`; zero or negative if empty. */
			constexpr std::int32_t Height() const;
									/*!< `y1 - y0`; zero or negative if empty. */
			constexpr bool		IsEmpty() const;
									/*!< Covers no pixel: `!(x0 < x1 && y0 < y1)`. */
			constexpr Rect		ToRect() const;
									/*!< The same rectangle in float coordinates. */
};


inline IntRect
IntRect::RoundedFrom(const Rect& rect)
{
	if (std::isnan(rect.x0) || std::isnan(rect.y0) || std::isnan(rect.x1)
		|| std::isnan(rect.y1)) {
		return {0, 0, 0, 0};
	}

	static constexpr float kLimit = 1 << 30;
	const auto round = [](float value) {
		return static_cast<std::int32_t>(
			std::clamp(std::floor(value + 0.5f), -kLimit, kLimit));
	};
	return {round(rect.x0), round(rect.y0), round(rect.x1), round(rect.y1)};
}


constexpr std::int32_t
IntRect::Width() const
{
	return x1 - x0;
}


constexpr std::int32_t
IntRect::Height() const
{
	return y1 - y0;
}


constexpr bool
IntRect::IsEmpty() const
{
	return !(x0 < x1 && y0 < y1);
}


constexpr Rect
IntRect::ToRect() const
{
	return {static_cast<float>(x0), static_cast<float>(y0), static_cast<float>(x1),
		static_cast<float>(y1)};
}


} // namespace ngui::gfx
