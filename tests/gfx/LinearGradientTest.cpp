/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/LinearGradient.h>

#include <array>
#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Color;
using ngui::gfx::GradientStop;
using ngui::gfx::HaikuGradientOffset;
using ngui::gfx::LinearGradient;
using ngui::gfx::Point;


static_assert(std::is_aggregate_v<LinearGradient>);
static_assert(std::is_trivially_copyable_v<LinearGradient>);

constexpr Color kBlack = {0, 0, 0, 1};
constexpr Color kWhite = {1, 1, 1, 1};


LinearGradient
BlackToWhite()
{
	const std::array<GradientStop, 2> stops = {{{0, kBlack}, {1, kWhite}}};
	return *LinearGradient::Create({0, 0}, {10, 0}, stops);
}


TEST(LinearGradientTest, InterpolatesAlongTheAxis)
{
	// What is tested: ColorAt() of a black-to-white gradient from (0, 0) to
	// (10, 0) at the ends, in the middle and off the axis.
	// Success criteria: The ends give the stop colors exactly, x = 5 gives
	// mid-grey, and the y coordinate does not matter.
	// Why it matters: This is the formula of specification 8, interpolated
	// in sRGB-encoded space without premultiplication; the conformance limit
	// for gradients is 3/255 per channel (21.2).
	//
	// Given
	const LinearGradient gradient = BlackToWhite();

	// When / Then
	EXPECT_EQ(gradient.ColorAt({0, 0}), kBlack);
	EXPECT_EQ(gradient.ColorAt({10, 0}), kWhite);
	EXPECT_FLOAT_EQ(gradient.ColorAt({5, 0}).r, 0.5f);
	EXPECT_EQ(gradient.ColorAt({5, 100}), gradient.ColorAt({5, -100}));
}


TEST(LinearGradientTest, ClampsBeyondTheEnds)
{
	// What is tested: ColorAt() before the start, after the end, and for a
	// gradient whose start and end coincide.
	// Success criteria: Before p0 the first stop's color, after p1 the last
	// stop's; with p0 == p1 always the last stop's.
	// Why it matters: The clamp of t and the p0 == p1 rule are spelled out in
	// specification 8; every backend must agree on them.
	//
	// Given
	const LinearGradient gradient = BlackToWhite();
	const std::array<GradientStop, 2> stops = {{{0, kBlack}, {1, kWhite}}};
	const LinearGradient point = *LinearGradient::Create({3, 3}, {3, 3}, stops);

	// When / Then
	EXPECT_EQ(gradient.ColorAt({-50, 0}), kBlack);
	EXPECT_EQ(gradient.ColorAt({50, 0}), kWhite);
	EXPECT_EQ(point.ColorAt({0, 0}), kWhite);
}


TEST(LinearGradientTest, HardEdgesTakeTheLaterStop)
{
	// What is tested: ColorAt() for two stops at the same offset, in the
	// middle and with both stops at the end (the first finding of the fuzz
	// target Gradient).
	// Success criteria: Exactly at the edge and beyond it the later stop's
	// color applies, before it the earlier one's; beyond p1 the color is the
	// last stop's even when all stops are at offset 1.
	// Why it matters: Specification 8 promises the last stop's color after
	// the end; a hard edge must be deterministic on every backend.
	//
	// Given
	const Color red = {1, 0, 0, 1};
	const std::array<GradientStop, 2> middle = {{{0.5f, kBlack}, {0.5f, red}}};
	const std::array<GradientStop, 2> atEnd = {{{1, kBlack}, {1, red}}};
	const LinearGradient edge = *LinearGradient::Create({0, 0}, {10, 0}, middle);
	const LinearGradient end = *LinearGradient::Create({0, 0}, {2, 0}, atEnd);

	// When / Then
	EXPECT_EQ(edge.ColorAt({4.9f, 0}), kBlack);
	EXPECT_EQ(edge.ColorAt({5, 0}), red);
	EXPECT_EQ(edge.ColorAt({5.1f, 0}), red);
	EXPECT_EQ(end.ColorAt({1, 0}), kBlack);
	EXPECT_EQ(end.ColorAt({4, 0}), red);
}


TEST(LinearGradientTest, GlossyStopsFromHaiku)
{
	// What is tested: The four stops of Haiku's glossy look at 0, 132, 136
	// and 255, converted with HaikuGradientOffset().
	// Success criteria: The offsets are 0, 0.518, 0.533 and 1; just below the
	// second stop the color is close to it, just above the third close to it.
	// Why it matters: These are the only stop patterns Haiku's looks use
	// (specification 8 and 22); the narrow band between them is the visible
	// gloss edge.
	//
	// Given
	const Color top = {0.9f, 0.9f, 0.9f, 1};
	const Color edgeAbove = {0.8f, 0.8f, 0.8f, 1};
	const Color edgeBelow = {0.6f, 0.6f, 0.6f, 1};
	const Color bottom = {0.7f, 0.7f, 0.7f, 1};
	const std::array<GradientStop, 4> stops = {{{HaikuGradientOffset(0), top},
		{HaikuGradientOffset(132), edgeAbove}, {HaikuGradientOffset(136), edgeBelow},
		{HaikuGradientOffset(255), bottom}}};

	// When
	const auto gradient = LinearGradient::Create({0, 0}, {0, 255}, stops);

	// Then
	ASSERT_TRUE(gradient.has_value());
	EXPECT_NEAR(stops[1].offset, 0.518f, 0.001f);
	EXPECT_NEAR(stops[2].offset, 0.533f, 0.001f);
	EXPECT_NEAR(gradient->ColorAt({0, 131.9f}).r, 0.8f, 0.001f);
	EXPECT_NEAR(gradient->ColorAt({0, 136.1f}).r, 0.6f, 0.001f);
}


TEST(LinearGradientTest, CreateRejectsInvalidStops)
{
	// What is tested: Create() with too few, too many, descending, out of
	// range and NaN stops, an invalid color and a NaN point.
	// Success criteria: Every case fails with ErrorCode::InvalidArgument.
	// Why it matters: The Canvas drops operations with invalid input
	// (specification 6.2); an invalid gradient must be caught before it
	// reaches a backend.
	//
	// Given
	const float nan = std::numeric_limits<float>::quiet_NaN();
	const std::array<GradientStop, 1> one = {{{0, kBlack}}};
	const std::array<GradientStop, 5> five = {{{0, kBlack}, {0.2f, kBlack}, {0.4f, kBlack},
		{0.6f, kBlack}, {1, kBlack}}};
	const std::array<GradientStop, 2> descending = {{{0.8f, kBlack}, {0.2f, kWhite}}};
	const std::array<GradientStop, 2> outOfRange = {{{0, kBlack}, {1.5f, kWhite}}};
	const std::array<GradientStop, 2> nanOffset = {{{nan, kBlack}, {1, kWhite}}};
	const std::array<GradientStop, 2> badColor = {{{0, kBlack}, {1, {2, 0, 0, 1}}}};
	const std::array<GradientStop, 2> good = {{{0, kBlack}, {1, kWhite}}};

	// When / Then
	for (const auto& result : {LinearGradient::Create({0, 0}, {1, 0}, one),
			LinearGradient::Create({0, 0}, {1, 0}, five),
			LinearGradient::Create({0, 0}, {1, 0}, descending),
			LinearGradient::Create({0, 0}, {1, 0}, outOfRange),
			LinearGradient::Create({0, 0}, {1, 0}, nanOffset),
			LinearGradient::Create({0, 0}, {1, 0}, badColor),
			LinearGradient::Create({nan, 0}, {1, 0}, good)}) {
		ASSERT_FALSE(result.has_value());
		EXPECT_EQ(result.error().code, ngui::ErrorCode::InvalidArgument);
	}
}


} // namespace
