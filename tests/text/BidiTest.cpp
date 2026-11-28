/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/InterfaceDefs.h>
#include <ngui/TextLayout.h>
#include <ngui/TextView.h>
#include <ngui/gfx/Region.h>
#include <ngui/text/FontSource.h>

#include "support/ScopedLook.h"


namespace {


using namespace ngui;
using namespace ngui::text;


constexpr float kTolerance = 1e-3f;

// Hebrew and Arabic come from the test fonts (tests/data/fonts/README.md).
class RightToLeftSource : public FontSource {
public:
	std::optional<FontLocation> Fallback(char32_t codePoint,
		const FontFamilyStyle&) const override
	{
		const std::string directory(NGUI_TEST_FONTS_DIR);
		if (codePoint >= 0x0590 && codePoint <= 0x05ff)
			return FontLocation{directory + "/NotoSansHebrew-Subset.ttf", 0};
		if (codePoint >= 0x0600 && codePoint <= 0x06ff)
			return FontLocation{directory + "/NotoSansArabic-Subset.ttf", 0};
		return std::nullopt;
	}
};


std::unique_ptr<FontTextSystem>
WithRightToLeft()
{
	return std::move(*FontTextSystem::Create(std::make_unique<RightToLeftSource>()));
}


// "שלום" (shalom): four Hebrew letters of two bytes each.
constexpr const char* kShalom = "שלום";
// "مرحبا" (marhaban): five Arabic letters of two bytes each.
constexpr const char* kMarhaban = "مرحبا";


TEST(BidiTest, ParagraphsAndLinesFollowTheBidirectionalAlgorithm)
{
	// What is tested: ParagraphDirection() and VisualRuns() of the text
	// subsystem for Latin and Hebrew in both orders, digits before Hebrew,
	// text without strong characters, and an empty line.
	// Success criteria: A paragraph takes the direction of its first strong
	// character ("123 שלום" right to left, "(!)" left to right). In the
	// left-to-right "abc שלום def" the runs are "abc " (L), the Hebrew (R)
	// and " def" (L), in that order; in the right-to-left "שלום abc" the
	// Latin run comes first from the left, then the space and the Hebrew,
	// as one right-to-left run. A line without right-to-left characters is
	// one left-to-right run; an empty line has that one empty run.
	// Why it matters: UAX #9 decides where each piece of mixed text stands
	// (module X3); its paragraph direction is automatic, as decided.
	//
	// Given
	const auto text = WithRightToLeft();
	const std::string ltr = std::string("abc ") + kShalom + " def";
	const std::string rtl = std::string(kShalom) + " abc";

	// When / Then
	EXPECT_EQ(text->ParagraphDirection(rtl), TextDirection::RightToLeft);
	EXPECT_EQ(text->ParagraphDirection(ltr), TextDirection::LeftToRight);
	EXPECT_EQ(text->ParagraphDirection(std::string("123 ") + kShalom),
		TextDirection::RightToLeft);
	EXPECT_EQ(text->ParagraphDirection("(!)"), TextDirection::LeftToRight);
	EXPECT_EQ(text->VisualRuns(ltr, 0, ltr.size()), (std::vector<DirectionRun>{
		{0, 4, TextDirection::LeftToRight}, {4, 12, TextDirection::RightToLeft},
		{12, 16, TextDirection::LeftToRight}}));
	EXPECT_EQ(text->VisualRuns(rtl, 0, rtl.size()), (std::vector<DirectionRun>{
		{9, 12, TextDirection::LeftToRight}, {0, 9, TextDirection::RightToLeft}}));
	EXPECT_EQ(text->VisualRuns("plain", 0, 5), (std::vector<DirectionRun>{
		{0, 5, TextDirection::LeftToRight}}));
	EXPECT_EQ(text->VisualRuns(rtl, 4, 4), (std::vector<DirectionRun>{
		{4, 4, TextDirection::LeftToRight}}));
}


TEST(BidiTest, ShapingOrdersMirrorsAndJoins)
{
	// What is tested: Shape() of "ab שלום", of "ש(א)" (a right-to-left
	// paragraph with brackets), of "مرحبا", and ShapeRun() of "ab" right to
	// left.
	// Success criteria: "ab שלום" has its glyphs in visual order: a, b, the
	// space, then the Hebrew letters last first (clusters 0 1 2 9 7 5 3),
	// the Hebrew in the fallback font with real glyphs. In "ש(א)" the
	// brackets are mirrored: the logical ")" is drawn as "(" at the left.
	// In "مرحبا" the first letter, drawn rightmost, joins: its glyph is not
	// the isolated م. ShapeRun() of "ab" right to left puts b before a, as
	// wide as left to right.
	// Why it matters: Right-to-left text must read right to left, with its
	// brackets turned and its letters joined (module X3).
	//
	// Given
	// the bundled Noto as the role fonts: a look that names none
	const test::ScopedLook noto("haiku");
	const auto text = WithRightToLeft();

	// When
	const ShapedText mixed = text->Shape(std::string("ab ") + kShalom, FontRole::Plain);
	const ShapedText brackets = text->Shape("ש(א)", FontRole::Plain);
	const ShapedText open = text->Shape("(", FontRole::Plain);
	const ShapedText close = text->Shape(")", FontRole::Plain);
	const ShapedText word = text->Shape(kMarhaban, FontRole::Plain);
	const ShapedText isolated = text->Shape("م", FontRole::Plain);
	const ShapedText reversed = text->ShapeRun("ab", text->RoleFont(FontRole::Plain),
		TextDirection::RightToLeft);
	const ShapedText forward = text->ShapeRun("ab", text->RoleFont(FontRole::Plain),
		TextDirection::LeftToRight);

	// Then
	EXPECT_EQ(mixed.clusters, (std::vector<std::uint32_t>{0, 1, 2, 9, 7, 5, 3}));
	ASSERT_EQ(mixed.fontRuns.size(), 2u);
	EXPECT_EQ(mixed.fontRuns[0].end, 3u);
	EXPECT_GE(mixed.fontRuns[1].font, 4u);
	for (std::size_t g = 3; g < mixed.glyphs.size(); g++)
		EXPECT_NE(mixed.glyphs[g], 0u);
	EXPECT_EQ(brackets.clusters, (std::vector<std::uint32_t>{5, 3, 2, 0}));
	ASSERT_EQ(brackets.glyphs.size(), 4u);
	EXPECT_EQ(brackets.glyphs[0], open.glyphs[0]);
	EXPECT_EQ(brackets.glyphs[2], close.glyphs[0]);
	ASSERT_FALSE(word.glyphs.empty());
	EXPECT_EQ(word.clusters.back(), 0u);
	EXPECT_NE(word.glyphs.back(), isolated.glyphs[0]);
	EXPECT_EQ(reversed.clusters, (std::vector<std::uint32_t>{1, 0}));
	EXPECT_NEAR(reversed.advance, forward.advance, kTolerance);
}


TEST(BidiTest, LayoutPutsCaretsWhereTheCharactersAre)
{
	// What is tested: A TextLayout of "abc שלום" and of the right-to-left
	// "שלום", left aligned, and the carets around the right-to-left run.
	// Success criteria: In "abc שלום" the caret before ש, where the space
	// meets the Hebrew word, stands after the space, at the word's left
	// edge (the "strong" caret, on the side of the paragraph's direction);
	// the carets before ל, ו, ם step to the left from the word's right
	// edge, all right of it; the caret after the text stands at the line's
	// right end, where typing goes on. The right-to-left paragraph stays
	// left aligned (alignment is absolute, as in Haiku): the caret before
	// its first letter is at its right end, after its last at 0 (in whole
	// pixels, as PointAt() gives them).
	// Why it matters: The caret follows the characters on the screen, not
	// their order in memory (module X3).
	//
	// Given
	const auto text = WithRightToLeft();
	const std::string mixedText = std::string("abc ") + kShalom;
	const TextLayout mixed(*text, mixedText, FontRole::Plain, {.width = 500});
	const TextLayout hebrew(*text, kShalom, FontRole::Plain, {.width = 500});

	// When
	const float lineWidth = mixed.LineAt(0).width;
	const float afterAbc = mixed.PointAt(3).x;
	const float beforeShin = mixed.PointAt(4).x;
	const float beforeLamed = mixed.PointAt(6).x;
	const float beforeVav = mixed.PointAt(8).x;
	const float beforeMem = mixed.PointAt(10).x;
	const float atEnd = mixed.PointAt(12).x;
	const float word = text->StringWidth(kShalom, FontRole::Plain);

	// Then
	EXPECT_GT(beforeShin, afterAbc);
	EXPECT_NEAR(beforeShin, lineWidth - word, 0.5f);
	EXPECT_GT(beforeLamed, beforeVav);
	EXPECT_GT(beforeVav, beforeMem);
	EXPECT_GT(beforeMem, beforeShin);
	EXPECT_NEAR(atEnd, lineWidth, 0.5f);
	EXPECT_NEAR(hebrew.LineAt(0).width, word, kTolerance);
	EXPECT_NEAR(hebrew.PointAt(0).x, word, 0.5f);
	EXPECT_NEAR(hebrew.PointAt(8).x, 0, 0.5f);
}


// The offsets the caret goes through from \a from, moving with \a move
// until it stays.
template<typename Move>
std::vector<std::size_t>
Walk(std::size_t from, Move move)
{
	std::vector<std::size_t> offsets;
	for (std::size_t offset = from, steps = 0; steps < 64; steps++) {
		const std::size_t next = move(offset);
		if (next == offset)
			break;
		offsets.push_back(next);
		offset = next;
	}
	return offsets;
}


TEST(BidiTest, CaretsMoveHitAndSelectVisually)
{
	// What is tested: In the TextLayout of "abc שלום def" (left to right):
	// CaretRight() from 0 and CaretLeft() from the end until they stay;
	// OffsetAt() in the left half of ש and just left of the word's right
	// edge; SelectionRects() of "c ש" (offsets 2 to 6).
	// Success criteria: The caret moves over one grapheme on the screen at
	// a time: right through "abc ", then into the Hebrew word at its left
	// edge (offset 4) and over ם, ו, ל (10, 8, 6), out at its right edge
	// as offset 12, then through " def"; left the same way back. A click in
	// ש's left half hits offset 6, one at the word's right edge offset 12.
	// The selection of "c ש" is two rectangles: "c " on the left and ש at
	// the right end of the word.
	// Why it matters: Arrow keys, clicks and selections in mixed text must
	// follow what the user sees (module X3).
	//
	// Given
	const auto text = WithRightToLeft();
	const std::string content = std::string("abc ") + kShalom + " def";
	const TextLayout layout(*text, content, FontRole::Plain, {.width = 500});

	// When
	const std::vector<std::size_t> right
		= Walk(0, [&](std::size_t offset) { return layout.CaretRight(offset); });
	const std::vector<std::size_t> left
		= Walk(content.size(), [&](std::size_t offset) { return layout.CaretLeft(offset); });
	const float y = layout.PointAt(0).y + 1;
	const std::size_t inShin = layout.OffsetAt({layout.PointAt(6).x + 1, y});
	const std::size_t atWordEnd = layout.OffsetAt({layout.PointAt(12).x - 1, y});
	const std::vector<gfx::Rect> rects = layout.SelectionRects(2, 6);

	// Then
	EXPECT_TRUE(layout.HasRightToLeft());
	EXPECT_EQ(right, (std::vector<std::size_t>{1, 2, 3, 4, 10, 8, 6, 12, 13, 14, 15, 16}));
	EXPECT_EQ(left, (std::vector<std::size_t>{15, 14, 13, 12, 6, 8, 10, 4, 3, 2, 1, 0}));
	EXPECT_EQ(inShin, 6u);
	EXPECT_EQ(atWordEnd, 12u);
	ASSERT_EQ(rects.size(), 2u);
	EXPECT_NEAR(rects[0].x0, layout.PointAt(2).x, 0.5f);
	EXPECT_NEAR(rects[0].x1, layout.PointAt(4).x, 0.5f);
	EXPECT_NEAR(rects[1].x1, layout.PointAt(12).x, 0.5f);
	EXPECT_GT(rects[1].x0, rects[0].x1);
}


TEST(BidiTest, TextViewsMoveAndHighlightVisually)
{
	// What is tested: A BTextView with "abc שלום" and the bundled fonts with
	// the Hebrew test font: the right arrow pressed three times from offset
	// 3, the left arrow then twice; the text region of offsets 2 to 6.
	// Success criteria: The caret goes 4 (into the word at its left edge),
	// 10, 8, then back 10, 4; the region of "c ש" has two rectangles. A text
	// without right-to-left characters keeps Haiku's moves (tested
	// elsewhere).
	// Why it matters: Text views are where users edit mixed text (module
	// X3).
	//
	// Given
	const auto text = WithRightToLeft();
	TextSystem::SetDefault(text.get());
	BTextView view({0, 0, 300, 50}, "view", {0, 0, 300, 50});
	view.SetText((std::string("abc ") + kShalom).c_str());
	view.Select(3, 3);

	// When
	std::vector<int32> carets;
	const auto press = [&](char key) {
		view.KeyDown(std::string(1, key));
		int32 start = 0;
		int32 end = 0;
		view.GetSelection(&start, &end);
		carets.push_back(start == end ? start : -1);
	};
	for (int i = 0; i < 3; i++)
		press(keys::kRightArrow);
	for (int i = 0; i < 2; i++)
		press(keys::kLeftArrow);
	gfx::Region region;
	view.GetTextRegion(2, 6, &region);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(carets, (std::vector<int32>{4, 10, 8, 10, 4}));
	EXPECT_EQ(region.CountRects(), 2);
}


} // namespace
