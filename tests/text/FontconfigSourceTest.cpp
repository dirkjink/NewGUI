/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontSource.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Font.h>
#include <ngui/text/FontTextSystem.h>

#include "support/ScopedLook.h"


namespace {


using namespace ngui;
using namespace ngui::text;


const std::string kCollection = std::string(NGUI_TEST_FONTS_DIR) + "/NotoSansCJK-Subset.ttc";
const std::string kMono = std::string(NGUI_BUNDLED_FONTS_DIR) + "/NotoSansMono-Regular.ttf";


// A fontconfig configuration that knows only the test collection and the
// bundled Noto fonts, so that the tests do not depend on the machine's
// fonts; its cache in a directory of its own.
class TestConfig {
public:
	TestConfig()
		:
		fDirectory(std::filesystem::temp_directory_path()
			/ ("ngui-fontconfig-" + std::to_string(::testing::UnitTest::GetInstance()
				->random_seed()) + "-" + ::testing::UnitTest::GetInstance()
				->current_test_info()->name()))
	{
		std::filesystem::create_directories(fDirectory);
		std::ofstream file(File());
		file << "<?xml version=\"1.0\"?>\n"
			<< "<!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">\n"
			<< "<fontconfig>\n"
			<< "  <dir>" << NGUI_TEST_FONTS_DIR << "</dir>\n"
			<< "  <dir>" << NGUI_BUNDLED_FONTS_DIR << "</dir>\n"
			<< "  <cachedir>" << (fDirectory / "cache").string() << "</cachedir>\n"
			<< "</fontconfig>\n";
	}

	~TestConfig()
	{
		std::error_code ignored;
		std::filesystem::remove_all(fDirectory, ignored);
	}

	std::string File() const
	{
		return (fDirectory / "fonts.conf").string();
	}

private:
	std::filesystem::path fDirectory;
};


const SourceFont*
Find(const std::vector<SourceFont>& fonts, std::string_view family, std::string_view style)
{
	for (const SourceFont& font : fonts) {
		if (font.family == family && font.style == style)
			return &font;
	}
	return nullptr;
}


TEST(FontconfigSourceTest, ItListsAndFindsTheFontsOfItsConfiguration)
{
	// What is tested: A fontconfig source over a configuration with the test
	// collection and the bundled fonts: List(); Fallback() for 日, 한, a Thai
	// letter, and "A" like Noto Sans Mono; a configuration file that does
	// not exist. Skipped where fontconfig is not there (Windows, a Linux
	// without libfontconfig.so.1).
	// Success criteria: The list has both faces of the collection (JP face
	// 0, KR face 1), "Noto Sans" "Bold" with the Bold face, and "Noto Sans
	// Mono" Regular. Fallback() finds 日 in face 0 and 한 in face 1, no font
	// for Thai, and for "A" like Noto Sans Mono that file (fontconfig sorts
	// by the family first). The missing file is an IoError.
	// Why it matters: The system's fonts come through fontconfig under
	// Linux, with its configuration (module X1).
	//
	// Given
	const TestConfig config;
	auto source = CreateFontconfigSource(config.File());
	if (!source.has_value() && source.error().code == ErrorCode::NotSupported)
		GTEST_SKIP() << source.error().message;
	ASSERT_TRUE(source.has_value()) << source.error().message;
	const FontFamilyStyle sans{"Noto Sans", "Regular", 1, BFontFace::Regular, false};
	const FontFamilyStyle mono{"Noto Sans Mono", "Regular", 3, BFontFace::Regular, true};

	// When
	const std::vector<SourceFont> fonts = (*source)->List();
	const auto han = (*source)->Fallback(U'日', sans);
	const auto hangul = (*source)->Fallback(U'한', sans);
	const auto thai = (*source)->Fallback(U'ก', sans);
	const auto latin = (*source)->Fallback(U'A', mono);
	const auto missing = CreateFontconfigSource(config.File() + ".missing");

	// Then
	const SourceFont* jp = Find(fonts, "Noto Sans CJK JP", "Regular");
	const SourceFont* kr = Find(fonts, "Noto Sans CJK KR", "Regular");
	const SourceFont* bold = Find(fonts, "Noto Sans", "Bold");
	const SourceFont* fixed = Find(fonts, "Noto Sans Mono", "Regular");
	ASSERT_NE(jp, nullptr);
	ASSERT_NE(kr, nullptr);
	ASSERT_NE(bold, nullptr);
	ASSERT_NE(fixed, nullptr);
	EXPECT_EQ(jp->location, (FontLocation{kCollection, 0}));
	EXPECT_EQ(kr->location, (FontLocation{kCollection, 1}));
	EXPECT_TRUE(bold->face.Has(BFontFace::Bold));
	EXPECT_TRUE(fixed->face.Has(BFontFace::Regular));
	ASSERT_TRUE(han.has_value());
	EXPECT_EQ(*han, (FontLocation{kCollection, 0}));
	ASSERT_TRUE(hangul.has_value());
	EXPECT_EQ(*hangul, (FontLocation{kCollection, 1}));
	EXPECT_FALSE(thai.has_value());
	ASSERT_TRUE(latin.has_value());
	EXPECT_EQ(latin->path, kMono);
	ASSERT_FALSE(missing.has_value());
	EXPECT_EQ(missing.error().code, ErrorCode::IoError);
}


TEST(FontconfigSourceTest, ATextSystemOffersTheSystemsFamilies)
{
	// What is tested: A FontTextSystem over that source: Styles(), Style()
	// of the KR face, its text shaped, "a日" in the plain role, and a BFont
	// set to the KR face with the text system as default. Skipped without
	// fontconfig.
	// Success criteria: Styles() begins with the six bundled styles and
	// lists the CJK faces after them, the bundled "Noto Sans" "Regular" only
	// once. The KR face has a FontId of its own, loaded only when used: then
	// its text is one run in it (the Hangul glyph is no missing glyph) and
	// Fonts() has it with face index 1. 日 falls back to the listed JP face,
	// the same FontId. The BFont measures 한 with the KR face: its 920
	// units, 11.04 px, as the shaped text.
	// Why it matters: Applications choose the system's fonts by family and
	// style, as with Haiku's get_font_family() and BFont (module X1).
	//
	// Given
	// the bundled Noto as the role fonts: a look that names none
	const test::ScopedLook noto("haiku");
	const TestConfig config;
	auto source = CreateFontconfigSource(config.File());
	if (!source.has_value() && source.error().code == ErrorCode::NotSupported)
		GTEST_SKIP() << source.error().message;
	ASSERT_TRUE(source.has_value()) << source.error().message;
	const auto text = std::move(*FontTextSystem::Create(std::move(*source)));

	// When
	const std::vector<FontFamilyStyle> styles = text->Styles();
	const std::optional<FontFamilyStyle> kr = text->Style("Noto Sans CJK KR", "Regular");
	const std::optional<FontFamilyStyle> jp = text->Style("Noto Sans CJK JP", "Regular");
	const std::size_t fontsBefore = text->Fonts().size();
	const ShapedText hangul = text->Shape("한", FontSpec{kr.value_or(styles[0]).font, 12});
	const ShapedText mixed = text->Shape("a日", FontRole::Plain);
	const std::vector<FontFile> fonts = text->Fonts();
	TextSystem::SetDefault(text.get());
	BFont font;
	const bool chosen = font.SetFamilyAndStyle("Noto Sans CJK KR", "Regular").has_value();
	const float width = font.StringWidth("한");
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_GE(styles.size(), 8u);
	EXPECT_EQ(styles[0].family, "Noto Sans");
	EXPECT_EQ(styles[2].family, "Noto Sans Mono");
	int sansRegular = 0;
	for (const FontFamilyStyle& style : styles) {
		if (style.family == "Noto Sans" && style.style == "Regular")
			sansRegular++;
	}
	EXPECT_EQ(sansRegular, 1);
	ASSERT_TRUE(kr.has_value());
	ASSERT_TRUE(jp.has_value());
	EXPECT_GE(kr->font, 7u);
	EXPECT_NE(kr->font, jp->font);
	EXPECT_EQ(fontsBefore, 6u);
	EXPECT_EQ(hangul.font, kr->font);
	EXPECT_TRUE(hangul.fontRuns.empty());
	ASSERT_EQ(hangul.glyphs.size(), 1u);
	EXPECT_NE(hangul.glyphs[0], 0u);
	ASSERT_EQ(mixed.fontRuns.size(), 2u);
	EXPECT_EQ(mixed.fontRuns[1].font, jp->font);
	ASSERT_EQ(fonts.size(), 8u);
	EXPECT_EQ(fonts[6].id, kr->font);
	EXPECT_EQ(fonts[6].faceIndex, 1u);
	EXPECT_TRUE(chosen);
	EXPECT_NEAR(width, 11.04f, 1e-3f);
	EXPECT_NEAR(width, hangul.advance, 1e-3f);
}


} // namespace
