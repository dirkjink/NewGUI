/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextSystem.h>

#include <algorithm>
#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Paint.h>

#include "TruncateString.h"


namespace ngui {


std::vector<gfx::GlyphRun>
ShapedText::Runs() const
{
	std::vector<gfx::GlyphRun> runs;
	const std::span<const std::uint32_t> allGlyphs(glyphs);
	const std::span<const gfx::Point> allPositions(positions);
	const std::span<const std::uint32_t> allClusters(clusters);
	const auto add = [&](gfx::FontId runFont, std::size_t begin, std::size_t end) {
		// positions and clusters as many as the glyphs, or broken text
		end = std::min({end, glyphs.size(), positions.size()});
		if (begin >= end)
			return;
		const std::size_t count = end - begin;
		runs.push_back({runFont, sizePx, allGlyphs.subspan(begin, count),
			allPositions.subspan(begin, count), text,
			clusters.size() == glyphs.size() ? allClusters.subspan(begin, count)
				: std::span<const std::uint32_t>()});
	};
	if (fontRuns.empty()) {
		add(font, 0, glyphs.size());
		return runs;
	}
	std::size_t begin = 0;
	for (const ShapedFontRun& run : fontRuns) {
		add(run.font, begin, run.end);
		begin = std::max(begin, run.end);
	}
	return runs;
}


void
ShapedText::Draw(gfx::Canvas& canvas, gfx::Point origin, const gfx::Paint& paint) const
{
	for (const gfx::GlyphRun& run : Runs())
		canvas.DrawGlyphRun(run, origin, paint);
}


gfx::FontId
ShapedText::FontOf(std::size_t glyph) const
{
	for (const ShapedFontRun& run : fontRuns) {
		if (glyph < run.end)
			return run.font;
	}
	return font;
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


std::vector<TextBoundaryFlags>
TextSystem::Boundaries(std::string_view text) const
{
	const std::size_t size = text.size();
	std::vector<TextBoundaryFlags> flags(size + 1);
	const auto isContinuation = [&](std::size_t i) {
		return (static_cast<unsigned char>(text[i]) & 0xc0) == 0x80;
	};
	const auto isSpace = [&](std::size_t i) { return text[i] == ' ' || text[i] == '\t'; };
	const auto isWord = [&](std::size_t i) {
		const auto c = static_cast<unsigned char>(text[i]);
		return c >= 0x80 || c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z');
	};

	flags[0] = TextBoundary::Grapheme | TextBoundary::Word;
	flags[size] = TextBoundary::Grapheme | TextBoundary::Word;
	for (std::size_t i = 1; i < size; i++) {
		if (isContinuation(i))
			continue;
		flags[i] |= TextBoundary::Grapheme;
		if (text[i - 1] == '\n')
			flags[i] |= TextBoundary::Line | TextBoundary::MandatoryLine;
		else if (isSpace(i - 1) && !isSpace(i))
			flags[i] |= TextBoundary::Line;
		if (isWord(i - 1) != isWord(i) || (!isWord(i) && !isContinuation(i)))
			flags[i] |= TextBoundary::Word;
	}
	if (size > 0 && text[size - 1] == '\n')
		flags[size] |= TextBoundary::Line | TextBoundary::MandatoryLine;
	return flags;
}


std::vector<float>
TextSystem::CaretPositions(std::string_view text, FontRole role) const
{
	return CaretPositions(text, RoleFont(role));
}


std::vector<float>
TextSystem::CaretPositions(std::string_view text, const FontSpec& font) const
{
	return CaretsOf(text, Shape(text, font), false, font);
}


std::vector<float>
TextSystem::CaretPositions(std::string_view text, const FontSpec& font,
	TextDirection direction) const
{
	return CaretsOf(text, ShapeRun(text, font, direction),
		direction == TextDirection::RightToLeft, font);
}


// The x of the caret before each byte of \a text and after it, from its
// glyphs \a shaped: a caret stands at the leading edge of its character,
// the left one left to right, the right one right to left (\a rightToLeft,
// then the clusters decrease from left to right).
std::vector<float>
TextSystem::CaretsOf(std::string_view text, const ShapedText& shaped, bool rightToLeft,
	const FontSpec& font) const
{
	const std::size_t size = text.size();
	std::vector<float> carets(size + 1, 0.0f);
	if (size == 0)
		return carets;

	const std::vector<std::size_t> starts = CodePointStarts(text);
	if (shaped.clusters.size() == shaped.glyphs.size() && !shaped.clusters.empty()) {
		// The clusters in visual order, from their leftmost glyph to the
		// next cluster's (the last to the advance).
		struct Span {
			std::size_t		cluster;
			float			left;
			float			right;
		};
		std::vector<Span> spans;
		for (std::size_t g = 0; g < shaped.glyphs.size(); g++) {
			const std::size_t cluster = std::min<std::size_t>(shaped.clusters[g], size);
			if (spans.empty() || spans.back().cluster != cluster)
				spans.push_back({cluster, shaped.positions[g].x, 0});
		}
		for (std::size_t v = 0; v < spans.size(); v++)
			spans[v].right = v + 1 < spans.size() ? spans[v + 1].left : shaped.advance;
		// in logical order; bytes before the first cluster belong to it
		std::ranges::stable_sort(spans, {}, &Span::cluster);
		spans.front().cluster = 0;

		// The code points of a cluster share its width evenly, from its
		// leading edge.
		std::size_t point = 0;
		for (std::size_t c = 0; c < spans.size(); c++) {
			const std::size_t end = c + 1 < spans.size() ? spans[c + 1].cluster : size;
			const std::size_t first = point;
			while (point < starts.size() && starts[point] < end)
				point++;
			const std::size_t count = point - first;
			const float width = spans[c].right - spans[c].left;
			for (std::size_t k = first; k < point; k++) {
				const float share = static_cast<float>(k - first) / static_cast<float>(count);
				carets[starts[k]] = rightToLeft ? spans[c].right - width * share
					: spans[c].left + width * share;
			}
		}
		carets[size] = rightToLeft ? spans.back().left : shaped.advance;
	} else {
		const float width = StringWidth(text, font);
		for (const std::size_t start : starts) {
			const float before = StringWidth(text.substr(0, start), font);
			carets[start] = rightToLeft ? width - before : before;
		}
		carets[size] = rightToLeft ? 0.0f : width;
	}

	// bytes inside a code point: the value of its start
	for (std::size_t i = 1; i < size; i++) {
		if ((static_cast<unsigned char>(text[i]) & 0xc0) == 0x80)
			carets[i] = carets[i - 1];
	}
	return carets;
}


ShapedText
TextSystem::ShapeRun(std::string_view text, const FontSpec& font,
	TextDirection direction) const
{
	return DoShapeRun(text, font, direction);
}


ShapedText
TextSystem::DoShapeRun(std::string_view text, const FontSpec& font, TextDirection) const
{
	return DoShape(text, font);
}


std::vector<DirectionRun>
TextSystem::VisualRuns(std::string_view, std::size_t lineStart, std::size_t lineEnd) const
{
	return {{lineStart, lineEnd, TextDirection::LeftToRight}};
}


TextDirection
TextSystem::ParagraphDirection(std::string_view) const
{
	return TextDirection::LeftToRight;
}


std::vector<FontFamilyStyle>
TextSystem::Styles() const
{
	return {{"Default", "Regular", 1, BFontFace::Regular, false},
		{"Default", "Bold", 2, BFontFace::Bold, false},
		{"Default Mono", "Regular", 3, BFontFace::Regular, true}};
}


std::optional<FontFamilyStyle>
TextSystem::Style(std::string_view family, std::string_view style) const
{
	for (FontFamilyStyle& entry : Styles()) {
		if (entry.family == family && entry.style == style)
			return std::move(entry);
	}
	return std::nullopt;
}


FontRole
TextSystem::RoleOf(gfx::FontId font)
{
	switch (font) {
		case 2:
			return FontRole::Bold;
		case 3:
			return FontRole::Fixed;
		default:
			return FontRole::Plain;
	}
}


FontSpec
TextSystem::RoleFont(FontRole role) const
{
	gfx::FontId font = 1;
	if (role == FontRole::Bold)
		font = 2;
	else if (role == FontRole::Fixed)
		font = 3;
	return {font, FontSize(role)};
}


namespace {


// From the size of \a role to \a size; metrics scale linearly without
// hinting (specification 13.4).
float
ScaleFrom(const TextSystem& text, FontRole role, float size)
{
	const float roleSize = text.FontSize(role);
	return roleSize > 0 ? size / roleSize : 1.0f;
}


} // namespace


FontHeight
TextSystem::Height(const FontSpec& font) const
{
	return DoHeight(font);
}


float
TextSystem::StringWidth(std::string_view text, const FontSpec& font) const
{
	return DoStringWidth(text, font);
}


std::string
TextSystem::Truncate(std::string_view text, float width, TruncationMode mode,
	const FontSpec& font) const
{
	return DoTruncate(text, width, mode, font);
}


ShapedText
TextSystem::Shape(std::string_view text, const FontSpec& font) const
{
	return DoShape(text, font);
}


FontHeight
TextSystem::DoHeight(const FontSpec& font) const
{
	const FontRole role = RoleOf(font.font);
	const float scale = ScaleFrom(*this, role, font.size);
	const FontHeight height = Height(role);
	return {height.ascent * scale, height.descent * scale, height.leading * scale};
}


float
TextSystem::DoStringWidth(std::string_view text, const FontSpec& font) const
{
	const FontRole role = RoleOf(font.font);
	return StringWidth(text, role) * ScaleFrom(*this, role, font.size);
}


std::string
TextSystem::DoTruncate(std::string_view text, float width, TruncationMode mode,
	const FontSpec& font) const
{
	const FontRole role = RoleOf(font.font);
	const float scale = ScaleFrom(*this, role, font.size);
	return Truncate(text, scale > 0 ? width / scale : width, mode, role);
}


ShapedText
TextSystem::DoShape(std::string_view text, const FontSpec& font) const
{
	const FontRole role = RoleOf(font.font);
	const float scale = ScaleFrom(*this, role, font.size);
	ShapedText shaped = Shape(text, role);
	shaped.sizePx *= scale;
	for (gfx::Point& position : shaped.positions)
		position = {position.x * scale, position.y * scale};
	shaped.advance *= scale;
	return shaped;
}


std::vector<FontFile>
TextSystem::Fonts() const
{
	return {};
}


namespace {

thread_local TextSystem::TruncationObserver sTruncationObserver;

} // namespace


void
TextSystem::SetTruncationObserver(TruncationObserver observer)
{
	sTruncationObserver = std::move(observer);
}


void
TextSystem::ReportTruncation(std::string_view text, std::string_view shown)
{
	if (sTruncationObserver && shown != text)
		sTruncationObserver(text, shown);
}


} // namespace ngui
