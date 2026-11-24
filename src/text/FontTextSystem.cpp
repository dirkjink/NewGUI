/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <graphemebreak.h>
#include <hb-ot.h>
#include <hb.h>
#include <linebreak.h>
#include <wordbreak.h>

#include "TruncateString.h"


namespace ngui::embedded {

extern const unsigned char kNotoSansRegular[];
extern const std::size_t kNotoSansRegularSize;
extern const unsigned char kNotoSansBold[];
extern const std::size_t kNotoSansBoldSize;
extern const unsigned char kNotoSansMonoRegular[];
extern const std::size_t kNotoSansMonoRegularSize;

} // namespace ngui::embedded


namespace ngui::text {


namespace {


// The UTF-8 decoding HarfBuzz would do, for the escapements: the code
// points of \a text, invalid bytes as U+FFFD.
std::vector<hb_codepoint_t>
CodePoints(std::string_view text)
{
	std::vector<hb_codepoint_t> codePoints;
	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0,
		static_cast<int>(text.size()));
	unsigned int count = 0;
	const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
	for (unsigned int i = 0; i < count; i++)
		codePoints.push_back(infos[i].codepoint);
	hb_buffer_destroy(buffer);
	return codePoints;
}


} // namespace


// One font: HarfBuzz's face and font over the embedded bytes, scaled to
// font units (the sizes are applied when measuring).
struct FontTextSystem::Font {
	FontFile		file;
	hb_blob_t*		blob = nullptr;
	hb_face_t*		face = nullptr;
	hb_font_t*		font = nullptr;
	float			unitsPerEm = 1000;

	~Font()
	{
		hb_font_destroy(font);
		hb_face_destroy(face);
		hb_blob_destroy(blob);
	}
};


FontTextSystem::FontTextSystem() = default;


FontTextSystem::~FontTextSystem() = default;


std::expected<std::unique_ptr<FontTextSystem>, Error>
FontTextSystem::CreateBundled(float size)
{
	if (!(size > 0))
		return Failure(ErrorCode::InvalidArgument, "the font size must be positive");
	std::unique_ptr<FontTextSystem> system(new FontTextSystem());
	system->fSize = size;
	struct Bundled {
		gfx::FontId		id;
		FontRole		role;
		const unsigned char* data;
		std::size_t		size;
	};
	const Bundled bundled[] = {
		{1, FontRole::Plain, embedded::kNotoSansRegular, embedded::kNotoSansRegularSize},
		{2, FontRole::Bold, embedded::kNotoSansBold, embedded::kNotoSansBoldSize},
		{3, FontRole::Fixed, embedded::kNotoSansMonoRegular, embedded::kNotoSansMonoRegularSize}
	};
	for (const Bundled& entry : bundled) {
		auto font = std::make_unique<Font>();
		font->file = {entry.id, entry.role,
			std::as_bytes(std::span(entry.data, entry.size))};
		font->blob = hb_blob_create(reinterpret_cast<const char*>(entry.data),
			static_cast<unsigned int>(entry.size), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
		font->face = hb_face_create(font->blob, 0);
		const unsigned int unitsPerEm = hb_face_get_upem(font->face);
		if (hb_face_get_glyph_count(font->face) == 0 || unitsPerEm == 0)
			return Failure(ErrorCode::BadData, "a bundled font cannot be read");
		font->unitsPerEm = static_cast<float>(unitsPerEm);
		font->font = hb_font_create(font->face);
		hb_font_set_scale(font->font, static_cast<int>(unitsPerEm),
			static_cast<int>(unitsPerEm));
		hb_font_make_immutable(font->font);
		system->fFiles.push_back(font->file);
		system->fFonts.push_back(std::move(font));
	}
	return system;
}


const FontTextSystem::Font&
FontTextSystem::_Font(gfx::FontId id) const
{
	for (const std::unique_ptr<Font>& font : fFonts) {
		if (font->file.id == id)
			return *font;
	}
	// an unknown font: the plain one
	return *fFonts[0];
}


std::vector<FontFamilyStyle>
FontTextSystem::Styles() const
{
	return {{"Noto Sans", "Regular", 1, BFontFace::Regular, false},
		{"Noto Sans", "Bold", 2, BFontFace::Bold, false},
		{"Noto Sans Mono", "Regular", 3, BFontFace::Regular, true}};
}


FontSpec
FontTextSystem::RoleFont(FontRole role) const
{
	gfx::FontId font = 1;
	if (role == FontRole::Bold)
		font = 2;
	else if (role == FontRole::Fixed)
		font = 3;
	return {font, fSize};
}


float
FontTextSystem::FontSize(FontRole) const
{
	return fSize;
}


FontHeight
FontTextSystem::Height(FontRole role) const
{
	return DoHeight(RoleFont(role));
}


FontHeight
FontTextSystem::DoHeight(const FontSpec& spec) const
{
	const Font& font = _Font(spec.font);
	hb_font_extents_t extents{};
	hb_font_get_h_extents(font.font, &extents);
	const float scale = spec.size / font.unitsPerEm;
	return {static_cast<float>(extents.ascender) * scale,
		-static_cast<float>(extents.descender) * scale,
		static_cast<float>(extents.line_gap) * scale};
}


ShapedText
FontTextSystem::Shape(std::string_view text, FontRole role) const
{
	return DoShape(text, RoleFont(role));
}


ShapedText
FontTextSystem::DoShape(std::string_view text, const FontSpec& spec) const
{
	const Font& font = _Font(spec.font);
	ShapedText shaped;
	shaped.font = font.file.id;
	shaped.sizePx = spec.size;
	shaped.text.assign(text.begin(), text.end());
	if (text.empty())
		return shaped;

	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0,
		static_cast<int>(text.size()));
	// Left to right until there is BiDi (module X3).
	hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
	hb_buffer_guess_segment_properties(buffer);
	hb_shape(font.font, buffer, nullptr, 0);

	unsigned int count = 0;
	const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
	const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
	const float scale = spec.size / font.unitsPerEm;
	float x = 0;
	for (unsigned int i = 0; i < count; i++) {
		shaped.glyphs.push_back(infos[i].codepoint);
		// HarfBuzz's y grows upwards, the Canvas's downwards.
		shaped.positions.push_back({(x + static_cast<float>(positions[i].x_offset)) * scale,
			-static_cast<float>(positions[i].y_offset) * scale});
		shaped.clusters.push_back(infos[i].cluster);
		x += static_cast<float>(positions[i].x_advance);
	}
	shaped.advance = x * scale;
	hb_buffer_destroy(buffer);
	return shaped;
}


float
FontTextSystem::StringWidth(std::string_view text, FontRole role) const
{
	return DoStringWidth(text, RoleFont(role));
}


float
FontTextSystem::DoStringWidth(std::string_view text, const FontSpec& spec) const
{
	return DoShape(text, spec).advance;
}


std::string
FontTextSystem::Truncate(std::string_view text, float width, TruncationMode mode,
	FontRole role) const
{
	return DoTruncate(text, width, mode, RoleFont(role));
}


std::string
FontTextSystem::DoTruncate(std::string_view text, float width, TruncationMode mode,
	const FontSpec& spec) const
{
	// As Haiku's ServerFont::TruncateString(): the advance of each single
	// character, in em, and the width of the ellipsis.
	const Font& font = _Font(spec.font);
	std::vector<float> escapements;
	for (const hb_codepoint_t codePoint : CodePoints(text)) {
		hb_codepoint_t glyph = 0;
		(void)hb_font_get_nominal_glyph(font.font, codePoint, &glyph);
		escapements.push_back(static_cast<float>(hb_font_get_glyph_h_advance(font.font, glyph))
			/ font.unitsPerEm);
	}
	return TruncateString(text, mode, width, escapements, spec.size,
		DoStringWidth("\xE2\x80\xA6", spec));
}


std::vector<TextBoundaryFlags>
FontTextSystem::Boundaries(std::string_view text) const
{
	// libunibreak: UAX #14 for lines, UAX #29 for graphemes and words.
	static std::once_flag initialized;
	std::call_once(initialized, [] {
		init_linebreak();
		init_wordbreak();
		init_graphemebreak();
	});

	const std::size_t size = text.size();
	std::vector<TextBoundaryFlags> flags(size + 1);
	flags[0] = TextBoundary::Grapheme | TextBoundary::Word;
	if (size == 0)
		return flags;

	// Per byte, the break after it; positions libunibreak leaves untouched
	// (it stops at malformed UTF-8) keep kUnset.
	constexpr char kUnset = 0x7f;
	std::vector<char> lines(size, kUnset);
	std::vector<char> graphemes(size, kUnset);
	std::vector<char> words(size, kUnset);
	const auto* bytes = reinterpret_cast<const utf8_t*>(text.data());
	set_linebreaks_utf8(bytes, size, nullptr, lines.data());
	set_graphemebreaks_utf8(bytes, size, nullptr, graphemes.data());
	set_wordbreaks_utf8(bytes, size, nullptr, words.data());

	for (std::size_t k = 0; k < size; k++) {
		const std::size_t boundary = k + 1;
		// never inside a code point
		if (boundary < size && (static_cast<unsigned char>(text[boundary]) & 0xc0) == 0x80)
			continue;

		// A boundary unless libunibreak says no: a malformed sequence ends
		// where the next code point begins.
		if (graphemes[k] != GRAPHEMEBREAK_NOBREAK)
			flags[boundary] |= TextBoundary::Grapheme;
		if (words[k] != WORDBREAK_NOBREAK)
			flags[boundary] |= TextBoundary::Word;
		if (lines[k] == LINEBREAK_MUSTBREAK)
			flags[boundary] |= TextBoundary::Line | TextBoundary::MandatoryLine;
		else if (lines[k] == LINEBREAK_ALLOWBREAK && boundary < size)
			flags[boundary] |= TextBoundary::Line;
	}
	flags[size] |= TextBoundary::Grapheme | TextBoundary::Word;
	return flags;
}


std::span<const FontFile>
FontTextSystem::Fonts() const
{
	return fFiles;
}


} // namespace ngui::text
