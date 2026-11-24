/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextLayout.h>

#include <array>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using ngui::test::FixedTextSystem;


// FixedTextSystem with a taller bold font: ascent 15.
class TallBold : public FixedTextSystem {
public:
	FontHeight Height(FontRole role) const override
	{
		return role == FontRole::Bold ? FontHeight{15, 3, 1} : FontHeight{9, 3, 1};
	}
};


// The [start, end) of each line.
std::vector<std::pair<std::size_t, std::size_t>>
Lines(const TextLayout& layout)
{
	std::vector<std::pair<std::size_t, std::size_t>> lines;
	for (int32 i = 0; i < layout.CountLines(); i++)
		lines.emplace_back(layout.LineAt(i).start, layout.LineAt(i).end);
	return lines;
}


using Ranges = std::vector<std::pair<std::size_t, std::size_t>>;


TEST(TextLayoutTest, LinesWrapAsInHaikusTextView)
{
	// What is tested: "Hello world foo" (7 px per byte, ascent 9, descent
	// 3, leading 1) wrapped at a width of 50; "abcdefghij" at a width of 30;
	// "ab\n\ncd\n" without wrapping.
	// Success criteria: As Haiku's _FindLineBreak() with its width of 49:
	// "Hello " fits (the trailing space hangs over), "world " does not, so
	// the lines are "Hello ", "world ", "foo"; each line is
	// ceil(9 + 3 + 1) + 1 = 14 high, 42 in all; a line's width counts its
	// trailing space (Haiku's LineWidth()). A word wider than the line is
	// broken where the next grapheme reaches the width: 4 bytes (28 px) per
	// line. Line breaks end lines; a final one is followed by an empty line.
	// Why it matters: BTextView lays its text out like Haiku's, so text
	// wraps at the same places.
	//
	// Given
	const FixedTextSystem text;

	// When
	const TextLayout wrapped(text, "Hello world foo", FontRole::Plain, {.width = 50});
	const TextLayout word(text, "abcdefghij", FontRole::Plain, {.width = 30});
	const TextLayout breaks(text, "ab\n\ncd\n", FontRole::Plain, {.wrap = false});

	// Then
	EXPECT_EQ(Lines(wrapped), (Ranges{{0, 6}, {6, 12}, {12, 15}}));
	EXPECT_EQ(wrapped.LineAt(1).top, 14);
	EXPECT_EQ(wrapped.LineAt(2).top, 28);
	EXPECT_EQ(wrapped.Height(), 42);
	EXPECT_EQ(wrapped.LineAt(0).width, 42);
	EXPECT_EQ(wrapped.LineAt(2).width, 21);
	EXPECT_EQ(wrapped.Baseline(1), 14 + 9 + 1);
	EXPECT_EQ(Lines(word), (Ranges{{0, 4}, {4, 8}, {8, 10}}));
	EXPECT_EQ(Lines(breaks), (Ranges{{0, 3}, {3, 4}, {4, 7}, {7, 7}}));
	EXPECT_EQ(breaks.LineAt(0).width, 14);
	EXPECT_EQ(breaks.Height(), 56);
}


TEST(TextLayoutTest, PointsAndOffsetsAsInHaikusTextView)
{
	// What is tested: PointAt() and OffsetAt() of "Hello world foo" wrapped
	// at 50, and of "ab\n" (no wrapping).
	// Success criteria: The caret after "H" is at (7, 0) with the line's
	// height 14; the offset where a line wraps belongs to the next line,
	// (0, 14). A point hits the nearer side of a character; right of a
	// wrapped line it gives the offset before the hanging space, right of a
	// line with a line break the offset before it; above the text 0, below
	// it the end. The empty last line after "ab\n" has its caret at (0, 14),
	// as high as the font without leading (Haiku's _NullStyleHeight(),
	// ceil(9 + 3 + 1)).
	// Why it matters: Clicks place the caret and the caret is drawn where
	// Haiku's would be.
	//
	// Given
	const FixedTextSystem text;
	const TextLayout layout(text, "Hello world foo", FontRole::Plain, {.width = 50});
	const TextLayout breaks(text, "ab\n", FontRole::Plain, {.wrap = false});
	float height = 0;
	float lastHeight = 0;

	// When
	const gfx::Point afterH = layout.PointAt(1, &height);
	const gfx::Point wrapped = layout.PointAt(6);
	const gfx::Point last = breaks.PointAt(3, &lastHeight);

	// Then
	EXPECT_EQ(afterH, (gfx::Point{7, 0}));
	EXPECT_EQ(height, 14);
	EXPECT_EQ(wrapped, (gfx::Point{0, 14}));
	EXPECT_EQ(layout.OffsetAt({10, 5}), 1u);
	EXPECT_EQ(layout.OffsetAt({12, 5}), 2u);
	EXPECT_EQ(layout.OffsetAt({200, 5}), 5u);
	EXPECT_EQ(layout.OffsetAt({200, 20}), 11u);
	EXPECT_EQ(layout.OffsetAt({200, 30}), 15u);
	EXPECT_EQ(layout.OffsetAt({5, -1}), 0u);
	EXPECT_EQ(layout.OffsetAt({5, 100}), 15u);
	EXPECT_EQ(breaks.OffsetAt({100, 5}), 2u);
	EXPECT_EQ(breaks.LineAtOffset(3), 1);
	EXPECT_EQ(last, (gfx::Point{0, 14}));
	EXPECT_EQ(lastHeight, 13);
}


TEST(TextLayoutTest, AlignmentTabsAndRoles)
{
	// What is tested: "ab" centered and right aligned in a width of 100;
	// "a\tb" with tab stops every 28; "ab cd" with "cd" in a taller bold
	// font; a selection across two lines of "Hello world foo".
	// Success criteria: Centered, the line moves right by round((100 -
	// 14) / 2) = 43, right aligned by 86, and points and hits follow it. The
	// tab after "a" (7) reaches the stop at 28. The bold run raises the
	// line's ascent to 15, its height to ceil(15 + 4) + 1 = 20. The
	// selection from 3 to 9 is one rectangle per line: (21, 0, 42, 14) and
	// (0, 14, 21, 28).
	// Why it matters: Text views align their lines, expand tabs and mix
	// styles as in Haiku.
	//
	// Given
	const TallBold text;
	const std::array<TextStyleRun, 2> runs = {TextStyleRun{0, text.RoleFont(FontRole::Plain)},
		TextStyleRun{3, text.RoleFont(FontRole::Bold)}};

	// When
	const TextLayout centered(text, "ab", FontRole::Plain,
		{.width = 100, .alignment = BHorizontalAlignment::Center});
	const TextLayout right(text, "ab", FontRole::Plain,
		{.width = 100, .alignment = BHorizontalAlignment::Right});
	const TextLayout tabs(text, "a\tb", FontRole::Plain, {.wrap = false});
	const TextLayout styled(text, "ab cd", runs, {.wrap = false});
	const TextLayout wrapped(text, "Hello world foo", FontRole::Plain, {.width = 50});

	// Then
	EXPECT_EQ(centered.PointAt(0).x, 43);
	EXPECT_EQ(centered.OffsetAt({50, 5}), 1u);
	EXPECT_EQ(right.PointAt(2).x, 100);
	EXPECT_EQ(tabs.PointAt(2).x, 28);
	EXPECT_EQ(tabs.PointAt(3).x, 35);
	EXPECT_EQ(styled.LineAt(0).ascent, 15);
	EXPECT_EQ(styled.LineHeight(0), 20);
	EXPECT_EQ(wrapped.SelectionRects(9, 3),
		(std::vector<gfx::Rect>{{21, 0, 42, 14}, {0, 14, 21, 28}}));
}


TEST(TextLayoutTest, TheCaretMovesByGraphemes)
{
	// What is tested: The default boundaries of "a\xC3\xA9 b-c\n" ("aé b-c"
	// and a line break) and moving the caret over it.
	// Success criteria: Right of offset 1 the caret skips the two bytes of
	// "é" to 3, left of 3 back to 1; word boundaries lie around "a\xC3\xA9",
	// "b" and "c"; a line may begin after the space; the line break is
	// mandatory. The ends are boundaries of every kind but Line.
	// Why it matters: The caret never stands inside a character; double
	// clicks and Option+arrows find words.
	//
	// Given
	const FixedTextSystem text;
	const TextLayout layout(text, "a\xC3\xA9 b-c\n", FontRole::Plain, {.wrap = false});

	// When / Then
	EXPECT_EQ(layout.CaretRight(1), 3u);
	EXPECT_EQ(layout.CaretLeft(3), 1u);
	EXPECT_EQ(layout.NextBoundary(0, TextBoundary::Word), 3u);
	EXPECT_EQ(layout.NextBoundary(3, TextBoundary::Word), 4u);
	EXPECT_EQ(layout.NextBoundary(4, TextBoundary::Word), 5u);
	EXPECT_EQ(layout.PreviousBoundary(7, TextBoundary::Word), 6u);
	EXPECT_TRUE(layout.IsBoundary(4, TextBoundary::Line));
	EXPECT_FALSE(layout.IsBoundary(3, TextBoundary::Line));
	EXPECT_TRUE(layout.IsBoundary(8, TextBoundary::MandatoryLine));
	EXPECT_TRUE(layout.IsBoundary(0, TextBoundary::Word));
	EXPECT_EQ(layout.CaretRight(8), 8u);
	EXPECT_EQ(layout.CaretLeft(0), 0u);
}


} // namespace
