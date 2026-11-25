/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontSource.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Font.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;
using namespace ngui::text;


// Whether \a path names the file \a name, in any case (Windows' file
// names).
bool
EndsWith(std::string path, std::string_view name)
{
	std::ranges::transform(path, path.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return path.ends_with(std::string("\\") + std::string(name))
		|| path.ends_with(std::string("/") + std::string(name));
}


const SourceFont*
Find(const std::vector<SourceFont>& fonts, std::string_view family, std::string_view style)
{
	for (const SourceFont& font : fonts) {
		if (font.family == family && font.style == style)
			return &font;
	}
	return nullptr;
}


TEST(DirectWriteSourceTest, ItListsAndFindsTheSystemsFonts)
{
	// What is tested: The DirectWrite source over the fonts Windows brings:
	// List(); Fallback() for "A" like Arial and for 日 like Noto Sans, which
	// Windows does not have. Skipped on other systems.
	// Success criteria: The list has Arial Regular (arial.ttf) and Bold
	// (with the Bold face), Courier New as monospaced, each family and
	// style once. "A" like Arial is in arial.ttf; 日 is in a font of the
	// system that has it, as its file and face.
	// Why it matters: Under Windows the system's fonts come through
	// DirectWrite, with Windows' own fallback (module X1).
	//
	// Given
	auto source = CreateDirectWriteSource();
	if (!source.has_value() && source.error().code == ErrorCode::NotSupported)
		GTEST_SKIP() << source.error().message;
	ASSERT_TRUE(source.has_value()) << source.error().message;
	const FontFamilyStyle arial{"Arial", "Regular", 1, BFontFace::Regular, false};
	const FontFamilyStyle sans{"Noto Sans", "Regular", 1, BFontFace::Regular, false};

	// When
	const std::vector<SourceFont> fonts = (*source)->List();
	const std::optional<FontLocation> latin = (*source)->Fallback(U'A', arial);
	const std::optional<FontLocation> han = (*source)->Fallback(U'日', sans);

	// Then
	const SourceFont* regular = Find(fonts, "Arial", "Regular");
	const SourceFont* bold = Find(fonts, "Arial", "Bold");
	const SourceFont* courier = Find(fonts, "Courier New", "Regular");
	ASSERT_NE(regular, nullptr);
	ASSERT_NE(bold, nullptr);
	ASSERT_NE(courier, nullptr);
	EXPECT_TRUE(EndsWith(regular->location.path, "arial.ttf")) << regular->location.path;
	EXPECT_EQ(regular->location.faceIndex, 0u);
	EXPECT_TRUE(bold->face.Has(BFontFace::Bold));
	EXPECT_TRUE(courier->fixed);
	EXPECT_FALSE(regular->fixed);
	std::set<std::pair<std::string, std::string>> seen;
	for (const SourceFont& font : fonts)
		EXPECT_TRUE(seen.emplace(font.family, font.style).second) << font.family;
	ASSERT_TRUE(latin.has_value());
	EXPECT_TRUE(EndsWith(latin->path, "arial.ttf")) << latin->path;
	ASSERT_TRUE(han.has_value());
	EXPECT_FALSE(han->path.empty());
}


TEST(DirectWriteSourceTest, ATextSystemUsesTheSystemsFonts)
{
	// What is tested: A FontTextSystem over the DirectWrite source: "a日" in
	// the plain role; a BFont set to Arial Bold, with the text system as
	// default. Skipped on other systems.
	// Success criteria: 日 comes from a font of the system (a FontId of its
	// own, a real glyph), "a" stays in Noto Sans. Arial Bold is among the
	// styles, and the BFont measures "A" with it: 1479 of 2048 units at
	// 12 px, 8.666 px (Noto Sans: 7.668).
	// Why it matters: Text that the bundled fonts cannot show is drawn with
	// the system's fonts, and applications can choose them (module X1).
	//
	// Given
	auto source = CreateDirectWriteSource();
	if (!source.has_value() && source.error().code == ErrorCode::NotSupported)
		GTEST_SKIP() << source.error().message;
	ASSERT_TRUE(source.has_value()) << source.error().message;
	const auto text = std::move(*FontTextSystem::Create(std::move(*source)));

	// When
	const ShapedText shaped = text->Shape("a日", FontRole::Plain);
	TextSystem::SetDefault(text.get());
	BFont font;
	const bool chosen = font.SetFamilyAndStyle("Arial", "Bold").has_value();
	const float width = font.StringWidth("A");
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_EQ(shaped.fontRuns.size(), 2u);
	EXPECT_EQ(shaped.fontRuns[0].font, 1u);
	EXPECT_GE(shaped.fontRuns[1].font, 4u);
	ASSERT_EQ(shaped.glyphs.size(), 2u);
	EXPECT_NE(shaped.glyphs[1], 0u);
	EXPECT_TRUE(text->Style("Arial", "Bold").has_value());
	EXPECT_TRUE(chosen);
	EXPECT_NEAR(width, 1479.0f * 12 / 2048, 0.01f);
}


} // namespace
