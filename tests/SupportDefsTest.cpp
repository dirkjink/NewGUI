/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/SupportDefs.h>

#include <type_traits>

#include <gtest/gtest.h>


namespace {


template<typename Alias, typename Expected>
constexpr bool kIsAlias = std::is_same_v<Alias, Expected>;


TEST(SupportDefsTest, AliasesMatchFixedWidthTypes)
{
	// What is tested: The integer aliases for ported Haiku code (int8 ...
	// uint64) against the fixed-width types of <cstdint>.
	// Success criteria: Every alias is the same type as its std:: counterpart
	// (checked at compile time), and two samples have the expected size.
	// Why it matters: Ported code relies on the exact widths, and the switch
	// to the target conventions (CONVENTIONS.md 7) replaces the aliases by the
	// std:: types one to one, which is only safe if they are identical.
	static_assert(kIsAlias<ngui::int8, std::int8_t>);
	static_assert(kIsAlias<ngui::uint8, std::uint8_t>);
	static_assert(kIsAlias<ngui::int16, std::int16_t>);
	static_assert(kIsAlias<ngui::uint16, std::uint16_t>);
	static_assert(kIsAlias<ngui::int32, std::int32_t>);
	static_assert(kIsAlias<ngui::uint32, std::uint32_t>);
	static_assert(kIsAlias<ngui::int64, std::int64_t>);
	static_assert(kIsAlias<ngui::uint64, std::uint64_t>);

	EXPECT_EQ(sizeof(ngui::int32), 4u);
	EXPECT_EQ(sizeof(ngui::uint64), 8u);
}


} // namespace
