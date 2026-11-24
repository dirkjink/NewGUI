/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <ngui/Clipboard.h>
#include <ngui/Font.h>
#include <ngui/TextView.h>


NGUI_FUZZ_TARGET(TextStyle)
{
	// What is tested: A stylable text view (not in a window), changed by
	// arbitrary bytes taken in triples: an operation and two arguments,
	// which insert and delete text, style ranges and the caret
	// (SetFontAndColor() with three fonts, sizes, colors and modes), set
	// run arrays, select, cut, copy, paste and undo (a clipboard of its
	// own, emptied first), and switch the view between stylable and not; at
	// most 2048 bytes of text.
	// Success criteria: No crash or sanitizer report; after every operation
	// the run array of all of the text begins at 0, its offsets increase
	// and lie within the text, and no two neighboring runs have the same
	// font and color.
	// Why it matters: The runs follow every change of the text; when they
	// drift, text is drawn in the wrong style or read out of range.
	//
	// Given
	// empty at the start of each input, so that inputs do not depend on
	// each other
	static ngui::BClipboard clipboard("fuzz");
	if (clipboard.Lock()) {
		(void)clipboard.Clear();
		(void)clipboard.Commit();
		clipboard.Unlock();
	}
	ngui::BTextView view({0, 0, 200, 100}, "fuzz", {0, 0, 200, 100});
	view.SetStylable(true);
	// pasting all of it again and again would double it
	view.SetMaxBytes(2048);

	const auto font = [](std::uint8_t byte) {
		ngui::BFont result = (byte & 1) != 0 ? ngui::BFont::Bold()
			: (byte & 2) != 0 ? ngui::BFont::Fixed() : ngui::BFont::Plain();
		result.SetSize(8.0f + static_cast<float>((byte >> 2) % 4) * 6.0f);
		return result;
	};
	const ngui::gfx::Color colors[] = {ngui::gfx::Color::FromRgba8(255, 0, 0),
		ngui::gfx::Color::FromRgba8(0, 0, 255)};
	const ngui::BFontModeFlags modes[] = {ngui::kFontAll, ngui::BFontMode::Size,
		ngui::BFontMode::FamilyAndStyle, ngui::BFontModeFlags{}};

	for (std::size_t i = 0; i + 2 < data.size(); i += 3) {
		const std::uint8_t op = data[i];
		const std::uint8_t a = data[i + 1];
		const std::uint8_t b = data[i + 2];
		const ngui::int32 length = view.TextLength();
		const ngui::int32 from = length > 0 ? a % (length + 1) : 0;
		const ngui::int32 to = length > 0 ? b % (length + 1) : 0;
		const ngui::int32 low = from < to ? from : to;
		const ngui::int32 high = from < to ? to : from;

		// When
		switch (op % 10) {
			case 0:
				view.Insert(from, std::string(static_cast<std::size_t>(b % 5 + 1),
					static_cast<char>('a' + op % 26)));
				break;
			case 1:
				view.Delete(low, high);
				break;
			case 2:
			{
				const ngui::BFont theFont = font(a);
				const ngui::gfx::Color* color = (op & 0x10) != 0 ? &colors[b & 1] : nullptr;
				view.SetFontAndColor(low, high, &theFont, modes[(op >> 5) % 4], color);
				break;
			}
			case 3:
			{
				// the caret's style, for what is typed next
				view.Select(from, from);
				const ngui::BFont theFont = font(b);
				view.SetFontAndColor(&theFont, modes[(op >> 5) % 4], &colors[a & 1]);
				break;
			}
			case 4:
			{
				const ngui::BTextRunArray runs = {{0, font(a), colors[0]},
					{b % 4, font(b), colors[1]}};
				view.SetRunArray(low, high, runs);
				break;
			}
			case 5:
				view.Select(low, high);
				break;
			case 6:
				if ((op & 0x10) != 0)
					view.Cut(&clipboard);
				else
					view.Copy(&clipboard);
				break;
			case 7:
				view.Paste(&clipboard);
				break;
			case 8:
				view.Undo(&clipboard);
				break;
			case 9:
				view.SetStylable((op & 0x10) == 0);
				break;
		}

		// Then
		const ngui::int32 textLength = view.TextLength();
		const ngui::BTextRunArray runs = view.RunArray(0, textLength);
		NGUI_FUZZ_CHECK(!runs.empty());
		NGUI_FUZZ_CHECK(runs.front().offset == 0);
		for (std::size_t r = 1; r < runs.size(); r++) {
			NGUI_FUZZ_CHECK(runs[r].offset > runs[r - 1].offset);
			NGUI_FUZZ_CHECK(runs[r].offset < textLength);
			NGUI_FUZZ_CHECK(!(runs[r].font == runs[r - 1].font
				&& runs[r].color == runs[r - 1].color));
		}
	}
}
