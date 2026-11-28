/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "svg/IconGamma.h"

#include <cstdint>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

#include <ngui/Bitmap.h>
#include <ngui/svg/LunaSvgRenderer.h>


namespace {


using namespace ngui;
using ngui::svg::detail::IconGamma;
using ngui::svg::detail::LinearizeColors;


TEST(IconGammaTest, TheTableIsAggs)
{
	// What is tested: Haiku's gamma table of 2.2 (agg::gamma_lut<int8u,
	// int8u>) at 0, 128, 255 and both ways.
	// Success criteria: dir() keeps 0 and 255 and takes 128 to 56; inv()
	// takes 56 back to 128 and 127 to 186.
	// Why it matters: Icons come out as Haiku's only with Haiku's own
	// table, rounding included.
	//
	// Given
	const IconGamma& gamma = IconGamma::Haiku();

	// Then
	EXPECT_EQ(gamma.dir[0], 0);
	EXPECT_EQ(gamma.dir[255], 255);
	EXPECT_EQ(gamma.dir[128], 56);
	EXPECT_EQ(gamma.inv[56], 128);
	EXPECT_EQ(gamma.inv[127], 186);
	EXPECT_EQ(gamma.inv[255], 255);
}


TEST(IconGammaTest, ColorsGoIntoLinearLight)
{
	// What is tested: LinearizeColors() on the color properties as
	// attributes (" and ') and CSS declarations, in the forms #rgb, #rgba,
	// #rrggbb, #rrggbbaa, rgb() with numbers and percentages, rgba(), with
	// !important; and on what it must leave: fill-opacity, color-
	// interpolation, data-fill, names, url(), none, malformed colors, text.
	// Success criteria: Each component goes through the table (#808080 to
	// #383838), alpha stays; everything else stays as it is.
	// Why it matters: Haiku's IconRenderer draws with the colors in linear
	// light; the icons' SVGs give them in these forms.
	//
	// Then
	EXPECT_EQ(LinearizeColors(R"x(<rect fill="#808080" stroke='#88f'/>)x"),
		R"x(<rect fill="#383838" stroke='#4040ff'/>)x");
	EXPECT_EQ(LinearizeColors(R"x(<stop stop-color="#80808080"/><stop stop-color="#8888"/>)x"),
		R"x(<stop stop-color="#38383880"/><stop stop-color="#40404088"/>)x");
	EXPECT_EQ(LinearizeColors("<style>.a{fill:#808080;stroke: rgb(128, 50%, 0)}</style>"),
		"<style>.a{fill:#383838;stroke:rgb(56,56,0)}</style>");
	EXPECT_EQ(LinearizeColors(R"x(<g style="color:rgba(128,128,128,0.5)">)x"),
		R"x(<g style="color:rgba(56,56,56,0.5)">)x");
	EXPECT_EQ(LinearizeColors("<style>*{fill:#808080 !important}</style>"),
		"<style>*{fill:#383838 !important}</style>");
	for (const std::string_view unchanged : {
			R"x(<rect fill-opacity="0.5" color-interpolation="linearRGB"/>)x",
			R"x(<rect data-fill="#808080" fill="gray" stroke="url(#g)"/>)x",
			R"x(<rect fill="none" stroke="#12345" color="rgb(1,2)"/>)x",
			"<title>fill and stroke</title>", "fill=", "fill:", ""}) {
		SCOPED_TRACE(unchanged);
		EXPECT_EQ(LinearizeColors(unchanged), unchanged);
	}
}


TEST(IconGammaTest, IconsBlendInLinearLight)
{
	// What is tested: The lunasvg renderer drawing black at half opacity
	// over white, and a gradient from black to white, into 8 × 1 bitmaps.
	// Success criteria: Half black over white is 186, as Haiku's
	// IconRenderer gives it (linear 127, back through the table), not the
	// 128 of blending in sRGB; the gradient's middle is lighter than 128
	// for the same reason; white stays 255.
	// Why it matters: Haiku blends its icons in linear light; their
	// semi-transparent shadows and edges are lighter than an SVG renderer
	// alone draws them.
	//
	// Given
	const svg::LunaSvgRenderer renderer;
	constexpr std::string_view kHalfBlack
		= R"x(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="1">)x"
		R"x(<rect width="8" height="1" fill="#ffffff"/>)x"
		R"x(<rect width="8" height="1" fill="#000000" fill-opacity="0.5"/></svg>)x";
	constexpr std::string_view kGradient
		= R"x(<svg xmlns="http://www.w3.org/2000/svg" width="8" height="1"><defs>)x"
		R"x(<linearGradient id="g"><stop offset="0" stop-color="#000000"/>)x"
		R"x(<stop offset="1" stop-color="#ffffff"/></linearGradient></defs>)x"
		R"x(<rect width="8" height="1" fill="url(#g)"/></svg>)x";
	const auto bytes = [](std::string_view text) {
		return std::span<const std::uint8_t>(
			reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
	};
	BBitmap half({0, 0, 8, 1}, BColorSpace::RGBA32);
	BBitmap gradient({0, 0, 8, 1}, BColorSpace::RGBA32);

	// When
	ASSERT_TRUE(renderer.Render(bytes(kHalfBlack), half).has_value());
	ASSERT_TRUE(renderer.Render(bytes(kGradient), gradient).has_value());

	// Then
	const std::span<const std::uint8_t> halfBits = std::as_const(half).Bits();
	const std::span<const std::uint8_t> gradientBits = std::as_const(gradient).Bits();
	EXPECT_NEAR(halfBits[0], 186, 1);
	EXPECT_EQ(halfBits[3], 255);
	EXPECT_GT(gradientBits[4 * 4], 150);
	EXPECT_EQ(gradientBits[7 * 4 + 3], 255);
}


} // namespace
