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
#include <string_view>

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


} // namespace


NGUI_FUZZ_TARGET(Text)
{
	// What is tested: Shape(), StringWidth() and Truncate() of the bundled
	// text system with arbitrary bytes as text: the first byte chooses the
	// role and the truncation mode, the second the width.
	// Success criteria: No crash or sanitizer report; the clusters are
	// byte offsets inside the text and never decrease; a truncated valid
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
	std::uint32_t previous = 0;
	for (const std::uint32_t cluster : shaped.clusters) {
		NGUI_FUZZ_CHECK(cluster < text.size());
		NGUI_FUZZ_CHECK(cluster >= previous);
		previous = cluster;
	}
	NGUI_FUZZ_CHECK(shaped.glyphs.size() == shaped.positions.size());
	NGUI_FUZZ_CHECK(measured == shaped.advance);
	if (IsValidUtf8(text)) {
		NGUI_FUZZ_CHECK(IsValidUtf8(truncated));
		NGUI_FUZZ_CHECK(truncated == text || truncated.empty()
			|| IsTruncationOf(truncated, text, mode));
	}
}
