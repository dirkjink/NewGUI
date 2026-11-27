/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <ngui/TextSystem.h>


namespace ngui::test {


// Fixed metrics: 7 pixels per byte at size 12, ascent 9, descent 3.
class FixedTextSystem : public TextSystem {
public:
	float FontSize(FontRole) const override
	{
		return 12;
	}

	FontHeight Height(FontRole) const override
	{
		return {9, 3, 1};
	}

	float StringWidth(std::string_view text, FontRole) const override
	{
		return 7.0f * static_cast<float>(text.size());
	}

	std::string Truncate(std::string_view text, float width, TruncationMode,
		FontRole) const override
	{
		const auto fits = static_cast<std::size_t>(std::max(width, 0.0f) / 7.0f);
		return std::string(text.substr(0, fits));
	}

	ShapedText Shape(std::string_view text, FontRole) const override
	{
		ShapedText shaped;
		shaped.font = 1;
		shaped.sizePx = 12;
		for (std::size_t i = 0; i < text.size(); i++) {
			shaped.glyphs.push_back(static_cast<std::uint32_t>(text[i]));
			shaped.positions.push_back({7.0f * static_cast<float>(i), 0});
		}
		shaped.advance = 7.0f * static_cast<float>(text.size());
		return shaped;
	}
};


} // namespace ngui::test
