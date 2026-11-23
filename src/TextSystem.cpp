/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextSystem.h>

#include <algorithm>
#include <atomic>
#include <string>


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
		FontRole role) const override
	{
		if (StringWidth(text, role) <= width)
			return std::string(text);
		const std::vector<std::size_t> starts = CodePointStarts(text);
		// Room for the characters besides the ellipsis.
		const auto room = static_cast<std::size_t>(std::max(0.0f, width / kAdvance - 1));
		const std::string ellipsis = "\u2026";
		if (room == 0)
			return width >= kAdvance ? ellipsis : std::string();
		const auto cut = [&](std::size_t first, std::size_t count) {
			const std::size_t from = starts[first];
			const std::size_t to = first + count < starts.size() ? starts[first + count]
				: text.size();
			return std::string(text.substr(from, to - from));
		};
		switch (mode) {
			case TruncationMode::Beginning:
				return ellipsis + cut(starts.size() - room, room);
			case TruncationMode::Middle:
			{
				const std::size_t head = (room + 1) / 2;
				return cut(0, head) + ellipsis + cut(starts.size() - (room - head), room - head);
			}
			case TruncationMode::End:
				break;
		}
		return cut(0, room) + ellipsis;
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


} // namespace ngui
