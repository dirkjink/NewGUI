/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Size.h>

#include <limits>
#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Size;


static_assert(std::is_aggregate_v<Size>);
static_assert(std::is_trivially_copyable_v<Size>);


TEST(SizeTest, EmptyUnlessBothExtentsPositive)
{
	// What is tested: IsEmpty() and IsFinite() for positive, zero, negative and
	// NaN extents.
	// Success criteria: Only a size with two positive extents is non-empty; NaN
	// counts as empty and as not finite.
	// Why it matters: Size is the extent of a half-open rectangle; unlike
	// Haiku's BSize, Size{0, 0} covers no pixel at all.
	//
	// When / Then
	static_assert(!(Size{1, 1}).IsEmpty());
	static_assert((Size{0, 1}).IsEmpty());
	static_assert((Size{1, -1}).IsEmpty());
	EXPECT_TRUE((Size{std::numeric_limits<float>::quiet_NaN(), 1}).IsEmpty());
	EXPECT_FALSE((Size{std::numeric_limits<float>::quiet_NaN(), 1}).IsFinite());
	EXPECT_TRUE((Size{2, 3}).IsFinite());
}


} // namespace
