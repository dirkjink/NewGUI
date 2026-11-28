/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <string>
#include <string_view>

#include <ngui/svg/MediaQueries.h>


NGUI_FUZZ_TARGET(MediaQueries)
{
	// What is tested: Arbitrary bytes as the text of an SVG, its media
	// queries resolved (svg::ResolveMediaQueries()) for a width of 0 to 255
	// pixels, as the first byte says.
	// Success criteria: No crash, hang or sanitizer report. The result is
	// never longer than the text; a text without "@media" (in any case)
	// comes back unchanged.
	// Why it matters: The renderer resolves the media queries of every icon
	// it draws, before lunasvg reads it; icons may come from files of any
	// origin.
	//
	if (data.empty())
		return;
	const auto width = static_cast<float>(data[0]);
	const std::string_view text(reinterpret_cast<const char*>(data.data()) + 1,
		data.size() - 1);

	const std::string resolved = ngui::svg::ResolveMediaQueries(text, width);

	NGUI_FUZZ_CHECK(resolved.size() <= text.size());
	std::string lower(text);
	for (char& c : lower) {
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	if (lower.find("@media") == std::string::npos)
		NGUI_FUZZ_CHECK(resolved == text);
}
