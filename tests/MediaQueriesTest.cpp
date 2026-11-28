/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/svg/MediaQueries.h>

#include <string>

#include <gtest/gtest.h>


namespace {


using ngui::svg::ResolveMediaQueries;


TEST(MediaQueriesTest, TheWidthDrawnChoosesTheRules)
{
	// What is tested: The levels of detail as Haiku's icons say them
	// (data/icons/haiku/App_CD-Burner.svg): ".lod1" hidden up to 21.07 px,
	// ".lod2" shown up to 21.08 px; and a range, ".lod3" from 21.12 to
	// 38.4 px; resolved for 16, 32 and 64 pixels.
	// Success criteria: At 16 px the first two rules stand alone; at 32 px
	// the range's; at 64 px none, only the text around them stays.
	// Why it matters: lunasvg ignores media queries; resolved, small icons
	// show Haiku's simpler level of detail instead of the 64 px drawing.
	//
	// Given
	const std::string style = "<style>\n"
		"@media (max-width:21.07px){.lod1{display:none}}\n"
		"@media (max-width:21.08px){.lod2{display:inline}}\n"
		"@media (min-width:21.12px) and (max-width:38.4px){.lod3{display:inline}}\n"
		"</style>";

	// When
	const std::string small = ResolveMediaQueries(style, 16);
	const std::string middle = ResolveMediaQueries(style, 32);
	const std::string large = ResolveMediaQueries(style, 64);

	// Then
	EXPECT_EQ(small, "<style>\n.lod1{display:none}\n.lod2{display:inline}\n\n</style>");
	EXPECT_EQ(middle, "<style>\n\n\n.lod3{display:inline}\n</style>");
	EXPECT_EQ(large, "<style>\n\n\n\n</style>");
}


TEST(MediaQueriesTest, OnlyWhatItKnowsHolds)
{
	// What is tested: Conditions other than plain width features: a list
	// with a comma, the media types all, screen and print, "only screen
	// and", "not", another feature (orientation), a width in em, a feature
	// without a value, malformed numbers, fractions, the keyword in
	// capitals; each for a width of 16.
	// Success criteria: A list holds if one query does; all, screen and
	// "only screen and (max-width:20px)" hold; print, not, orientation, em
	// the feature without a value or with a number that is none (no digits,
	// a sign, two points) do not; widths with a fraction compare exactly;
	// "@MEDIA" is a media rule too.
	// Why it matters: A browser drops a query it does not understand; so
	// does NewGUI, rather than showing a level of detail by mistake.
	//
	// Given
	const auto kept = [](const std::string& condition) {
		return ResolveMediaQueries("@media " + condition + "{x}", 16) == "x";
	};

	// Then
	EXPECT_TRUE(kept("(min-width:40px), (max-width:20px)"));
	EXPECT_TRUE(kept("all"));
	EXPECT_TRUE(kept("screen"));
	EXPECT_TRUE(kept("only screen and (max-width:20px)"));
	EXPECT_TRUE(kept("( max-width : 16px )"));
	EXPECT_FALSE(kept("print"));
	EXPECT_FALSE(kept("not screen"));
	EXPECT_FALSE(kept("(orientation:portrait)"));
	EXPECT_FALSE(kept("(max-width:20em)"));
	EXPECT_FALSE(kept("(max-width)"));
	EXPECT_FALSE(kept("(max-width:px)"));
	EXPECT_FALSE(kept("(max-width:-20px)"));
	EXPECT_FALSE(kept("(max-width:1.2.3px)"));
	EXPECT_TRUE(kept("(max-width:16.5px)"));
	EXPECT_FALSE(kept("(max-width:15.99px)"));
	EXPECT_FALSE(kept("(max-width:20px) (min-width:1px)"));
	EXPECT_FALSE(kept(""));
	EXPECT_EQ(ResolveMediaQueries("@MEDIA (max-width:20px){x}", 16), "x");
}


TEST(MediaQueriesTest, BrokenRulesEndTheText)
{
	// What is tested: A rule nested in one that holds; a rule without a
	// block; a rule whose block is left open, holding and not; a closing
	// brace of the text outside any rule; text without media queries.
	// Success criteria: Nested rules are resolved as well; without a block
	// or without its end a rule takes the rest of the text (a holding one
	// keeps its open block's content); other braces and text stay as they
	// are.
	// Why it matters: CSS ends a broken rule so; the icons come from files,
	// and such input must neither crash nor show something it should not.
	//
	// Then
	EXPECT_EQ(ResolveMediaQueries("@media all{a@media (max-width:8px){b}c}d", 16), "acd");
	EXPECT_EQ(ResolveMediaQueries("a@media all", 16), "a");
	EXPECT_EQ(ResolveMediaQueries("a@media all{b{c}", 16), "ab{c}");
	EXPECT_EQ(ResolveMediaQueries("a@media print{b{c}", 16), "a");
	EXPECT_EQ(ResolveMediaQueries("a}b{c}", 16), "a}b{c}");
	EXPECT_EQ(ResolveMediaQueries("<svg/>", 16), "<svg/>");
	EXPECT_EQ(ResolveMediaQueries("", 16), "");
}


} // namespace
