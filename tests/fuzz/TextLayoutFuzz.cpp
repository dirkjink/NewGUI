/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <ngui/TextLayout.h>
#include <ngui/text/FontTextSystem.h>


NGUI_FUZZ_TARGET(TextLayout)
{
	// What is tested: A TextLayout of arbitrary bytes in the bundled fonts:
	// the first byte chooses wrapping, alignment and the role and size of
	// a second style run, the second the width, the third where that run begins;
	// then points and offsets at every byte and some points.
	// Success criteria: No crash or sanitizer report; the lines cover the
	// text from 0 to its end without gaps, each but an empty last line
	// longer than zero, their tops increasing; OffsetAt() gives an offset
	// within the text; CaretLeft() and CaretRight() from every offset stay
	// in the text and never stop before a UTF-8 continuation byte; in text
	// without right-to-left runs, moving the caret right from 0 reaches the
	// end in increasing steps (with them, the caret moves visually, so
	// right may go back).
	// Why it matters: Text views lay out whatever users type or paste,
	// including broken UTF-8 and very narrow widths.
	//
	// Given
	static const std::unique_ptr<ngui::text::FontTextSystem> kText
		= std::move(*ngui::text::FontTextSystem::CreateBundled());
	if (data.size() < 3)
		return;
	ngui::TextLayoutOptions options;
	options.wrap = (data[0] & 1) != 0;
	options.alignment = static_cast<ngui::BHorizontalAlignment>((data[0] >> 1) % 3);
	options.width = static_cast<float>(data[1]);
	const std::string text(reinterpret_cast<const char*>(data.data() + 3), data.size() - 3);
	// the second run in another role, at 6 to 34 pixels
	const ngui::FontSpec second = {
		kText->RoleFont(static_cast<ngui::FontRole>((data[0] >> 3) % 3)).font,
		6.0f + static_cast<float>(data[0] >> 5) * 4.0f};
	const std::vector<ngui::TextStyleRun> runs = {
		{0, kText->RoleFont(ngui::FontRole::Plain)},
		{text.empty() ? 0 : data[2] % (text.size() + 1), second}};

	// When
	const ngui::TextLayout layout(*kText, text, runs, options);

	// Then
	NGUI_FUZZ_CHECK(layout.CountLines() >= 1);
	std::size_t expected = 0;
	float top = -1;
	for (ngui::int32 i = 0; i < layout.CountLines(); i++) {
		const ngui::TextLayout::Line& line = layout.LineAt(i);
		NGUI_FUZZ_CHECK(line.start == expected);
		NGUI_FUZZ_CHECK(line.end > line.start || i == layout.CountLines() - 1);
		NGUI_FUZZ_CHECK(line.top > top);
		expected = line.end;
		top = line.top;
	}
	NGUI_FUZZ_CHECK(expected == text.size());

	for (std::size_t offset = 0; offset <= text.size(); offset++) {
		const ngui::gfx::Point point = layout.PointAt(offset);
		NGUI_FUZZ_CHECK(layout.OffsetAt(point) <= text.size());
		NGUI_FUZZ_CHECK(layout.OffsetAt({point.x + 3, point.y + 1}) <= text.size());
	}
	// the start and end of the text, or before a byte that begins a code
	// point (a stray continuation byte at the start is not one)
	const auto onBoundary = [&](std::size_t offset) {
		return offset == 0 || offset == text.size()
			|| (static_cast<unsigned char>(text[offset]) & 0xc0) != 0x80;
	};
	for (std::size_t offset = 0; offset <= text.size(); offset++) {
		const std::size_t right = layout.CaretRight(offset);
		const std::size_t left = layout.CaretLeft(offset);
		NGUI_FUZZ_CHECK(right <= text.size() && onBoundary(right));
		NGUI_FUZZ_CHECK(left <= text.size() && onBoundary(left));
	}
	if (!layout.HasRightToLeft()) {
		std::size_t caret = 0;
		while (caret < text.size()) {
			const std::size_t next = layout.CaretRight(caret);
			NGUI_FUZZ_CHECK(next > caret);
			if (next <= caret)
				break;
			caret = next;
		}
	}
	(void)layout.SelectionRects(0, text.size());
}
