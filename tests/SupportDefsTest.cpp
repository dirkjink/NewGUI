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
