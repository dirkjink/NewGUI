/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Paint.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Color;
using ngui::gfx::LinearGradient;
using ngui::gfx::Paint;


static_assert(std::is_aggregate_v<Paint>);
static_assert(std::is_trivially_copyable_v<Paint>);


TEST(PaintTest, DefaultsFollowTheSpecification)
{
	// What is tested: A default-constructed Paint.
	// Success criteria: Opaque black, no gradient, stroke width 1, butt caps,
	// miter joins, antialiasing on, and valid.
	// Why it matters: The defaults are part of the API (specification 8);
	// ported code that only sets a color relies on the rest.
	//
	// Given
	const Paint paint;

	// When / Then
	EXPECT_EQ(paint.color, (Color{0, 0, 0, 1}));
	EXPECT_FALSE(paint.gradient.has_value());
	EXPECT_EQ(paint.strokeWidth, 1.0f);
	EXPECT_EQ(paint.cap, Paint::Cap::Butt);
	EXPECT_EQ(paint.join, Paint::Join::Miter);
	EXPECT_TRUE(paint.antialias);
	EXPECT_TRUE(paint.IsValid());
}


TEST(PaintTest, IsValidRejectsBadInput)
{
	// What is tested: IsValid() with an invalid color, a negative, NaN and
	// zero stroke width, and an invalid gradient.
	// Success criteria: Only the zero width is valid, the others are not.
	// Why it matters: The Canvas discards operations whose Paint is invalid
	// (specification 6.2), deterministically on every backend.
	//
	// When / Then
	EXPECT_FALSE((Paint{.color = {1, 1, 1, 2}}).IsValid());
	EXPECT_FALSE((Paint{.strokeWidth = -1}).IsValid());
	EXPECT_FALSE((Paint{.strokeWidth = std::numeric_limits<float>::quiet_NaN()}).IsValid());
	EXPECT_TRUE((Paint{.strokeWidth = 0}).IsValid());
	EXPECT_FALSE((Paint{.gradient = LinearGradient{{0, 0}, {1, 0}, {}, 1}}).IsValid());
}


} // namespace
