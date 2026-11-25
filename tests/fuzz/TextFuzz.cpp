/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/text/FontTextSystem.h>


namespace {


// Whether \a text is valid UTF-8 (no cut code point, no stray byte).
bool
IsValidUtf8(std::string_view text)
{
	std::size_t i = 0;
	while (i < text.size()) {
		const auto lead = static_cast<unsigned char>(text[i]);
		const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2
			: (lead >> 4) == 0xe ? 3 : (lead >> 3) == 0x1e ? 4 : 0;
		if (length == 0 || i + length > text.size())
			return false;
		for (std::size_t k = 1; k < length; k++) {
			if ((static_cast<unsigned char>(text[i + k]) & 0xc0) != 0x80)
				return false;
		}
		i += length;
	}
	return true;
}


// Whether \a truncated is \a text with a part replaced by one ellipsis:
// a beginning of the text, "…", an end of it, together shorter than it; for
// End the end is empty, for Beginning the beginning.
bool
IsTruncationOf(std::string_view truncated, std::string_view text,
	ngui::TruncationMode mode)
{
	constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
	for (std::size_t at = truncated.find(kEllipsis); at != std::string_view::npos;
			at = truncated.find(kEllipsis, at + 1)) {
		const std::string_view head = truncated.substr(0, at);
		const std::string_view tail = truncated.substr(at + kEllipsis.size());
		if (mode == ngui::TruncationMode::End && !tail.empty())
			continue;
		if (mode == ngui::TruncationMode::Beginning && !head.empty())
			continue;
		if (head.size() + tail.size() < text.size() && text.starts_with(head)
			&& text.ends_with(tail)) {
			return true;
		}
	}
	return false;
}


// The paragraphs of \a text: split at the paragraph separators of UAX #9
// (type B: LF, CR, U+001C to U+001E, U+0085, U+2029), without them.
std::vector<std::string_view>
Paragraphs(std::string_view text)
{
	std::vector<std::string_view> paragraphs;
	std::size_t start = 0;
	std::size_t i = 0;
	while (i < text.size()) {
		std::size_t separator = 0;
		const auto c = static_cast<unsigned char>(text[i]);
		if (c == '\n' || c == '\r' || (c >= 0x1c && c <= 0x1e))
			separator = 1;
		else if (text.substr(i, 2) == "\xC2\x85")
			separator = 2;
		else if (text.substr(i, 3) == "\xE2\x80\xA9")
			separator = 3;
		if (separator == 0) {
			i++;
			continue;
		}
		paragraphs.push_back(text.substr(start, i - start));
		i += separator;
		start = i;
	}
	paragraphs.push_back(text.substr(start));
	return paragraphs;
}


} // namespace


NGUI_FUZZ_TARGET(Text)
{
	// What is tested: Shape(), StringWidth() and Truncate() of the bundled
	// text system with arbitrary bytes as text: the first byte chooses the
	// role and the truncation mode, the second the width.
	// Success criteria: No crash or sanitizer report; the clusters are
	// byte offsets inside the text and, in text that is one left-to-right
	// run, never decrease; the visual runs of the bidirectional algorithm
	// cover each paragraph once, each in it; the font runs (the
	// fallback to the other bundled fonts) cover the glyphs in order, and
	// Runs() views each glyph once; a truncated valid
	// UTF-8 text is valid UTF-8 again, and the text itself, empty, or a
	// beginning and an end of it joined by "…" (for End only a beginning,
	// for Beginning only an end).
	// Why it matters: Labels and text views get text from users and files;
	// truncation cuts it by characters and must never split one.
	//
	// Given
	static const std::unique_ptr<ngui::text::FontTextSystem> kText
		= std::move(*ngui::text::FontTextSystem::CreateBundled());
	if (data.size() < 2)
		return;
	const auto role = static_cast<ngui::FontRole>(data[0] % 3);
	const auto mode = static_cast<ngui::TruncationMode>((data[0] / 3) % 3);
	const float width = static_cast<float>(data[1]);
	const std::string text(reinterpret_cast<const char*>(data.data() + 2), data.size() - 2);

	// When
	const ngui::ShapedText shaped = kText->Shape(text, role);
	const float measured = kText->StringWidth(text, role);
	const std::string truncated = kText->Truncate(text, width, mode, role);

	// Then
	bool leftToRight = true;
	for (const std::string_view paragraph : Paragraphs(text)) {
		const std::vector<ngui::DirectionRun> runs
			= kText->VisualRuns(paragraph, 0, paragraph.size());
		leftToRight = leftToRight && runs.size() == 1
			&& runs[0].direction == ngui::TextDirection::LeftToRight;
		std::size_t covered = 0;
		for (const ngui::DirectionRun& run : runs) {
			NGUI_FUZZ_CHECK(run.start <= run.end && run.end <= paragraph.size());
			covered += run.end - run.start;
		}
		NGUI_FUZZ_CHECK(covered == paragraph.size());
	}
	std::uint32_t previous = 0;
	for (const std::uint32_t cluster : shaped.clusters) {
		NGUI_FUZZ_CHECK(cluster < text.size());
		NGUI_FUZZ_CHECK(!leftToRight || cluster >= previous);
		previous = cluster;
	}
	NGUI_FUZZ_CHECK(shaped.glyphs.size() == shaped.positions.size());
	NGUI_FUZZ_CHECK(measured == shaped.advance);
	std::size_t runEnd = 0;
	for (const ngui::ShapedFontRun& run : shaped.fontRuns) {
		NGUI_FUZZ_CHECK(run.end >= runEnd && run.end <= shaped.glyphs.size());
		NGUI_FUZZ_CHECK(run.font >= 1 && run.font <= 3);
		runEnd = run.end;
	}
	NGUI_FUZZ_CHECK(shaped.fontRuns.empty() || runEnd == shaped.glyphs.size());
	std::size_t viewed = 0;
	for (const ngui::gfx::GlyphRun& run : shaped.Runs()) {
		NGUI_FUZZ_CHECK(run.glyphs.size() == run.positions.size());
		NGUI_FUZZ_CHECK(run.clusters.size() == run.glyphs.size());
		viewed += run.glyphs.size();
	}
	NGUI_FUZZ_CHECK(viewed == shaped.glyphs.size());
	if (IsValidUtf8(text)) {
		NGUI_FUZZ_CHECK(IsValidUtf8(truncated));
		NGUI_FUZZ_CHECK(truncated == text || truncated.empty()
			|| IsTruncationOf(truncated, text, mode));
	}
}
