/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Color.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Color;


static_assert(std::is_aggregate_v<Color>);
static_assert(std::is_trivially_copyable_v<Color>);


TEST(ColorTest, FromRgba8DividesBy255)
{
	// What is tested: The conversion of 8-bit components, as in Haiku's
	// rgb_color, to float components.
	// Success criteria: 0 becomes 0, 255 becomes 1, 51 becomes 0.2, and the
	// alpha defaults to opaque.
	// Why it matters: Every color of the ported looks goes through this
	// conversion; a different divisor would shift all of them.
	//
	// When / Then
	static_assert(Color::FromRgba8(0, 255, 51) == Color{0.0f, 1.0f, 51 / 255.0f, 1.0f});
	static_assert(Color::FromRgba8(0, 0, 0, 0).a == 0.0f);
	EXPECT_FLOAT_EQ(Color::FromRgba8(51, 0, 0).r, 0.2f);
}


TEST(ColorTest, IsValidRequiresUnitRange)
{
	// What is tested: IsValid() for components inside, at the bounds of and
	// outside [0, 1], and for NaN.
	// Success criteria: 0 and 1 are valid; -0.01, 1.01 and NaN are not.
	// Why it matters: Colors are not premultiplied and must stay in [0, 1]
	// (specification 4.3); the Canvas uses this check to reject bad input.
	//
	// When / Then
	EXPECT_TRUE((Color{0, 0, 0, 0}).IsValid());
	EXPECT_TRUE((Color{1, 1, 1, 1}).IsValid());
	EXPECT_FALSE((Color{-0.01f, 0, 0, 1}).IsValid());
	EXPECT_FALSE((Color{0, 1.01f, 0, 1}).IsValid());
	EXPECT_FALSE((Color{0, 0, std::numeric_limits<float>::quiet_NaN(), 1}).IsValid());
	static_assert((Color{0.1f, 0.2f, 0.3f, 1}).WithAlpha(0.5f) == Color{0.1f, 0.2f, 0.3f, 0.5f});
}


} // namespace
