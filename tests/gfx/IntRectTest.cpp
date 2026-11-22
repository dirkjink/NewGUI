/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/IntRect.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::IntRect;
using ngui::gfx::Rect;


static_assert(std::is_aggregate_v<IntRect>);
static_assert(std::is_trivially_copyable_v<IntRect>);


TEST(IntRectTest, HalfOpenLikeRect)
{
	// What is tested: Width, height, emptiness and the conversion to Rect.
	// Success criteria: IntRect{0, 0, 10, 10} is 10 × 10; zero-width and
	// inverted rectangles are empty; ToRect() keeps every edge.
	// Why it matters: The clip region and the Canvas exchange rectangles in
	// both types; they must agree on the half-open model.
	//
	// When / Then
	static_assert((IntRect{0, 0, 10, 10}).Width() == 10);
	static_assert((IntRect{0, 0, 10, 10}).Height() == 10);
	static_assert((IntRect{3, 3, 3, 9}).IsEmpty());
	static_assert((IntRect{5, 5, 1, 1}).IsEmpty());
	static_assert(!(IntRect{0, 0, 1, 1}).IsEmpty());
	static_assert((IntRect{-2, 1, 4, 7}).ToRect() == Rect{-2, 1, 4, 7});
}


TEST(IntRectTest, RoundedFromFollowsTheClipRule)
{
	// What is tested: RoundedFrom() for fractional, negative, huge and NaN
	// edges.
	// Success criteria: Edges are rounded with floor(v + 0.5); values beyond
	// ±2^30 are clamped; a NaN edge gives the empty rectangle {0, 0, 0, 0}.
	// Why it matters: ClipRect() rounds this way after the translation
	// (specification 7); without the clamp a huge float would overflow int32,
	// which is undefined behaviour.
	//
	// Given
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float huge = 1e20f;

	// When / Then
	EXPECT_EQ(IntRect::RoundedFrom(Rect{0.5f, -0.5f, 9.49f, 9.5f}), (IntRect{1, 0, 9, 10}));
	EXPECT_EQ(IntRect::RoundedFrom(Rect{-huge, 0, huge, 1}),
		(IntRect{-(1 << 30), 0, 1 << 30, 1}));
	EXPECT_EQ(IntRect::RoundedFrom(Rect{0, nan, 1, 1}), (IntRect{0, 0, 0, 0}));
}


} // namespace
