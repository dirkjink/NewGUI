/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Point.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Point;


static_assert(std::is_aggregate_v<Point>);
static_assert(std::is_trivially_copyable_v<Point>);


TEST(PointTest, ArithmeticIsComponentWise)
{
	// What is tested: Sum, difference, negation and the compound assignments.
	// Success criteria: Each operation works on x and y independently.
	// Why it matters: Translations in the Canvas and offsets in the layout are
	// written with these operators.
	//
	// Given
	constexpr Point a = {1, 2};
	constexpr Point b = {10, 20};

	// When / Then
	static_assert(a + b == Point{11, 22});
	static_assert(b - a == Point{9, 18});
	static_assert(-a == Point{-1, -2});
	Point c = a;
	c += b;
	EXPECT_EQ(c, (Point{11, 22}));
	c -= b;
	EXPECT_EQ(c, a);
}


TEST(PointTest, IsFiniteRejectsNanAndInfinity)
{
	// What is tested: IsFinite() for ordinary, NaN and infinite coordinates.
	// Success criteria: Only a point with two finite coordinates is finite.
	// Why it matters: The Canvas drops operations with NaN input
	// (specification 6.2), and this is the check it uses.
	//
	// When / Then
	EXPECT_TRUE((Point{1, -1}).IsFinite());
	EXPECT_FALSE((Point{std::numeric_limits<float>::quiet_NaN(), 0}).IsFinite());
	EXPECT_FALSE((Point{0, std::numeric_limits<float>::infinity()}).IsFinite());
}


} // namespace
