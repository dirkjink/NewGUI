/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Point.cpp (commit b1d497bb69). Internal to the
 * compatibility layer (specification 17): Haiku's inclusive BRect,
 * never part of the public API.
 *
 * Authors (Haiku):
 *		Frans van Nispen
 *		John Scipione, jscipione@gmail.com
 */


#include "Point.h"

#include <algorithm>

#include <stdio.h>

#include <ngui/SupportDefs.h>
#include "Rect.h"


namespace ngui::compat {


const BPoint B_ORIGIN(0, 0);


void
BPoint::ConstrainTo(BRect rect)
{
	x = std::max(std::min(x, rect.right), rect.left);
	y = std::max(std::min(y, rect.bottom), rect.top);
}


void
BPoint::PrintToStream() const
{
	printf("BPoint(x:%f, y:%f)\n", x, y);
}


BPoint
BPoint::operator-() const
{
	return BPoint(-x, -y);
}


BPoint
BPoint::operator+(const BPoint& other) const
{
	return BPoint(x + other.x, y + other.y);
}


BPoint
BPoint::operator-(const BPoint& other) const
{
	return BPoint(x - other.x, y - other.y);
}


BPoint&
BPoint::operator+=(const BPoint& other)
{
	x += other.x;
	y += other.y;

	return *this;
}


BPoint&
BPoint::operator-=(const BPoint& other)
{
	x -= other.x;
	y -= other.y;

	return *this;
}


bool
BPoint::operator!=(const BPoint& other) const
{
	return x != other.x || y != other.y;
}


bool
BPoint::operator==(const BPoint& other) const
{
	return x == other.x && y == other.y;
}


} // namespace ngui::compat
