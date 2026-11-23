/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextSystem.h>

#include <algorithm>
#include <atomic>
#include <string>

#include "TruncateString.h"


namespace ngui {


gfx::GlyphRun
ShapedText::Run() const
{
	return {font, sizePx, glyphs, positions, text, clusters};
}


namespace {


// The start of each code point of \a text (bytes that are not UTF-8
// continuation bytes).
std::vector<std::size_t>
CodePointStarts(std::string_view text)
{
	std::vector<std::size_t> starts;
	for (std::size_t i = 0; i < text.size(); i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
			starts.push_back(i);
	}
	return starts;
}


// Metrics without fonts, until the text subsystem exists.
class PlaceholderTextSystem final : public TextSystem {
public:
	static constexpr float kSize = 12;
	static constexpr float kAdvance = 0.6f * kSize;

	float FontSize(FontRole) const override
	{
		return kSize;
	}

	FontHeight Height(FontRole) const override
	{
		return {0.75f * kSize, 0.25f * kSize, 0};
	}

	float StringWidth(std::string_view text, FontRole) const override
	{
		return kAdvance * static_cast<float>(CodePointStarts(text).size());
	}

	std::string Truncate(std::string_view text, float width, TruncationMode mode,
		FontRole) const override
	{
		// Haiku's algorithm with the same advance for every character.
		const std::vector<float> escapements(CodePointStarts(text).size(), 0.6f);
		return TruncateString(text, mode, width, escapements, kSize, kAdvance);
	}

	ShapedText Shape(std::string_view, FontRole) const override
	{
		return {};
	}
};


std::atomic<const TextSystem*> sDefault = nullptr;


} // namespace


const TextSystem&
TextSystem::Default()
{
	static const PlaceholderTextSystem kPlaceholder;
	const TextSystem* text = sDefault.load();
	return text != nullptr ? *text : kPlaceholder;
}


void
TextSystem::SetDefault(const TextSystem* text)
{
	sDefault.store(text);
}


std::span<const FontFile>
TextSystem::Fonts() const
{
	return {};
}


} // namespace ngui
