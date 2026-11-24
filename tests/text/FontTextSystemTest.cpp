/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Font.h>
#include <ngui/TextLayout.h>


namespace {


using namespace ngui;
using namespace ngui::text;


// The expected values come from the font files themselves, read with
// fontTools (hhea/OS/2 and hmtx): Noto Sans has 1000 units per em, an
// ascender of 1069 and a descender of 293; the advances are O 781, K 619,
// A 639, V 600 and "…" 791 (bold: O 791, K 664; mono: 600 each).
constexpr float kTolerance = 1e-3f;


std::unique_ptr<FontTextSystem>
Bundled(float size = 12)
{
	auto text = FontTextSystem::CreateBundled(size);
	EXPECT_TRUE(text.has_value());
	return text.has_value() ? std::move(*text) : nullptr;
}


TEST(FontTextSystemTest, MetricsComeFromTheBundledFonts)
{
	// What is tested: FontSize(), Height() and StringWidth() of the bundled
	// fonts at 12 px for the three roles, and at 24 px.
	// Success criteria: Ascent 12.828 and descent 3.516 (1069 and 293 units
	// at 12/1000 px), leading 0; "OK" is (781 + 619) · 0.012 = 16.8 wide in
	// Noto Sans, (791 + 664) · 0.012 = 17.46 in the bold face, 2 · 7.2 in
	// Noto Sans Mono; everything doubles at 24 px.
	// Why it matters: Labels are laid out and centered with these numbers
	// (the BeOS look, BLayoutUtils::AlignOnRect()); they must be the
	// font's, the same on every system (specification 20.3).
	//
	// Given
	const auto text = Bundled();
	const auto large = Bundled(24);

	// When
	const FontHeight height = text->Height(FontRole::Plain);

	// Then
	EXPECT_EQ(text->FontSize(FontRole::Plain), 12);
	EXPECT_NEAR(height.ascent, 12.828f, kTolerance);
	EXPECT_NEAR(height.descent, 3.516f, kTolerance);
	EXPECT_NEAR(height.leading, 0, kTolerance);
	EXPECT_NEAR(text->StringWidth("OK", FontRole::Plain), 16.8f, kTolerance);
	EXPECT_NEAR(text->StringWidth("OK", FontRole::Bold), 17.46f, kTolerance);
	EXPECT_NEAR(text->StringWidth("OK", FontRole::Fixed), 14.4f, kTolerance);
	EXPECT_NEAR(large->StringWidth("OK", FontRole::Plain), 33.6f, kTolerance);
	EXPECT_NEAR(large->Height(FontRole::Plain).ascent, 25.656f, kTolerance);
	EXPECT_EQ(text->StringWidth("", FontRole::Plain), 0);
	EXPECT_EQ(FontTextSystem::CreateBundled(0).error().code, ErrorCode::InvalidArgument);
}


TEST(FontTextSystemTest, ShapingGivesGlyphsPositionsAndClusters)
{
	// What is tested: Shape() of "OK", of "AV" and of "Aé".
	// Success criteria: "OK" has the glyphs 50 and 46 of the font at x 0
	// and 9.372 (781 · 0.012) on the baseline, the plain font's id and the
	// size; "AV" is narrower than its two advances (kerning); the clusters
	// of "Aé" are the byte offsets 0 and 1, and the text is kept.
	// Why it matters: The Canvas draws the glyph run as it is (specification
	// 10); the clusters let text views map glyphs back to the text (M21).
	//
	// Given
	const auto text = Bundled();

	// When
	const ShapedText ok = text->Shape("OK", FontRole::Plain);
	const ShapedText av = text->Shape("AV", FontRole::Plain);
	const ShapedText accent = text->Shape("A\xC3\xA9", FontRole::Plain);

	// Then
	EXPECT_EQ(ok.glyphs, (std::vector<std::uint32_t>{50, 46}));
	ASSERT_EQ(ok.positions.size(), 2u);
	EXPECT_NEAR(ok.positions[0].x, 0, kTolerance);
	EXPECT_NEAR(ok.positions[1].x, 9.372f, kTolerance);
	EXPECT_NEAR(ok.positions[1].y, 0, kTolerance);
	EXPECT_EQ(ok.font, text->Fonts()[0].id);
	EXPECT_EQ(ok.sizePx, 12);
	EXPECT_NEAR(ok.advance, 16.8f, kTolerance);
	EXPECT_LT(av.advance, (639 + 600) * 0.012f - 0.01f);
	EXPECT_EQ(accent.clusters, (std::vector<std::uint32_t>{0, 1}));
	// gtest has no printer for char8_t strings; compared as a condition.
	EXPECT_TRUE(accent.text == u8"A\u00e9");
}


TEST(FontTextSystemTest, TruncationUsesTheCharactersAdvances)
{
	// What is tested: Truncate() of "OK OK OK" at 20 px from the end, of a
	// text that fits, and the font files the system hands to backends.
	// Success criteria: With O 9.372, K 7.428, space 3.12 and "…" 9.492 px,
	// Haiku's algorithm keeps "O…" at 20 px; a text that fits stays; the
	// three fonts have the ids 1 to 3, their roles and non-empty files.
	// Why it matters: X5 asks for Haiku's truncation with the font's
	// metrics; backends need the same files to draw the glyphs (13.4).
	//
	// Given
	const auto text = Bundled();

	// When / Then
	EXPECT_EQ(text->Truncate("OK OK OK", 20, TruncationMode::End, FontRole::Plain),
		"O…");
	EXPECT_EQ(text->Truncate("OK", 20, TruncationMode::End, FontRole::Plain), "OK");
	const auto fonts = text->Fonts();
	ASSERT_EQ(fonts.size(), 3u);
	EXPECT_EQ(fonts[0].id, 1u);
	EXPECT_EQ(fonts[1].role, FontRole::Bold);
	EXPECT_EQ(fonts[2].role, FontRole::Fixed);
	for (const FontFile& font : fonts)
		EXPECT_GT(font.data.size(), 100000u);
}


TEST(FontTextSystemTest, ItIsSafeFromManyThreads)
{
	// What is tested: Four threads shape and measure with one text system at
	// once, 500 times each.
	// Success criteria: Every result equals the one of a single thread.
	// Why it matters: Windows lay out and draw on threads of their own,
	// all with the one text system of the process.
	//
	// Given
	const auto text = Bundled();
	const float expected = text->StringWidth("Hello, NewGUI", FontRole::Plain);
	std::atomic<int> wrong = 0;

	// When
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; t++) {
		threads.emplace_back([&] {
			for (int i = 0; i < 500; i++) {
				if (text->StringWidth("Hello, NewGUI", FontRole::Plain) != expected)
					wrong++;
			}
		});
	}
	for (std::thread& thread : threads)
		thread.join();

	// Then
	EXPECT_EQ(wrong, 0);
}


// The offsets with \a kind in \a flags.
std::vector<std::size_t>
With(const std::vector<TextBoundaryFlags>& flags, TextBoundary kind)
{
	std::vector<std::size_t> offsets;
	for (std::size_t i = 0; i < flags.size(); i++) {
		if (flags[i].Has(kind))
			offsets.push_back(i);
	}
	return offsets;
}


TEST(FontTextSystemTest, BoundariesFollowUnicode)
{
	// What is tested: Boundaries() of "e\u0301x" (e with a combining
	// acute), of a woman technologist emoji (woman, ZWJ, laptop: 11 bytes),
	// of "日本語" and of "hello, world\nok", and of malformed UTF-8 ("a",
	// a lone continuation byte, "b").
	// Success criteria: The combining accent belongs to the e (graphemes at
	// 0, 3, 4); the emoji sequence is one grapheme; lines may break between
	// the ideographs (3, 6) but not after the last; "hello", ",", " ",
	// "world" are words and a line may begin at "world"; the line break is
	// mandatory. In malformed text every code point start is a grapheme
	// boundary; a lone continuation byte stays with the character before.
	// Why it matters: The caret moves by what users see as characters, and
	// lines break where Unicode allows (libunibreak, UAX #14 and #29).
	//
	// Given
	const auto text = Bundled();

	// When
	const auto combining = text->Boundaries("e\xCC\x81x");
	const auto emoji = text->Boundaries("\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x92\xBB");
	const auto ideographs = text->Boundaries("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
	const auto words = text->Boundaries("hello, world\nok");
	const auto malformed = text->Boundaries("a\x80" "b");

	// Then
	EXPECT_EQ(With(combining, TextBoundary::Grapheme), (std::vector<std::size_t>{0, 3, 4}));
	EXPECT_EQ(With(emoji, TextBoundary::Grapheme), (std::vector<std::size_t>{0, 11}));
	EXPECT_EQ(With(ideographs, TextBoundary::Line), (std::vector<std::size_t>{3, 6}));
	EXPECT_EQ(With(words, TextBoundary::Word),
		(std::vector<std::size_t>{0, 5, 6, 7, 12, 13, 15}));
	EXPECT_EQ(With(words, TextBoundary::Line), (std::vector<std::size_t>{7, 13}));
	EXPECT_EQ(With(words, TextBoundary::MandatoryLine), (std::vector<std::size_t>{13}));
	EXPECT_EQ(With(malformed, TextBoundary::Grapheme), (std::vector<std::size_t>{0, 2, 3}));
}


TEST(FontTextSystemTest, TextIsLaidOutWithTheShapedWidths)
{
	// What is tested: A TextLayout of "The quick brown fox jumps" in the
	// bundled plain font wrapped at 80 pixels, and the caret positions of
	// "AV".
	// Success criteria: The text takes several lines, each beginning at a
	// word and no wider than 79 without its trailing space; the carets of
	// "AV" are 0, the kerned position of V, and the run's advance; the
	// caret after the last character of a line is where its glyphs end.
	// Why it matters: The layout measures with the font's real (kerned)
	// advances, so carets sit between the drawn glyphs.
	//
	// Given
	const auto text = Bundled();
	const std::string_view sentence = "The quick brown fox jumps";

	// When
	const TextLayout layout(*text, sentence, FontRole::Plain, {.width = 80});
	const std::vector<float> carets = text->CaretPositions("AV", FontRole::Plain);
	const ShapedText av = text->Shape("AV", FontRole::Plain);

	// Then
	ASSERT_GT(layout.CountLines(), 1);
	for (int32 i = 0; i < layout.CountLines(); i++) {
		const TextLayout::Line& line = layout.LineAt(i);
		EXPECT_TRUE(line.start == 0 || sentence[line.start - 1] == ' ') << i;
		std::size_t visibleEnd = line.end;
		while (visibleEnd > line.start && sentence[visibleEnd - 1] == ' ')
			visibleEnd--;
		EXPECT_LT(layout.PointAt(visibleEnd).x - layout.PointAt(line.start).x, 80) << i;
	}
	ASSERT_EQ(carets.size(), 3u);
	EXPECT_EQ(carets[0], 0);
	EXPECT_NEAR(carets[1], av.positions[1].x, kTolerance);
	EXPECT_NEAR(carets[2], av.advance, kTolerance);
}


TEST(FontTextSystemTest, FontsOfAnySizeAndTheirStyles)
{
	// What is tested: The bundled text system at 12: its styles; Noto
	// Sans Bold as a BFont, at 12 and at 24: Spec(), the width and height
	// of "Haiku", the run of "Haiku" and the truncation of "Haiku is
	// here" to 100 pixels at 24.
	// Success criteria: "Noto Sans" has Regular (font 1) and Bold (font 2),
	// "Noto Sans Mono" Regular (font 3, monospaced). At 24 the bold font
	// is font 2 at 24; its width and metrics are twice those at 12 and its
	// run says 24 pixels; truncation to 100 pixels at 24 cuts as to 50 at
	// 12.
	// Why it matters: BFont names the bundled fonts as Haiku names its
	// system fonts, and the text system measures and shapes any size, not
	// only the roles' (W7).
	//
	// Given
	const auto text = Bundled();
	TextSystem::SetDefault(text.get());
	BFont font;
	ASSERT_TRUE(font.SetFamilyAndStyle("Noto Sans", "Bold").has_value());
	const float width12 = font.StringWidth("Haiku");
	FontHeight height12{};
	font.GetHeight(&height12);

	// When
	font.SetSize(24);
	const FontSpec spec = font.Spec();
	const float width24 = font.StringWidth("Haiku");
	FontHeight height24{};
	font.GetHeight(&height24);
	const ShapedText shaped = text->Shape("Haiku", spec);
	std::string truncated = "Haiku is here";
	font.TruncateString(&truncated, TruncationMode::End, 100);

	// Then
	const std::vector<FontFamilyStyle> styles = text->Styles();
	ASSERT_EQ(styles.size(), 3u);
	EXPECT_EQ(styles[0].family, "Noto Sans");
	EXPECT_EQ(styles[1].style, "Bold");
	EXPECT_EQ(styles[1].font, 2u);
	EXPECT_TRUE(styles[2].fixed);
	EXPECT_EQ(BFont::Families(), (std::vector<std::string>{"Noto Sans", "Noto Sans Mono"}));
	EXPECT_EQ(spec.font, 2u);
	EXPECT_EQ(spec.size, 24);
	EXPECT_FLOAT_EQ(width24, 2 * width12);
	EXPECT_FLOAT_EQ(height24.ascent, 2 * height12.ascent);
	EXPECT_FLOAT_EQ(height24.descent, 2 * height12.descent);
	EXPECT_EQ(shaped.font, 2u);
	EXPECT_EQ(shaped.sizePx, 24);
	EXPECT_FLOAT_EQ(shaped.advance, width24);
	EXPECT_EQ(truncated, text->Truncate("Haiku is here", 50, TruncationMode::End,
		FontRole::Bold));
	EXPECT_NE(truncated, "Haiku is here");
	TextSystem::SetDefault(nullptr);
}


} // namespace
