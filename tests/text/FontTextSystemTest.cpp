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


} // namespace
