/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Rect.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Point;
using ngui::gfx::Rect;
using ngui::gfx::Size;


static_assert(std::is_aggregate_v<Rect>);
static_assert(std::is_trivially_copyable_v<Rect>);
static_assert(std::is_standard_layout_v<Rect>);


TEST(RectTest, ExtentIsHalfOpen)
{
	// What is tested: Width, height and size of a rectangle against the
	// half-open model, including the rectangle Haiku writes as BRect(0, 0, 9, 9).
	// Success criteria: Rect{0, 0, 10, 10} is 10 × 10, FromSize() builds the
	// same rectangle, and a one pixel rectangle is Rect{3, 4, 4, 5}.
	// Why it matters: The half-open model is the main semantic break from
	// Haiku (specification 4.1); every off-by-one in ported code shows up here
	// first.
	//
	// Given
	constexpr Rect rect = {0, 0, 10, 10};

	// When / Then
	static_assert(rect.Width() == 10 && rect.Height() == 10);
	EXPECT_EQ(rect.Size(), (Size{10, 10}));
	EXPECT_EQ(Rect::FromSize({0, 0}, {10, 10}), rect);
	EXPECT_EQ(Rect::FromSize({3, 4}, {1, 1}), (Rect{3, 4, 4, 5}));
	EXPECT_EQ(rect.LeftTop(), (Point{0, 0}));
}


TEST(RectTest, EmptinessFollowsTheEdges)
{
	// What is tested: IsEmpty() for proper, zero-width, inverted and NaN
	// rectangles.
	// Success criteria: Only a rectangle with x0 < x1 and y0 < y1 is non-empty;
	// a NaN edge makes it empty.
	// Why it matters: The Canvas drops operations on empty rectangles
	// (specification 4.1); a NaN that counted as non-empty would reach the
	// backends.
	//
	// When / Then
	const float nan = std::numeric_limits<float>::quiet_NaN();
	EXPECT_FALSE((Rect{0, 0, 1, 1}).IsEmpty());
	EXPECT_TRUE((Rect{0, 0, 0, 1}).IsEmpty());
	EXPECT_TRUE((Rect{0, 0, 1, 0}).IsEmpty());
	EXPECT_TRUE((Rect{5, 5, 1, 1}).IsEmpty());
	EXPECT_TRUE((Rect{0, 0, nan, 1}).IsEmpty());
	EXPECT_FALSE((Rect{0, 0, nan, 1}).IsFinite());
	EXPECT_TRUE((Rect{0, 0, 1, 1}).IsFinite());
}


TEST(RectTest, ContainsPointExcludesFarEdges)
{
	// What is tested: Contains(Point) at the four edges of a rectangle.
	// Success criteria: Points on the left and top edges are inside, points on
	// the right and bottom edges are outside.
	// Why it matters: This is the definition of half-open; with inclusive far
	// edges two adjacent rectangles would both claim the shared pixel row.
	//
	// Given
	constexpr Rect rect = {0, 0, 10, 10};

	// When / Then
	static_assert(rect.Contains(Point{0, 0}));
	static_assert(rect.Contains(Point{9.999f, 9.999f}));
	static_assert(!rect.Contains(Point{10, 5}));
	static_assert(!rect.Contains(Point{5, 10}));
	static_assert(!rect.Contains(Point{-0.001f, 5}));
	static_assert(!(Rect{0, 0, 0, 0}).Contains(Point{0, 0}));
}


TEST(RectTest, SetOperationsTreatTouchingAsDisjoint)
{
	// What is tested: Intersects(), operator& and Contains(Rect) for
	// overlapping, touching and nested rectangles.
	// Success criteria: Overlapping rectangles intersect in their common part;
	// rectangles sharing only an edge do not intersect and their intersection is
	// empty; an empty rectangle is contained in every rectangle.
	// Why it matters: The clip region and the damage tracking are built from
	// these operations (specification 7); touching rectangles must tile the
	// plane without overlap.
	//
	// Given
	constexpr Rect left = {0, 0, 10, 10};
	constexpr Rect overlapping = {5, 5, 15, 15};
	constexpr Rect touching = {10, 0, 20, 10};

	// When / Then
	static_assert((left & overlapping) == Rect{5, 5, 10, 10});
	static_assert(left.Intersects(overlapping) && overlapping.Intersects(left));
	static_assert(!left.Intersects(touching));
	static_assert((left & touching).IsEmpty());
	static_assert(left.Contains(Rect{2, 2, 8, 8}));
	static_assert(left.Contains(left));
	static_assert(!left.Contains(overlapping));
	static_assert(left.Contains(Rect{50, 50, 50, 50}));
	static_assert(!(Rect{0, 0, 0, 0}).Intersects(left));
}


TEST(RectTest, UnionIgnoresEmptyOperands)
{
	// What is tested: operator| with two proper rectangles and with an empty
	// operand far away from the other.
	// Success criteria: The union of two rectangles is their bounding box; an
	// empty operand leaves the other unchanged, wherever its coordinates are.
	// Why it matters: Haiku's BRect::operator| would stretch the result to the
	// coordinates of an invalid rectangle; accumulating damage from an empty
	// rectangle must not grow the area to repaint.
	//
	// Given
	constexpr Rect a = {0, 0, 10, 10};
	constexpr Rect b = {20, 5, 30, 8};
	constexpr Rect emptyFarAway = {-100, -100, -200, -200};

	// When / Then
	static_assert((a | b) == Rect{0, 0, 30, 10});
	static_assert((a | emptyFarAway) == a);
	static_assert((emptyFarAway | a) == a);
}


TEST(RectTest, OffsetAndInsetKeepHaikuMeaning)
{
	// What is tested: OffsetBy, OffsetTo and InsetBy, in place and as the
	// *Copy variants.
	// Success criteria: Offsetting moves all edges, OffsetTo moves the left top
	// corner to the target, InsetBy shrinks by the inset on each side; the
	// *Copy variants leave the original unchanged.
	// Why it matters: The names are taken over from BRect because the meaning
	// is the same (rule K5); ported code relies on in place versus copy.
	//
	// Given
	Rect rect = {0, 0, 10, 10};

	// When
	rect.OffsetBy(5, 5);
	rect.InsetBy(1, 2);

	// Then
	EXPECT_EQ(rect, (Rect{6, 7, 14, 13}));
	rect.OffsetTo({0, 0});
	EXPECT_EQ(rect, (Rect{0, 0, 8, 6}));
	static_assert((Rect{0, 0, 10, 10}).OffsetByCopy(Point{1, 2}) == Rect{1, 2, 11, 12});
	static_assert((Rect{4, 4, 10, 10}).OffsetToCopy({0, 0}) == Rect{0, 0, 6, 6});
	static_assert((Rect{0, 0, 10, 10}).InsetByCopy(-1, -1) == Rect{-1, -1, 11, 11});
}


TEST(RectTest, RoundedUsesFloorOfValuePlusHalf)
{
	// What is tested: Rounded() for positive, negative and exact .5 values.
	// Success criteria: Every edge becomes floor(v + 0.5): 0.5 rounds up to 1,
	// -0.5 rounds up to 0, -0.6 down to -1.
	// Why it matters: Clip rectangles and A1 primitives are rounded this way in
	// every backend (specification 4.4, 7); std::round would round -0.5 to -1
	// and shift negative edges by a pixel.
	//
	// When / Then
	EXPECT_EQ((Rect{0.5f, -0.5f, 2.49f, -0.6f}).Rounded(), (Rect{1, 0, 2, -1}));
	EXPECT_EQ((Rect{1, 2, 3, 4}).Rounded(), (Rect{1, 2, 3, 4}));
}


} // namespace
