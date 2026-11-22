/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Flags.h>

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>


namespace test_flags {


enum class State : std::uint32_t {
	Focused = 1u << 0,
	Clicked = 1u << 1,
	Disabled = 1u << 2
};
NGUI_FLAG_ENUM(State)


enum class Border : std::uint8_t {
	Left = 1u << 0,
	Right = 1u << 1,
	Top = 1u << 2,
	Bottom = 1u << 3
};
NGUI_FLAG_ENUM(Border)


enum class NotOptedIn : std::uint8_t {
	A = 1
};


} // namespace test_flags


namespace {


using test_flags::Border;
using test_flags::State;
using StateFlags = ngui::Flags<State>;
using BorderFlags = ngui::Flags<Border>;


static_assert(std::is_trivially_copyable_v<StateFlags>);
static_assert(ngui::FlagEnum<State>);
static_assert(!ngui::FlagEnum<test_flags::NotOptedIn>);


TEST(FlagsTest, CombineAndQuery)
{
	// What is tested: Building a set with `E | E`, querying it with Has(),
	// HasAny() and IsEmpty(), and Raw().
	// Success criteria: The set holds exactly the combined flags, and its raw
	// value is the bitwise or of the enumerators.
	// Why it matters: Control states and border masks of the looks are written
	// this way (specification 18.3, L5); it replaces Haiku's B_ constants.
	//
	// Given
	constexpr StateFlags state = State::Focused | State::Clicked;

	// When / Then
	static_assert(state.Has(State::Focused) && state.Has(State::Clicked));
	static_assert(!state.Has(State::Disabled));
	static_assert(state.HasAny(State::Clicked | State::Disabled));
	static_assert(!StateFlags().HasAny(State::Focused));
	static_assert(StateFlags().IsEmpty());
	static_assert(state.Raw() == 0b011u);
	static_assert(StateFlags::FromRaw(0b011u) == state);
}


TEST(FlagsTest, ModifyInPlace)
{
	// What is tested: Set(), Clear() and the compound operators.
	// Success criteria: Each modifies exactly the bits of its operand.
	// Why it matters: Views update their state flags in place on every event;
	// a wrong mask would leave a control stuck in a state.
	//
	// Given
	BorderFlags borders = Border::Left;

	// When
	borders.Set(Border::Top).Set(Border::Left, false);
	borders |= Border::Bottom;

	// Then
	EXPECT_EQ(borders, Border::Top | Border::Bottom);
	borders ^= Border::Top | Border::Right;
	EXPECT_EQ(borders, Border::Right | Border::Bottom);
	borders &= Border::Right;
	EXPECT_EQ(borders, BorderFlags(Border::Right));
	borders.Clear(Border::Right);
	EXPECT_TRUE(borders.IsEmpty());
	EXPECT_EQ((~BorderFlags(Border::Left)).Raw(), 0xfeu);
	EXPECT_EQ((BorderFlags(Border::Left) & Border::Right).Raw(), 0u);
	EXPECT_EQ((BorderFlags(Border::Left) ^ Border::Left).Raw(), 0u);
}


} // namespace
