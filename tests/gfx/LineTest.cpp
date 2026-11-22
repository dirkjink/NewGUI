/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Line.h>

#include <type_traits>

#include <gtest/gtest.h>


namespace {


using ngui::gfx::Color;
using ngui::gfx::Line;


TEST(LineTest, IsPlainData)
{
	// What is tested: That Line is an aggregate, trivially copyable, and
	// compares all its fields.
	// Success criteria: The type traits hold, and lines differing only in
	// color compare unequal.
	// Why it matters: DrawLines records arrays of Line in the DisplayList,
	// which copies them as plain bytes (specification 5 and 11).
	//
	// When / Then
	static_assert(std::is_aggregate_v<Line>);
	static_assert(std::is_trivially_copyable_v<Line>);
	constexpr Line red = {{0, 0}, {10, 0}, {1, 0, 0, 1}};
	static_assert(red == Line{{0, 0}, {10, 0}, {1, 0, 0, 1}});
	static_assert(red != Line{{0, 0}, {10, 0}, Color{0, 0, 1, 1}});
	SUCCEED();
}


} // namespace
