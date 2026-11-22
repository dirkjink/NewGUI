/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <cmath>

#include <ngui/gfx/Point.h>
#include <ngui/gfx/Size.h>


namespace ngui::gfx {


/*!	An axis-parallel rectangle in logical pixels, **half-open**: it covers
	`[x0, x1) × [y0, y1)` (specification 4.1). A rectangle with `x1 <= x0` or
	`y1 <= y0` is empty, and so is one with a NaN edge.

	Replaces Haiku's BRect, which is inclusive: `BRect(0, 0, 9, 9)` covers
	10 × 10 pixels and corresponds to `Rect{0, 0, 10, 10}`. The conversion
	happens in the internal compatibility layer only (specification 17.1).

	The set operations treat a rectangle as the set of points it covers:
	rectangles that only touch do not intersect, an empty rectangle is
	contained in every rectangle, and the union ignores empty operands.

	An aggregate and trivially copyable, as the DisplayList requires
	(specification 5): `Rect{x0, y0, x1, y1}`.
*/
struct Rect {
			float				x0;	/*!< Left edge, inclusive. */
			float				y0;	/*!< Top edge, inclusive. */
			float				x1;	/*!< Right edge, exclusive. */
			float				y1;	/*!< Bottom edge, exclusive. */

	static	constexpr Rect		FromSize(Point origin, gfx::Size size);
									/*!< The rectangle at \a origin with the extent \a size. */

			constexpr bool		operator==(const Rect& other) const = default;
									/*!< Exact comparison of all four edges. */

			constexpr float		Width() const;
									/*!< `x1 - x0`; zero or negative if empty. */
			constexpr float		Height() const;
									/*!< `y1 - y0`; zero or negative if empty. */
			constexpr gfx::Size	Size() const;
									/*!< Width and height. */
			constexpr Point		LeftTop() const;
									/*!< The inclusive corner `(x0, y0)`. */
			constexpr bool		IsEmpty() const;
									/*!< Covers no point: `!(x0 < x1 && y0 < y1)`. */
			bool				IsFinite() const;
									/*!< No edge is NaN or infinite. */

			constexpr bool		Contains(Point point) const;
									/*!< `x0 <= x < x1` and `y0 <= y < y1`. */
			constexpr bool		Contains(const Rect& other) const;
									/*!< \a other is empty or lies completely inside. */
			constexpr bool		Intersects(const Rect& other) const;
									/*!< Both share at least one point; touching edges do not. */

			constexpr Rect		operator&(const Rect& other) const;
									/*!< The intersection; empty if the two do not intersect. */
			constexpr Rect		operator|(const Rect& other) const;
									/*!< The smallest rectangle containing both; an empty
										operand is ignored. */

			constexpr void		OffsetBy(float dx, float dy);
									/*!< Moves the rectangle by `(dx, dy)`. */
			constexpr void		OffsetBy(Point delta);
									/*!< Moves the rectangle by \a delta. */
			constexpr void		OffsetTo(Point origin);
									/*!< Moves the rectangle so that LeftTop() is \a origin. */
			constexpr void		InsetBy(float dx, float dy);
									/*!< Moves the left and right edges inwards by \a dx, top
										and bottom by \a dy. */

	[[nodiscard]] constexpr Rect OffsetByCopy(float dx, float dy) const;
									/*!< A copy moved by `(dx, dy)`. */
	[[nodiscard]] constexpr Rect OffsetByCopy(Point delta) const;
									/*!< A copy moved by \a delta. */
	[[nodiscard]] constexpr Rect OffsetToCopy(Point origin) const;
									/*!< A copy moved so that LeftTop() is \a origin. */
	[[nodiscard]] constexpr Rect InsetByCopy(float dx, float dy) const;
									/*!< A copy inset by `(dx, dy)`, see InsetBy(). */

	[[nodiscard]] Rect			Rounded() const;
									/*!< Every edge rounded to the nearest whole number,
										`floor(v + 0.5)` (specification 4.4 and 7). */
};


constexpr Rect
Rect::FromSize(Point origin, gfx::Size size)
{
	return {origin.x, origin.y, origin.x + size.w, origin.y + size.h};
}


constexpr float
Rect::Width() const
{
	return x1 - x0;
}


constexpr float
Rect::Height() const
{
	return y1 - y0;
}


constexpr gfx::Size
Rect::Size() const
{
	return {Width(), Height()};
}


constexpr Point
Rect::LeftTop() const
{
	return {x0, y0};
}


constexpr bool
Rect::IsEmpty() const
{
	return !(x0 < x1 && y0 < y1);
}


inline bool
Rect::IsFinite() const
{
	return std::isfinite(x0) && std::isfinite(y0) && std::isfinite(x1)
		&& std::isfinite(y1);
}


constexpr bool
Rect::Contains(Point point) const
{
	return point.x >= x0 && point.x < x1 && point.y >= y0 && point.y < y1;
}


constexpr bool
Rect::Contains(const Rect& other) const
{
	if (other.IsEmpty())
		return true;

	return other.x0 >= x0 && other.x1 <= x1 && other.y0 >= y0 && other.y1 <= y1;
}


constexpr bool
Rect::Intersects(const Rect& other) const
{
	return !(*this & other).IsEmpty();
}


constexpr Rect
Rect::operator&(const Rect& other) const
{
	return {std::max(x0, other.x0), std::max(y0, other.y0),
		std::min(x1, other.x1), std::min(y1, other.y1)};
}


constexpr Rect
Rect::operator|(const Rect& other) const
{
	if (other.IsEmpty())
		return *this;
	if (IsEmpty())
		return other;

	return {std::min(x0, other.x0), std::min(y0, other.y0),
		std::max(x1, other.x1), std::max(y1, other.y1)};
}


constexpr void
Rect::OffsetBy(float dx, float dy)
{
	x0 += dx;
	y0 += dy;
	x1 += dx;
	y1 += dy;
}


constexpr void
Rect::OffsetBy(Point delta)
{
	OffsetBy(delta.x, delta.y);
}


constexpr void
Rect::OffsetTo(Point origin)
{
	OffsetBy(origin.x - x0, origin.y - y0);
}


constexpr void
Rect::InsetBy(float dx, float dy)
{
	x0 += dx;
	y0 += dy;
	x1 -= dx;
	y1 -= dy;
}


constexpr Rect
Rect::OffsetByCopy(float dx, float dy) const
{
	Rect copy = *this;
	copy.OffsetBy(dx, dy);
	return copy;
}


constexpr Rect
Rect::OffsetByCopy(Point delta) const
{
	return OffsetByCopy(delta.x, delta.y);
}


constexpr Rect
Rect::OffsetToCopy(Point origin) const
{
	Rect copy = *this;
	copy.OffsetTo(origin);
	return copy;
}


constexpr Rect
Rect::InsetByCopy(float dx, float dy) const
{
	Rect copy = *this;
	copy.InsetBy(dx, dy);
	return copy;
}


inline Rect
Rect::Rounded() const
{
	return {std::floor(x0 + 0.5f), std::floor(y0 + 0.5f), std::floor(x1 + 0.5f),
		std::floor(y1 + 0.5f)};
}


} // namespace ngui::gfx
