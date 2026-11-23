/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "TruncateString.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui;


// Every character 1 em, at size 10; the ellipsis 10 wide.
std::string
Truncated(std::string_view text, TruncationMode mode, float width, std::size_t chars)
{
	const std::vector<float> escapements(chars, 1.0f);
	return TruncateString(text, mode, width, escapements, 10, 10);
}


TEST(TruncateStringTest, HaikusAlgorithmForEachMode)
{
	// What is tested: Haiku's truncate_string() as ported, for "abcdefgh"
	// with equal characters of 10 px and an ellipsis of 10 px, at 50 px,
	// at 80 px and at 5 px.
	// Success criteria: End keeps "abcd…", Beginning "…efgh", Middle takes
	// from both sides as Haiku alternates and gives "ab…gh"; at 80 px the
	// text fits and stays; at 5 px not even the ellipsis fits and the text
	// is empty.
	// Why it matters: Labels of buttons and menu fields are truncated this
	// way in Haiku (module X5); the ported looks lay out with these results.
	//
	// When / Then
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::End, 50, 8), "abcd…");
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::Beginning, 50, 8), "…efgh");
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::Middle, 50, 8), "ab…gh");
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::End, 80, 8), "abcdefgh");
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::End, 5, 8), "");
	EXPECT_EQ(Truncated("abcdefgh", TruncationMode::Middle, 5, 8), "");
}


TEST(TruncateStringTest, CharactersAreCodePoints)
{
	// What is tested: Truncation of "äöüßéè" (two bytes per character) and
	// a call with fewer escapements than characters.
	// Success criteria: Cuts happen between code points, never inside one;
	// too few escapements leave the text as it is.
	// Why it matters: BString counts UTF-8 characters, not bytes; a cut
	// inside a character would make invalid UTF-8.
	//
	// When / Then
	EXPECT_EQ(Truncated("äöüßéè", TruncationMode::End, 40, 6), "äöü…");
	EXPECT_EQ(Truncated("äöüßéè", TruncationMode::Beginning, 40, 6), "…ßéè");
	EXPECT_EQ(Truncated("äöüßéè", TruncationMode::End, 40, 3), "äöüßéè");
}


} // namespace
