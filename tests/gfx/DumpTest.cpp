/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Dump.h>

#include <array>
#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>


namespace {


using namespace ngui::gfx;


constexpr Color kRed = {1, 0, 0, 1};


DisplayListRef
Sample()
{
	Canvas child({0, 0, 4, 4});
	child.FillEllipse({0, 0, 4, 4}, Paint{.color = {0, 0, 1, 0.5f}});
	const DisplayListRef childList = child.Finish();

	const std::array<Line, 2> lines = {{{{0, 0.5f}, {10, 0.5f}, kRed},
		{{0, 1.5f}, {10, 1.5f}, {0, 1, 0, 1}}}};
	Canvas canvas({0, 0, 100, 50});
	canvas.FillRect({0.12345f, -0.0001f, 10, 10}, Paint{.color = kRed});
	canvas.ClipRect({0, 0, 50, 50});
	canvas.DrawLines(lines, 1, Paint::Cap::Square);
	canvas.DrawList(childList, {20, 20});
	return canvas.Finish();
}


TEST(DumpTest, TextIsReadableAndDeterministic)
{
	// What is tested: DumpText() of a list with a fill, lines under a new
	// clip and a nested list.
	// Success criteria: Exactly the expected text: names instead of opcodes,
	// a clip line when the clip changes, numbers rounded to three decimals
	// without trailing zeros (-0.0001 becomes 0), the child indented.
	// Why it matters: Snapshot tests compare this text against checked-in
	// references (specification 15 and 20.5); it must not depend on the
	// platform or the locale.
	//
	// When
	const std::string text = DumpText(*Sample());

	// Then
	EXPECT_EQ(text,
		"DisplayList bounds=[0 0 100 50] ops=3\n"
		"clip [0 0 100 50]\n"
		"FillRect rect=[0.123 0 10 10] color=(1 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"clip [0 0 50 50]\n"
		"DrawLines width=1 cap=Square line=(0 0.5)->(10 0.5):(1 0 0 1)"
		" line=(0 1.5)->(10 1.5):(0 1 0 1)\n"
		"DrawList offset=(20 20)\n"
		"  DisplayList bounds=[0 0 4 4] ops=1\n"
		"  clip [0 0 4 4]\n"
		"  FillEllipse rect=[0 0 4 4] color=(0 0 1 0.5) width=1 cap=Butt join=Miter aa=1\n");
}


TEST(DumpTest, JsonResolvesClipsAndNests)
{
	// What is tested: DumpJson() of the same list.
	// Success criteria: Exactly the expected JSON: every operation carries
	// its clip rectangles, the nested list appears inline under "list".
	// Why it matters: Tools and the test kit read the JSON form
	// (specification 15); resolved clips make each operation self-contained.
	//
	// When
	const std::string json = DumpJson(*Sample());

	// Then
	EXPECT_EQ(json,
		"{\"bounds\":[0,0,100,50],\"ops\":["
		"{\"op\":\"FillRect\",\"clip\":[[0,0,100,50]],\"rect\":[0.123,0,10,10],"
		"\"paint\":{\"color\":[1,0,0,1],\"width\":1,\"cap\":\"Butt\",\"join\":\"Miter\","
		"\"antialias\":true}},"
		"{\"op\":\"DrawLines\",\"clip\":[[0,0,50,50]],\"width\":1,\"cap\":\"Square\","
		"\"lines\":[{\"p0\":[0,0.5],\"p1\":[10,0.5],\"color\":[1,0,0,1]},"
		"{\"p0\":[0,1.5],\"p1\":[10,1.5],\"color\":[0,1,0,1]}]},"
		"{\"op\":\"DrawList\",\"clip\":[[0,0,50,50]],\"offset\":[20,20],\"list\":"
		"{\"bounds\":[0,0,4,4],\"ops\":[{\"op\":\"FillEllipse\",\"clip\":[[0,0,4,4]],"
		"\"rect\":[0,0,4,4],\"paint\":{\"color\":[0,0,1,0.5],\"width\":1,\"cap\":\"Butt\","
		"\"join\":\"Miter\",\"antialias\":true}}]}}]}");
}


TEST(DumpTest, GlyphTextIsEscaped)
{
	// What is tested: The text of a glyph run with a quote, a newline and an
	// invalid UTF-8 byte, in both forms.
	// Success criteria: Quote and newline are escaped, the invalid byte
	// becomes U+FFFD, the rest is kept.
	// Why it matters: The text of a run is not checked for valid UTF-8 when
	// recorded; the JSON form must stay valid JSON regardless.
	//
	// Given
	const std::array<std::uint32_t, 1> glyph = {5};
	const std::array<Point, 1> position = {{{0, 0}}};
	const std::u8string text = u8"a\"\n" + std::u8string(1, char8_t(0xff)) + u8"ä";
	Canvas canvas({0, 0, 10, 10});
	canvas.DrawGlyphRun({2, 12, glyph, position, text, {}}, {1, 9}, Paint{.color = kRed});
	const DisplayListRef list = canvas.Finish();

	// When
	const std::string dumped = DumpText(*list);
	const std::string json = DumpJson(*list);

	// Then
	EXPECT_NE(dumped.find("text=\"a\\\"\\u000a\xef\xbf\xbd\xc3\xa4\""), std::string::npos)
		<< dumped;
	EXPECT_NE(json.find("\"text\":\"a\\\"\\u000a\xef\xbf\xbd\xc3\xa4\""), std::string::npos)
		<< json;
	EXPECT_NE(dumped.find("glyphs=5@(0 0)"), std::string::npos) << dumped;
}


} // namespace
