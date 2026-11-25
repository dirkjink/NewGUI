/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <algorithm>
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


// A code point of a text and the byte it begins at.
struct CodePoint {
	char32_t		value;
	std::size_t		offset;
};


// The UTF-8 decoding HarfBuzz does: the code points of \a text, invalid
// bytes as U+FFFD.
std::vector<CodePoint>
CodePoints(std::string_view text)
{
	std::vector<CodePoint> codePoints;
	hb_buffer_t* buffer = hb_buffer_create();
	hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0,
		static_cast<int>(text.size()));
	unsigned int count = 0;
	const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
	for (unsigned int i = 0; i < count; i++)
		codePoints.push_back({static_cast<char32_t>(infos[i].codepoint), infos[i].cluster});
	hb_buffer_destroy(buffer);
	return codePoints;
}


// Characters a font need not have: control characters and Unicode's
// default ignorables (joiners, variation selectors, format characters),
// which HarfBuzz hides without a glyph.
bool
Ignorable(char32_t c)
{
	return c < 0x20 || (c >= 0x7f && c <= 0x9f) || c == 0xad || c == 0x34f || c == 0x61c
		|| (c >= 0x115f && c <= 0x1160) || (c >= 0x17b4 && c <= 0x17b5)
		|| (c >= 0x180b && c <= 0x180f) || (c >= 0x200b && c <= 0x200f)
		|| (c >= 0x202a && c <= 0x202e) || (c >= 0x2060 && c <= 0x206f) || c == 0x3164
		|| (c >= 0xfe00 && c <= 0xfe0f) || c == 0xfeff || c == 0xffa0
		|| (c >= 0xfff0 && c <= 0xfff8) || (c >= 0x1bca0 && c <= 0x1bca3)
		|| (c >= 0x1d173 && c <= 0x1d17a) || (c >= 0xe0000 && c <= 0xe0fff);
}


// The styles of the bundled fonts 1 to 3.
std::vector<FontFamilyStyle>
BundledStyles()
{
	return {{"Noto Sans", "Regular", 1, BFontFace::Regular, false},
		{"Noto Sans", "Bold", 2, BFontFace::Bold, false},
		{"Noto Sans Mono", "Regular", 3, BFontFace::Regular, true}};
}


void
InitUnibreak()
{
	static std::once_flag initialized;
	std::call_once(initialized, [] {
		init_linebreak();
		init_wordbreak();
		init_graphemebreak();
	});
}


// A name of \a face from its name table, UTF-8; empty if it has none.
std::string
FaceName(hb_face_t* face, hb_ot_name_id_t id)
{
	unsigned int size = hb_ot_name_get_utf8(face, id, HB_LANGUAGE_INVALID, nullptr, nullptr);
	if (size == 0)
		return {};
	std::string name(size + 1, '\0');
	size++;
	(void)hb_ot_name_get_utf8(face, id, HB_LANGUAGE_INVALID, &size, name.data());
	name.resize(size);
	return name;
}


} // namespace


// One font: HarfBuzz's face and font over the embedded bytes or a mapped
// file, scaled to font units (the sizes are applied when measuring).
struct FontTextSystem::Font {
	FontFile		file;
	FontFamilyStyle	look;
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

	// HarfBuzz's objects over \a blob (taken over); false if it is no font.
	bool Open(hb_blob_t* fontBlob, unsigned int index)
	{
		blob = fontBlob;
		face = hb_face_create(blob, index);
		const unsigned int units = hb_face_get_upem(face);
		if (hb_face_get_glyph_count(face) == 0 || units == 0)
			return false;
		unitsPerEm = static_cast<float>(units);
		font = hb_font_create(face);
		hb_font_set_scale(font, static_cast<int>(units), static_cast<int>(units));
		hb_font_make_immutable(font);
		return true;
	}

	bool Has(char32_t codePoint) const
	{
		hb_codepoint_t glyph = 0;
		return Ignorable(codePoint) || hb_font_get_nominal_glyph(font, codePoint, &glyph);
	}
};


// A font of the source: listed (with its look) or found as a fallback,
// loaded on first use.
struct FontTextSystem::Known {
	FontLocation	location;
	gfx::FontId		id = 0;
	std::optional<FontFamilyStyle> look;
	const Font*		font = nullptr;
	bool			failed = false;
};


FontTextSystem::FontTextSystem() = default;


FontTextSystem::~FontTextSystem() = default;


std::expected<std::unique_ptr<FontTextSystem>, Error>
FontTextSystem::CreateBundled(float size)
{
	return Create(nullptr, size);
}


std::expected<std::unique_ptr<FontTextSystem>, Error>
FontTextSystem::Create(std::unique_ptr<FontSource> fonts, float size)
{
	if (!(size > 0))
		return Failure(ErrorCode::InvalidArgument, "the font size must be positive");
	std::unique_ptr<FontTextSystem> system(new FontTextSystem());
	system->fSize = size;
	system->fSource = std::move(fonts);
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
	const std::vector<FontFamilyStyle> styles = BundledStyles();
	for (std::size_t i = 0; i < std::size(bundled); i++) {
		const Bundled& entry = bundled[i];
		auto font = std::make_unique<Font>();
		font->file = {entry.id, entry.role, std::as_bytes(std::span(entry.data, entry.size))};
		font->look = styles[i];
		if (!font->Open(hb_blob_create(reinterpret_cast<const char*>(entry.data),
				static_cast<unsigned int>(entry.size), HB_MEMORY_MODE_READONLY, nullptr,
				nullptr), 0)) {
			return Failure(ErrorCode::BadData, "a bundled font cannot be read");
		}
		system->fFonts.push_back(std::move(font));
	}
	return system;
}


const FontTextSystem::Font&
FontTextSystem::_Font(gfx::FontId id) const
{
	std::scoped_lock lock(fLock);
	for (const std::unique_ptr<Font>& font : fFonts) {
		if (font->file.id == id)
			return *font;
	}
	// a font the source listed, not loaded yet
	for (const std::unique_ptr<Known>& known : fKnown) {
		if (known->id == id) {
			if (const Font* font = _LoadLocked(*known))
				return *font;
			break;
		}
	}
	// an unknown font, or one that cannot be read: the plain one
	return *fFonts[0];
}


// The font for the grapheme cluster \a cluster that \a primary lacks:
// the first of the other bundled fonts and the source's font that has all
// of it, else the first that has its first missing character; nullptr if
// none has that.
const FontTextSystem::Font*
FontTextSystem::_FontFor(const Font& primary, std::span<const char32_t> cluster) const
{
	const auto missing = std::ranges::find_if(cluster,
		[&](char32_t c) { return !primary.Has(c); });
	if (missing == cluster.end())
		return &primary;

	std::vector<const Font*> candidates;
	{
		std::scoped_lock lock(fLock);
		for (std::size_t i = 0; i < 3; i++) {
			if (fFonts[i].get() != &primary)
				candidates.push_back(fFonts[i].get());
		}
	}
	if (const Font* found = _SourceFont(primary, *missing))
		candidates.push_back(found);

	const Font* partial = nullptr;
	for (const Font* candidate : candidates) {
		if (std::ranges::all_of(cluster, [&](char32_t c) { return candidate->Has(c); }))
			return candidate;
		if (partial == nullptr && candidate->Has(*missing))
			partial = candidate;
	}
	return partial;
}


// The font the source finds for \a codePoint like \a primary, loaded;
// nullptr if there is none or it lacks the character. Asked once per font
// and character.
const FontTextSystem::Font*
FontTextSystem::_SourceFont(const Font& primary, char32_t codePoint) const
{
	if (fSource == nullptr)
		return nullptr;
	const std::pair key(primary.file.id, codePoint);
	{
		std::scoped_lock lock(fLock);
		if (const auto found = fFallbacks.find(key); found != fFallbacks.end()) {
			if (found->second == 0)
				return nullptr;
			for (const std::unique_ptr<Font>& font : fFonts) {
				if (font->file.id == found->second)
					return font.get();
			}
			return nullptr;
		}
	}

	// not under the lock: the system may take its time
	const std::optional<FontLocation> location = fSource->Fallback(codePoint, primary.look);
	const Font* font = location.has_value() ? _Load(*location) : nullptr;
	if (font != nullptr && !font->Has(codePoint))
		font = nullptr;
	std::scoped_lock lock(fLock);
	fFallbacks[key] = font != nullptr ? font->file.id : 0;
	return font;
}


// The font at \a location, loaded on first use; nullptr if it cannot be
// read.
const FontTextSystem::Font*
FontTextSystem::_Load(const FontLocation& location) const
{
	std::scoped_lock lock(fLock);
	return _LoadLocked(_KnownLocked(location));
}


// The entry of \a location, with the next free FontId if it is new.
FontTextSystem::Known&
FontTextSystem::_KnownLocked(const FontLocation& location) const
{
	for (const std::unique_ptr<Known>& known : fKnown) {
		if (known->location == location)
			return *known;
	}
	auto known = std::make_unique<Known>();
	known->location = location;
	known->id = fNextId++;
	fKnown.push_back(std::move(known));
	return *fKnown.back();
}


// The font of \a known, loaded now if it is not yet; nullptr if it cannot
// be read (and is not tried again).
const FontTextSystem::Font*
FontTextSystem::_LoadLocked(Known& known) const
{
	if (known.font != nullptr || known.failed)
		return known.font;

	// HarfBuzz maps the file (and reads UTF-8 paths under Windows too)
	hb_blob_t* blob = hb_blob_create_from_file_or_fail(known.location.path.c_str());
	auto font = std::make_unique<Font>();
	if (blob == nullptr || known.location.faceIndex >= hb_face_count(blob)
		|| !font->Open(blob, known.location.faceIndex)) {
		if (font->blob == nullptr)
			hb_blob_destroy(blob);
		known.failed = true;
		return nullptr;
	}
	unsigned int length = 0;
	const char* data = hb_blob_get_data(blob, &length);
	font->file = {known.id, FontRole::Plain, std::as_bytes(std::span(data, length)),
		known.location.faceIndex};
	if (known.look.has_value())
		font->look = *known.look;
	else {
		// found as a fallback: named after its name table
		std::string family = FaceName(font->face, HB_OT_NAME_ID_TYPOGRAPHIC_FAMILY);
		if (family.empty())
			family = FaceName(font->face, HB_OT_NAME_ID_FONT_FAMILY);
		std::string style = FaceName(font->face, HB_OT_NAME_ID_TYPOGRAPHIC_SUBFAMILY);
		if (style.empty())
			style = FaceName(font->face, HB_OT_NAME_ID_FONT_SUBFAMILY);
		font->look = {std::move(family), std::move(style), known.id, BFontFace::Regular,
			false};
	}
	fFonts.push_back(std::move(font));
	known.font = fFonts.back().get();
	return known.font;
}


// The styles: the bundled ones, then those of \a listed that are not
// bundled, each with its FontId (loaded when it is used).
void
FontTextSystem::_ListLocked(std::vector<SourceFont> listed) const
{
	fStyles = BundledStyles();
	for (SourceFont& font : listed) {
		const std::pair key(font.family, font.style);
		if (fStyleIndex.contains(key)
			|| std::ranges::any_of(fStyles, [&](const FontFamilyStyle& style) {
				return style.family == font.family && style.style == font.style;
			})) {
			continue;
		}
		Known& known = _KnownLocked(font.location);
		known.look = FontFamilyStyle{font.family, font.style, known.id, font.face, font.fixed};
		fStyleIndex.emplace(key, fStyles.size());
		fStyles.push_back(*known.look);
	}
	for (std::size_t i = 0; i < 3; i++)
		fStyleIndex.emplace(std::pair(fStyles[i].family, fStyles[i].style), i);
	fListed = true;
}


// The stretches of \a text by font: their ends (byte offsets) and fonts.
// A grapheme cluster stays in one font.
std::vector<std::pair<std::size_t, const FontTextSystem::Font*>>
FontTextSystem::_Itemize(std::string_view text, const Font& primary) const
{
	const std::vector<CodePoint> codePoints = CodePoints(text);
	if (std::ranges::all_of(codePoints, [&](const CodePoint& c) { return primary.Has(c.value); }))
		return {{text.size(), &primary}};

	InitUnibreak();
	std::vector<char> breaks(text.size(), GRAPHEMEBREAK_NOBREAK);
	set_graphemebreaks_utf8(reinterpret_cast<const utf8_t*>(text.data()), text.size(), nullptr,
		breaks.data());
	const auto beginsCluster = [&](std::size_t offset) {
		return offset == 0 || breaks[offset - 1] != GRAPHEMEBREAK_NOBREAK;
	};

	std::vector<std::pair<std::size_t, const Font*>> pieces;
	std::vector<char32_t> cluster;
	const auto close = [&](std::size_t end) {
		if (cluster.empty())
			return;
		const Font* font = _FontFor(primary, cluster);
		if (font == nullptr)
			font = &primary;
		if (!pieces.empty() && pieces.back().second == font)
			pieces.back().first = end;
		else
			pieces.emplace_back(end, font);
		cluster.clear();
	};
	for (const CodePoint& codePoint : codePoints) {
		if (codePoint.offset > 0 && beginsCluster(codePoint.offset))
			close(codePoint.offset);
		cluster.push_back(codePoint.value);
	}
	close(text.size());
	return pieces;
}


std::vector<FontFamilyStyle>
FontTextSystem::Styles() const
{
	{
		std::scoped_lock lock(fLock);
		if (fListed)
			return fStyles;
	}
	// not under the lock: listing the system's fonts takes its time
	std::vector<SourceFont> listed;
	if (fSource != nullptr)
		listed = fSource->List();
	std::scoped_lock lock(fLock);
	if (!fListed)
		_ListLocked(std::move(listed));
	return fStyles;
}


std::optional<FontFamilyStyle>
FontTextSystem::Style(std::string_view family, std::string_view style) const
{
	{
		std::scoped_lock lock(fLock);
		if (fListed) {
			const auto found = fStyleIndex.find(std::pair(std::string(family),
				std::string(style)));
			if (found == fStyleIndex.end())
				return std::nullopt;
			return fStyles[found->second];
		}
	}
	(void)Styles();
	return Style(family, style);
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
	const Font& primary = _Font(spec.font);
	ShapedText shaped;
	shaped.font = primary.file.id;
	shaped.sizePx = spec.size;
	shaped.text.assign(text.begin(), text.end());
	if (text.empty())
		return shaped;

	// Each stretch in its font, with the whole text as context; the
	// clusters stay offsets in the whole text.
	const auto pieces = _Itemize(text, primary);
	std::size_t begin = 0;
	float pen = 0;
	for (const auto& [end, font] : pieces) {
		hb_buffer_t* buffer = hb_buffer_create();
		hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()),
			static_cast<unsigned int>(begin), static_cast<int>(end - begin));
		// Left to right until there is BiDi (module X3).
		hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
		hb_buffer_guess_segment_properties(buffer);
		hb_shape(font->font, buffer, nullptr, 0);

		unsigned int count = 0;
		const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
		const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
		const float scale = spec.size / font->unitsPerEm;
		float x = 0;
		for (unsigned int i = 0; i < count; i++) {
			shaped.glyphs.push_back(infos[i].codepoint);
			// HarfBuzz's y grows upwards, the Canvas's downwards.
			shaped.positions.push_back(
				{pen + (x + static_cast<float>(positions[i].x_offset)) * scale,
					-static_cast<float>(positions[i].y_offset) * scale});
			shaped.clusters.push_back(infos[i].cluster);
			x += static_cast<float>(positions[i].x_advance);
		}
		pen += x * scale;
		hb_buffer_destroy(buffer);
		if (pieces.size() > 1 || font != &primary)
			shaped.fontRuns.push_back({font->file.id, shaped.glyphs.size()});
		begin = end;
	}
	shaped.advance = pen;
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
	// character in the font it comes in, in em, and the width of the
	// ellipsis.
	const Font& primary = _Font(spec.font);
	std::vector<float> escapements;
	for (const CodePoint& codePoint : CodePoints(text)) {
		const char32_t single[] = {codePoint.value};
		const Font* font = _FontFor(primary, single);
		if (font == nullptr)
			font = &primary;
		hb_codepoint_t glyph = 0;
		(void)hb_font_get_nominal_glyph(font->font, codePoint.value, &glyph);
		escapements.push_back(
			static_cast<float>(hb_font_get_glyph_h_advance(font->font, glyph))
				/ font->unitsPerEm);
	}
	return TruncateString(text, mode, width, escapements, spec.size,
		DoStringWidth("\xE2\x80\xA6", spec));
}


std::vector<TextBoundaryFlags>
FontTextSystem::Boundaries(std::string_view text) const
{
	// libunibreak: UAX #14 for lines, UAX #29 for graphemes and words.
	InitUnibreak();

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


std::vector<FontFile>
FontTextSystem::Fonts() const
{
	std::scoped_lock lock(fLock);
	std::vector<FontFile> files;
	for (const std::unique_ptr<Font>& font : fFonts)
		files.push_back(font->file);
	return files;
}


} // namespace ngui::text
