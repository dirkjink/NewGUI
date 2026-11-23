/*
 * Copyright 2001-2009, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/os/interface/Point.h (commit b1d497bb69). Internal to the
 * compatibility layer (specification 17): Haiku's inclusive BRect,
 * never part of the public API.
 */
#pragma once


#include <ngui/SupportDefs.h>


namespace ngui::compat {


class BRect;


class BPoint {
public:
			float				x;
			float				y;

								BPoint();
								BPoint(float x, float y);
								BPoint(const BPoint& p);

			BPoint&				operator=(const BPoint& other);
			void				Set(float x, float y);

			void				ConstrainTo(BRect rect);
			void				PrintToStream() const;

			BPoint				operator-() const;
			BPoint				operator+(const BPoint& other) const;
			BPoint				operator-(const BPoint& other) const;
			BPoint&				operator+=(const BPoint& other);
			BPoint&				operator-=(const BPoint& other);

			bool				operator!=(const BPoint& other) const;
			bool				operator==(const BPoint& other) const;
};


extern const BPoint B_ORIGIN;
	// returns (0, 0)


inline
BPoint::BPoint()
	:
	x(0.0f),
	y(0.0f)
{
}


inline
BPoint::BPoint(float x, float y)
	:
	x(x),
	y(y)
{
}


inline
BPoint::BPoint(const BPoint& other)
	:
	x(other.x),
	y(other.y)
{
}


inline BPoint&
BPoint::operator=(const BPoint& other)
{
	x = other.x;
	y = other.y;
	return *this;
}


inline void
BPoint::Set(float newX, float newY)
{
	// Parameters renamed: MSVC warns that they hide the members (C4458).
	x = newX;
	y = newY;
}


} // namespace ngui::compat
