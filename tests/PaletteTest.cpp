/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Palette.h>

#include <cstdint>

#include <gtest/gtest.h>


namespace {


using ngui::BColorWhich;
using ngui::Palette;
using ngui::TintColor;
using ngui::gfx::Color;


std::uint8_t
Byte(float value)
{
	return static_cast<std::uint8_t>(value * 255.0f + 0.5f);
}


TEST(PaletteTest, DefaultsAreHaikus)
{
	// What is tested: Some colors of Palette::Default() and Set().
	// Success criteria: Panel background 216, control border 172, control
	// mark (27, 82, 140), failure (255, 65, 54); Set() changes only its role.
	// Why it matters: The ported looks derive all their colors from the
	// palette (specification 18.1, L3); with other defaults they could not
	// match Haiku's reference images.
	//
	// Given
	const Palette& palette = Palette::Default();
	Palette changed = palette;

	// When
	changed.Set(BColorWhich::PanelBackground, {1, 0, 0, 1});

	// Then
	EXPECT_EQ(palette.Get(BColorWhich::PanelBackground), Color::FromRgba8(216, 216, 216));
	EXPECT_EQ(palette.Get(BColorWhich::ControlBorder), Color::FromRgba8(172, 172, 172));
	EXPECT_EQ(palette.Get(BColorWhich::ControlMark), Color::FromRgba8(27, 82, 140));
	EXPECT_EQ(palette.Get(BColorWhich::Failure), Color::FromRgba8(255, 65, 54));
	EXPECT_EQ(changed.Get(BColorWhich::PanelBackground), (Color{1, 0, 0, 1}));
	EXPECT_EQ(changed.Get(BColorWhich::MenuBackground), palette.Get(BColorWhich::MenuBackground));
	EXPECT_FALSE(changed == palette);
}


TEST(PaletteTest, TintColorMatchesHaiku)
{
	// What is tested: TintColor() of the panel gray 216 with every named
	// tint, and of a color with alpha.
	// Success criteria: Exactly what Haiku's tint_color() computes: 255,
	// 239, 231, 216, 184, 152, 128, 96, 0; alpha stays unchanged. (The
	// comments in Haiku's InterfaceDefs.h say 240 and 232 for the two
	// lighten tints, but the code truncates 239.985 and 231.99.)
	// Why it matters: tint_color() works on 8-bit components and truncates;
	// the bevels of the ported looks are made of such tints, so a float
	// formula, or rounding, would shift them by one step.
	//
	// Given
	const Color gray = Color::FromRgba8(216, 216, 216);

	// When / Then
	EXPECT_EQ(Byte(TintColor(gray, ngui::kLightenMaxTint).r), 255);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kLighten2Tint).r), 239);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kLighten1Tint).r), 231);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kNoTint).r), 216);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kDarken1Tint).r), 184);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kDarken2Tint).r), 152);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kDarken3Tint).r), 128);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kDarken4Tint).r), 96);
	EXPECT_EQ(Byte(TintColor(gray, ngui::kDarkenMaxTint).r), 0);
	EXPECT_EQ(Byte(TintColor(Color::FromRgba8(216, 100, 0, 128), ngui::kDarken1Tint).a), 128);
}


} // namespace
