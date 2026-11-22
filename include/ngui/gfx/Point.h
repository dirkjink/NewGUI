/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>


namespace ngui::gfx {


/*!	A point in logical pixels. The origin is the top left corner, the y axis
	points down (specification 4.1). Replaces Haiku's BPoint.

	An aggregate and trivially copyable, as the DisplayList requires
	(specification 5): `Point{x, y}`.
*/
struct Point {
			float				x;	/*!< Horizontal coordinate. */
			float				y;	/*!< Vertical coordinate. */

			constexpr bool		operator==(const Point& other) const = default;
									/*!< Exact comparison of both coordinates. */

			constexpr Point		operator+(const Point& other) const;
									/*!< Component-wise sum. */
			constexpr Point		operator-(const Point& other) const;
									/*!< Component-wise difference. */
			constexpr Point		operator-() const;
									/*!< Both coordinates negated. */
			constexpr Point&	operator+=(const Point& other);
									/*!< Adds \a other component-wise. */
			constexpr Point&	operator-=(const Point& other);
									/*!< Subtracts \a other component-wise. */

			bool				IsFinite() const;
									/*!< Neither coordinate is NaN or infinite. */
};


constexpr Point
Point::operator+(const Point& other) const
{
	return {x + other.x, y + other.y};
}


constexpr Point
Point::operator-(const Point& other) const
{
	return {x - other.x, y - other.y};
}


constexpr Point
Point::operator-() const
{
	return {-x, -y};
}


constexpr Point&
Point::operator+=(const Point& other)
{
	x += other.x;
	y += other.y;
	return *this;
}


constexpr Point&
Point::operator-=(const Point& other)
{
	x -= other.x;
	y -= other.y;
	return *this;
}


inline bool
Point::IsFinite() const
{
	return std::isfinite(x) && std::isfinite(y);
}


} // namespace ngui::gfx
