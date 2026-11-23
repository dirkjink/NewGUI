/*
 * Copyright 2006-2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/LayoutUtils.cpp (commit b1d497bb69),
 * the size arithmetic. NewGUI counts sizes in pixels (see LayoutUtils.h), so
 * the "+ 1" and "- 1" of AddDistances() and SubtractDistances() are gone.
 *
 * Authors (Haiku):
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */


#include <ngui/LayoutUtils.h>


namespace ngui {


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b)
{
	if (a >= (int32)kSizeUnlimited - b)
		return (int32)kSizeUnlimited;
	return a + b;
}


// AddSizesInt32
int32
BLayoutUtils::AddSizesInt32(int32 a, int32 b, int32 c)
{
	return AddSizesInt32(AddSizesInt32(a, b), c);
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b)
{
	float sum = a + b;
	if (sum >= kSizeUnlimited)
		return kSizeUnlimited;

	return sum;
}


// AddDistances
float
BLayoutUtils::AddDistances(float a, float b, float c)
{
	return AddDistances(AddDistances(a, b), c);
}


// SubtractSizesInt32
int32
BLayoutUtils::SubtractSizesInt32(int32 a, int32 b)
{
	if (a < b)
		return 0;
	return a - b;
}


// SubtractDistances
float
BLayoutUtils::SubtractDistances(float a, float b)
{
	if (a < b)
		return 0;
	return a - b;
}


// FixSizeConstraints
void
BLayoutUtils::FixSizeConstraints(float& min, float& max, float& preferred)
{
	if (max < min)
		max = min;
	if (preferred < min)
		preferred = min;
	else if (preferred > max)
		preferred = max;
}


} // namespace ngui
