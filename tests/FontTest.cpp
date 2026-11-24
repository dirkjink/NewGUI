/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Font.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;


std::pair<std::string, std::string>
FamilyAndStyle(const BFont& font)
{
	std::string family;
	std::string style;
	font.GetFamilyAndStyle(&family, &style);
	return {family, style};
}


TEST(FontTest, TheRolesHaveTheirFonts)
{
	// What is tested: With the fixed text system (its default styles):
	// BFont(), Plain(), Bold() and Fixed(); Families() and Styles().
	// Success criteria: The plain font is "Default" "Regular" at 12, as
	// BFont() is; the bold one "Default" "Bold" with the Bold face; the
	// fixed one "Default Mono" "Regular" and monospaced. There are two
	// families, "Default" with Regular and Bold, and "Default Mono".
	// Why it matters: be_plain_font, be_bold_font and be_fixed_font are
	// where applications start from; the lists are what a font menu shows.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);

	// When
	const BFont plain = BFont::Plain();
	const BFont bold = BFont::Bold();
	const BFont mono = BFont::Fixed();

	// Then
	EXPECT_EQ(FamilyAndStyle(plain), (std::pair<std::string, std::string>{"Default",
		"Regular"}));
	EXPECT_EQ(plain.Size(), 12);
	EXPECT_TRUE(BFont() == plain);
	EXPECT_EQ(FamilyAndStyle(bold), (std::pair<std::string, std::string>{"Default", "Bold"}));
	EXPECT_TRUE(bold.Face().Has(BFontFace::Bold));
	EXPECT_FALSE(plain.IsFixed());
	EXPECT_EQ(FamilyAndStyle(mono), (std::pair<std::string, std::string>{"Default Mono",
		"Regular"}));
	EXPECT_TRUE(mono.IsFixed());
	EXPECT_EQ(BFont::Families(), (std::vector<std::string>{"Default", "Default Mono"}));
	EXPECT_EQ(BFont::Styles("Default"), (std::vector<std::string>{"Regular", "Bold"}));
	EXPECT_TRUE(BFont::Styles("Nope").empty());
	TextSystem::SetDefault(nullptr);
}


TEST(FontTest, FamilyStyleAndFaceAreChosen)
{
	// What is tested: The plain font given the Bold face, then Italic (no
	// family has it); "Default Mono" with an empty style; an unknown family
	// and an unknown style; the Bold face asked of "Default Mono"; a size
	// of 0.
	// Success criteria: Bold chooses the family's Bold style; a face the
	// family lacks leaves the style. An empty style takes the regular one.
	// Unknown names and faces are NotFound and change nothing; a size not
	// above 0 is ignored.
	// Why it matters: Font menus and styled text pick fonts by name and
	// face, as with Haiku's BFont.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BFont font;

	// When
	font.SetFace(BFontFace::Bold);
	const auto bold = FamilyAndStyle(font);
	font.SetFace(BFontFace::Italic);
	const auto italic = FamilyAndStyle(font);
	const auto mono = font.SetFamilyAndStyle("Default Mono", "");
	const auto monoNames = FamilyAndStyle(font);
	const auto unknownFamily = font.SetFamilyAndStyle("Nope", "Regular");
	const auto unknownStyle = font.SetFamilyAndStyle("", "Black");
	const auto boldMono = font.SetFamilyAndFace("", BFontFace::Bold);
	font.SetSize(0);

	// Then
	EXPECT_EQ(bold, (std::pair<std::string, std::string>{"Default", "Bold"}));
	EXPECT_EQ(italic, bold);
	EXPECT_TRUE(mono.has_value());
	EXPECT_EQ(monoNames, (std::pair<std::string, std::string>{"Default Mono", "Regular"}));
	ASSERT_FALSE(unknownFamily.has_value());
	EXPECT_EQ(unknownFamily.error().code, ErrorCode::NotFound);
	EXPECT_FALSE(unknownStyle.has_value());
	EXPECT_FALSE(boldMono.has_value());
	EXPECT_EQ(FamilyAndStyle(font), monoNames);
	EXPECT_EQ(font.Size(), 12);
	TextSystem::SetDefault(nullptr);
}


TEST(FontTest, MetricsFollowTheSize)
{
	// What is tested: With the fixed text system (7 px per byte, ascent 9,
	// descent 3, leading 1 at 12): the bold font at 24: its Spec(), height,
	// the width of "abc", "abcdef" truncated to 50 pixels; the shaped run
	// of "ab" in its spec.
	// Success criteria: The spec is the bold font file (2) at 24; the
	// metrics are twice those at 12: height 18, 6, 2, width 42; truncation
	// keeps what fits at that size, 3 bytes; the run is 24 pixels with
	// positions 0 and 14.
	// Why it matters: A BFont is measured at its own size by the text
	// system, scaled linearly from the role where the text system knows
	// only that (specification 13.4: no hinting).
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BFont font = BFont::Bold();
	font.SetSize(24);

	// When
	const FontSpec spec = font.Spec();
	FontHeight height{};
	font.GetHeight(&height);
	const float width = font.StringWidth("abc");
	std::string truncated = "abcdef";
	font.TruncateString(&truncated, TruncationMode::End, 50);
	const ShapedText shaped = TextSystem::Default().Shape("ab", spec);

	// Then
	EXPECT_EQ(spec.font, 2u);
	EXPECT_EQ(spec.size, 24);
	EXPECT_EQ(height.ascent, 18);
	EXPECT_EQ(height.descent, 6);
	EXPECT_EQ(height.leading, 2);
	EXPECT_EQ(width, 42);
	EXPECT_EQ(truncated, "abc");
	EXPECT_EQ(shaped.sizePx, 24);
	ASSERT_EQ(shaped.positions.size(), 2u);
	EXPECT_EQ(shaped.positions[1].x, 14);
	TextSystem::SetDefault(nullptr);
}


} // namespace
