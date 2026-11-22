/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/RRect.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Radii;
using ngui::gfx::Rect;
using ngui::gfx::RRect;


static_assert(std::is_aggregate_v<RRect>);
static_assert(std::is_trivially_copyable_v<RRect>);


TEST(RRectTest, NormalizedKeepsFittingRadii)
{
	// What is tested: Normalized() for radii that already fit the rectangle.
	// Success criteria: The radii come back unchanged.
	// Why it matters: The common case, a button with a radius of a few pixels,
	// must be drawn exactly as requested.
	//
	// Given
	const RRect rrect = {{0, 0, 40, 20}, {3, 4, 5, 6}};

	// When / Then
	EXPECT_EQ(rrect.Normalized(), rrect);
}


TEST(RRectTest, NormalizedLimitsToHalfTheShorterSide)
{
	// What is tested: Normalized() for a uniform radius larger than half the
	// height of a wide rectangle.
	// Success criteria: Every radius becomes min(width, height) / 2, which makes
	// the short sides semicircles.
	// Why it matters: The limit from specification 5; a pill-shaped control is
	// drawn with an oversized radius on purpose.
	//
	// Given
	const RRect pill = {{0, 0, 100, 10}, Radii::Uniform(50)};

	// When / Then
	EXPECT_EQ(pill.Normalized().radii, Radii::Uniform(5));
}


TEST(RRectTest, NormalizedScalesAllRadiiProportionally)
{
	// What is tested: Normalized() when two adjacent radii add up to more than
	// their edge.
	// Success criteria: All four radii shrink by the same factor (here 10 / 12),
	// then each is limited to half the shorter side.
	// Why it matters: The proportional rule of specification 5 (as in CSS and
	// Skia) keeps the shape of mixed radii instead of clipping just the large
	// one.
	//
	// Given
	const RRect rrect = {{0, 0, 10, 10}, {10, 2, 0, 0}};

	// When
	const Radii radii = rrect.Normalized().radii;

	// Then
	EXPECT_FLOAT_EQ(radii.tl, 5.0f);
	EXPECT_FLOAT_EQ(radii.tr, 2.0f * 10.0f / 12.0f);
	EXPECT_EQ(radii.br, 0.0f);
	EXPECT_EQ(radii.bl, 0.0f);
}


TEST(RRectTest, NormalizedDropsInvalidRadii)
{
	// What is tested: Normalized() for negative, NaN and infinite radii and for
	// an empty rectangle.
	// Success criteria: Negative, NaN and infinite radii become 0, as in Skia;
	// an empty rectangle keeps its coordinates but has no radii.
	// Why it matters: Radii are >= 0 by definition (specification 5); invalid
	// values must not reach the backends.
	//
	// Given
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const float infinity = std::numeric_limits<float>::infinity();
	const RRect invalid = {{0, 0, 10, 10}, {-1, nan, 2, infinity}};
	const RRect empty = {{5, 5, 5, 9}, Radii::Uniform(3)};

	// When / Then
	EXPECT_EQ(invalid.Normalized().radii, (Radii{0, 0, 2, 0}));
	EXPECT_EQ(empty.Normalized(), (RRect{{5, 5, 5, 9}, Radii::Uniform(0)}));
}


TEST(RRectTest, NormalizedFitsSubnormalExtents)
{
	// What is tested: Normalized() for a rectangle whose height is a subnormal
	// float, with huge radii (the first finding of the fuzz target Rect).
	// Success criteria: Two adjacent radii add up to at most the height.
	// Why it matters: Halving a subnormal rounds up; without the correction
	// the radii exceeded their edge by one step, which backends such as Skia
	// reject as an invalid rounded rectangle.
	//
	// Given
	const float height = 7.598821182494186e-41f;
	const RRect rrect = {{0, 0, 1e30f, height}, Radii::Uniform(1e30f)};

	// When
	const Radii radii = rrect.Normalized().radii;

	// Then
	EXPECT_LE(radii.tr + radii.br, height);
	EXPECT_LE(radii.tl + radii.bl, height);
	EXPECT_GT(radii.tl, 0.0f);
}


} // namespace
