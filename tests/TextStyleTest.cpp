/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextView.h>

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Clipboard.h>
#include <ngui/Font.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>

#include "TextStyleBuffer.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;


constexpr gfx::Color kRed = gfx::Color::FromRgba8(255, 0, 0);
constexpr gfx::Color kBlue = gfx::Color::FromRgba8(0, 0, 255);


// Family and style of \a font, as "family/style".
std::string
Name(const BFont& font)
{
	std::string family;
	std::string style;
	font.GetFamilyAndStyle(&family, &style);
	return family + "/" + style;
}


// The runs of \a view as "offset family/style size" plus "*" for a color
// other than the text color.
std::vector<std::string>
Runs(const BTextView& view, int32 start, int32 end)
{
	const gfx::Color text = Palette::Default().Get(BColorWhich::DocumentText);
	std::vector<std::string> runs;
	for (const BTextRun& run : view.RunArray(start, end)) {
		runs.push_back(std::to_string(run.offset) + " " + Name(run.font) + " "
			+ std::to_string(static_cast<int>(run.font.Size())) + (run.color == text ? "" : "*"));
	}
	return runs;
}


TEST(TextStyleBufferTest, RangesSplitAndMerge)
{
	// What is tested: Haiku's style buffer over a text of 10 bytes in the
	// plain font: bold red set on 3 to 6, then on 6 to 8; the size 20 (size
	// only) on 3 to 8; the style common to all of it; 2 to 9 removed.
	// Success criteria: Setting a style splits the run it lies in (three
	// runs); the same style next to it merges with it; a mode without the
	// color keeps the colors. The common style keeps only what all runs
	// share (neither family and style nor size here) and reports
	// different colors. Removing a range merges the runs that meet.
	// Why it matters: The runs stay minimal and the offsets right through
	// every change, as in Haiku.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	TextStyleBuffer buffer(BFont::Plain(), std::nullopt);
	buffer.SetStyleRange(0, 10, 10, kFontAll, nullptr, nullptr);
	const BFont bold = BFont::Bold();
	const std::optional<gfx::Color> red = kRed;
	BFont big = BFont::Plain();
	big.SetSize(20);

	// When
	buffer.SetStyleRange(3, 6, 10, kFontAll, &bold, &red);
	const int32 split = buffer.NumRuns();
	buffer.SetStyleRange(6, 8, 10, kFontAll, &bold, &red);
	const int32 merged = buffer.NumRuns();
	const int32 boldEnd = buffer.RunAt(2).offset;
	buffer.SetStyleRange(3, 8, 10, BFontMode::Size, &big, nullptr);
	const TextStyle sized = buffer.GetStyle(5);
	BFont common;
	BFontModeFlags mode;
	std::optional<gfx::Color> color;
	bool sameColor = true;
	buffer.ContinuousGetStyle(&common, &mode, &color, &sameColor, 0, 10);
	buffer.RemoveStyleRange(2, 9);

	// Then
	EXPECT_EQ(split, 3);
	EXPECT_EQ(merged, 3);
	EXPECT_EQ(boldEnd, 8);
	EXPECT_EQ(Name(sized.font), "Default/Bold");
	EXPECT_EQ(sized.font.Size(), 20);
	EXPECT_EQ(sized.color, red);
	EXPECT_FALSE(mode.Has(BFontMode::FamilyAndStyle));
	EXPECT_FALSE(mode.Has(BFontMode::Size));
	EXPECT_TRUE(mode.Has(BFontMode::Face));
	EXPECT_FALSE(sameColor);
	EXPECT_EQ(buffer.NumRuns(), 1);
	EXPECT_EQ(buffer.RunAt(0).style.font, BFont::Plain());
	TextSystem::SetDefault(nullptr);
}


TEST(TextStyleTest, RunsHaveTheirFontsAndColors)
{
	// What is tested: With the fixed text system (ascent 9, descent 3,
	// leading 1 at 12), a stylable text view with "Hello World": bold red
	// on "World", then the size 24 (size only) on "Hello"; the run arrays
	// of all and of 3 to 8; the style at 7 and of the selection. A text
	// view that is not stylable, "abc", with bold red on "b".
	// Success criteria: The runs are the plain font in the text color and
	// "World" in bold red; a run array is relative to its start. The
	// selection's style keeps the size of its last run but not family and
	// style, and reports two colors. The size changes only the size, and
	// the first line is as high as the bigger font: 18 + 6 + 2 rounded up
	// plus 1. Not stylable, the whole text takes the style.
	// Why it matters: Styled text (Haiku's text_run_array) is what editors
	// like StyledEdit build on.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextView view({0, 0, 300, 100}, "view", {0, 0, 300, 100});
	view.SetStylable(true);
	view.SetText("Hello World");
	BTextView plain({0, 0, 300, 100}, "plain", {0, 0, 300, 100});
	plain.SetText("abc");
	const BFont bold = BFont::Bold();
	BFont big = BFont::Plain();
	big.SetSize(24);

	// When
	view.SetFontAndColor(6, 11, &bold, kFontAll, &kRed);
	const std::vector<std::string> styled = Runs(view, 0, 11);
	const std::vector<std::string> part = Runs(view, 3, 8);
	BFont at7;
	gfx::Color color7{};
	view.GetFontAndColor(7, &at7, &color7);
	view.Select(0, 11);
	BFont common;
	BFontModeFlags mode;
	bool sameColor = true;
	view.GetFontAndColor(&common, &mode, nullptr, &sameColor);
	view.SetFontAndColor(0, 5, &big, BFontMode::Size);
	const std::vector<std::string> sized = Runs(view, 0, 11);
	plain.SetFontAndColor(1, 2, &bold, kFontAll, &kRed);

	// Then
	EXPECT_EQ(styled, (std::vector<std::string>{"0 Default/Regular 12", "6 Default/Bold 12*"}));
	EXPECT_EQ(part, (std::vector<std::string>{"0 Default/Regular 12", "3 Default/Bold 12*"}));
	EXPECT_EQ(Name(at7), "Default/Bold");
	EXPECT_EQ(color7, kRed);
	EXPECT_FALSE(mode.Has(BFontMode::FamilyAndStyle));
	EXPECT_TRUE(mode.Has(BFontMode::Size));
	EXPECT_FALSE(sameColor);
	EXPECT_EQ(sized, (std::vector<std::string>{"0 Default/Regular 24", "5 Default/Regular 12",
		"6 Default/Bold 12*"}));
	EXPECT_EQ(view.LineHeight(0), 27);
	EXPECT_EQ(Runs(plain, 0, 3), (std::vector<std::string>{"0 Default/Bold 12*"}));
	TextSystem::SetDefault(nullptr);
}


TEST(TextStyleTest, TypedTextTakesTheNullStyle)
{
	// What is tested: A stylable text view with "ab", the caret at the end
	// given bold blue by SetFontAndColor(); "c" and "d" inserted; the caret
	// put after "a" and "x" inserted; then 2 to 4 deleted.
	// Success criteria: Styling the caret changes no text, but what is
	// inserted there next is bold blue, and so is what follows it. After
	// the caret moved, inserted text takes the style before it (plain).
	// Deleting keeps the runs of what is left.
	// Why it matters: Choosing a style and typing is how styled text is
	// written (Haiku's null style).
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextView view({0, 0, 300, 100}, "view", {0, 0, 300, 100});
	view.SetStylable(true);
	view.SetText("ab");
	const BFont bold = BFont::Bold();

	// When
	view.Select(2, 2);
	view.SetFontAndColor(&bold, kFontAll, &kBlue);
	const std::vector<std::string> before = Runs(view, 0, 2);
	view.Insert("c");
	view.Insert("d");
	const std::vector<std::string> typed = Runs(view, 0, 4);
	view.Select(1, 1);
	view.Insert("x");
	const std::string text(view.Text());
	const std::vector<std::string> inserted = Runs(view, 0, 5);
	view.Delete(2, 4);

	// Then
	EXPECT_EQ(before, (std::vector<std::string>{"0 Default/Regular 12"}));
	EXPECT_EQ(typed, (std::vector<std::string>{"0 Default/Regular 12", "2 Default/Bold 12*"}));
	EXPECT_EQ(text, "axbcd");
	EXPECT_EQ(inserted, (std::vector<std::string>{"0 Default/Regular 12",
		"3 Default/Bold 12*"}));
	EXPECT_EQ(view.Text(), "axd");
	EXPECT_EQ(Runs(view, 0, 3), (std::vector<std::string>{"0 Default/Regular 12",
		"2 Default/Bold 12*"}));
	TextSystem::SetDefault(nullptr);
}


TEST(TextStyleTest, TheClipboardAndUndoKeepTheRuns)
{
	// What is tested: A stylable text view with "Hello World", "World" bold
	// red: "o Wo" (4 to 8) copied to a clipboard; pasted into a stylable
	// view with "[]" between the brackets, into one that is not stylable,
	// and into a stylable one where 'o' is disallowed; then all of the
	// first view cut and the cut undone.
	// Success criteria: The clipboard has the text and the runs, flattened
	// ("application/x-vnd.Be-text_run_array"): plain from 0, bold red
	// from 2. Pasted into a stylable view, the text keeps its runs; into
	// one that is not, only the text arrives in its style. Without the
	// 'o's the runs move with the text left (" W": bold from 1). Undoing
	// the cut brings the text back with its runs.
	// Why it matters: Styled text survives copy, paste and undo, as in
	// Haiku.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BClipboard clipboard("styles");
	const BFont bold = BFont::Bold();
	BTextView source({0, 0, 300, 100}, "source", {0, 0, 300, 100});
	source.SetStylable(true);
	source.SetText("Hello World");
	source.SetFontAndColor(6, 11, &bold, kFontAll, &kRed);
	const std::vector<std::string> original = Runs(source, 0, 11);
	BTextView styled({0, 0, 300, 100}, "styled", {0, 0, 300, 100});
	styled.SetStylable(true);
	styled.SetText("[]");
	BTextView plain({0, 0, 300, 100}, "plain", {0, 0, 300, 100});
	plain.SetText("[]");
	BTextView filtered({0, 0, 300, 100}, "filtered", {0, 0, 300, 100});
	filtered.SetStylable(true);
	filtered.DisallowChar('o');

	// When
	source.Select(4, 8);
	source.Copy(&clipboard);
	BTextRunArray onClipboard;
	if (clipboard.Lock()) {
		if (auto data = clipboard.Data()->FindData("application/x-vnd.Be-text_run_array",
				BTypeCode::Mime)) {
			if (auto runs = BTextView::UnflattenRunArray(*data))
				onClipboard = *runs;
		}
		clipboard.Unlock();
	}
	styled.Select(1, 1);
	styled.Paste(&clipboard);
	plain.Select(1, 1);
	plain.Paste(&clipboard);
	filtered.Paste(&clipboard);
	source.SelectAll();
	source.Cut(&clipboard);
	const std::string cut(source.Text());
	source.Undo(&clipboard);

	// Then
	ASSERT_EQ(onClipboard.size(), 2u);
	EXPECT_EQ(onClipboard[1].offset, 2);
	EXPECT_EQ(Name(onClipboard[1].font), "Default/Bold");
	EXPECT_EQ(onClipboard[1].color, kRed);
	EXPECT_EQ(styled.Text(), "[o Wo]");
	EXPECT_EQ(Runs(styled, 0, 6), (std::vector<std::string>{"0 Default/Regular 12",
		"3 Default/Bold 12*", "5 Default/Regular 12"}));
	EXPECT_EQ(plain.Text(), "[o Wo]");
	EXPECT_EQ(Runs(plain, 0, 6), (std::vector<std::string>{"0 Default/Regular 12"}));
	EXPECT_EQ(filtered.Text(), " W");
	EXPECT_EQ(Runs(filtered, 0, 2), (std::vector<std::string>{"0 Default/Regular 12",
		"1 Default/Bold 12*"}));
	EXPECT_EQ(cut, "");
	EXPECT_EQ(source.Text(), "Hello World");
	EXPECT_EQ(Runs(source, 0, 11), original);
	TextSystem::SetDefault(nullptr);
}


TEST(TextStyleTest, RunArraysFlattenAsInHaiku)
{
	// What is tested: A run array of the plain font in red from 0 and the
	// bold font at 18 in (10, 20, 30) from 5, flattened and unflattened;
	// data with another magic number, and data cut short.
	// Success criteria: The flattened data is Haiku's: 'Ali!', version 0
	// and the count, big endian, then 148 bytes per run (the offset, family
	// and style in 64 bytes each, size, shear 90, face, RGBA, reserved);
	// unflattened, the runs are as before. Other data is BadData.
	// Why it matters: Styled text on the clipboard has Haiku's format, so
	// that it can be exchanged with what reads that format.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BFont bold = BFont::Bold();
	bold.SetSize(18);
	const BTextRunArray runs = {{0, BFont::Plain(), kRed},
		{5, bold, gfx::Color::FromRgba8(10, 20, 30)}};

	// When
	const std::vector<std::byte> flattened = BTextView::FlattenRunArray(runs);
	const auto unflattened = BTextView::UnflattenRunArray(flattened);
	std::vector<std::byte> otherMagic = flattened;
	otherMagic[0] = std::byte{'X'};
	const std::vector<std::byte> cutShort(flattened.begin(), flattened.end() - 1);

	// Then
	ASSERT_EQ(flattened.size(), 12u + 2 * 148);
	const auto byte = [&](std::size_t at) { return static_cast<int>(flattened[at]); };
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(flattened.data()), 4), "Ali!");
	EXPECT_EQ(byte(11), 2);
	EXPECT_EQ(byte(12 + 148 + 3), 5);
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(flattened.data()) + 16), "Default");
	EXPECT_EQ(std::string(reinterpret_cast<const char*>(flattened.data()) + 12 + 148 + 68),
		"Bold");
	// face (B_BOLD_FACE) and color of the second run
	EXPECT_EQ(byte(12 + 148 + 141), 0x20);
	EXPECT_EQ(byte(12 + 148 + 142), 10);
	EXPECT_EQ(byte(12 + 148 + 145), 255);
	ASSERT_TRUE(unflattened.has_value());
	ASSERT_EQ(unflattened->size(), 2u);
	EXPECT_EQ((*unflattened)[1].offset, 5);
	EXPECT_EQ((*unflattened)[1].font, bold);
	EXPECT_EQ((*unflattened)[1].color, runs[1].color);
	EXPECT_EQ((*unflattened)[0].color, kRed);
	EXPECT_EQ(BTextView::UnflattenRunArray(otherMagic).error().code, ErrorCode::BadData);
	EXPECT_EQ(BTextView::UnflattenRunArray(cutShort).error().code, ErrorCode::BadData);
	TextSystem::SetDefault(nullptr);
}


} // namespace
