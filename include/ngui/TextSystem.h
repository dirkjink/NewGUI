/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>


namespace ngui {


/*!	The fonts a look draws with, the counterparts of Haiku's be_plain_font,
	be_bold_font and be_fixed_font. */
enum class FontRole : std::uint8_t {
	Plain,	/*!< be_plain_font: labels, most controls. */
	Bold,	/*!< be_bold_font: titles, default buttons in some looks. */
	Fixed	/*!< be_fixed_font: monospaced. */
};


/*!	Vertical metrics of a font, as Haiku's font_height. */
struct FontHeight {
			float				ascent;		/*!< Above the baseline. */
			float				descent;	/*!< Below the baseline. */
			float				leading;	/*!< Extra space between lines. */
};


/*!	Where TextSystem::Truncate() cuts, as Haiku's B_TRUNCATE_* modes. */
enum class TruncationMode : std::uint8_t {
	End,		/*!< B_TRUNCATE_END: "Long te…" */
	Beginning,	/*!< B_TRUNCATE_BEGINNING: "…ong text" */
	Middle		/*!< B_TRUNCATE_MIDDLE: "Lon…ext" */
};


/*!	A font file of a text system, for the backends that draw its glyph runs:
	a backend makes its own font from the same file (specification 13.4,
	Backend::AddFont()). BWindow hands the fonts of TextSystem::Default() to
	its backend before the first frame. */
struct FontFile {
			gfx::FontId			id;		/*!< The font of the glyph runs. */
			FontRole			role;	/*!< The role it serves. */
			std::span<const std::byte> data;
									/*!< The font file; valid as long as the text system. */
};


/*!	Shaped text that owns its glyphs, positions, text and clusters; Run()
	views it as the GlyphRun the Canvas draws. */
struct ShapedText {
			gfx::FontId			font = 0;	/*!< The font of the run. */
			float				sizePx = 0;	/*!< Font size in logical pixels. */
			std::vector<std::uint32_t> glyphs;
									/*!< Glyph indices of the font file. */
			std::vector<gfx::Point> positions;
									/*!< Baseline position of each glyph, relative to the origin. */
			std::u8string		text;		/*!< The shaped text, UTF-8. */
			std::vector<std::uint32_t> clusters;
									/*!< Per glyph the byte offset of its cluster in #text. */
			float				advance = 0;
									/*!< Width of the whole run. */

			gfx::GlyphRun		Run() const;
									/*!< A view of this text as GlyphRun; valid as long as
										this object is not changed. */
};


/*!	What a look needs from the text subsystem (specification 10 and 18.3):
	metrics, widths, truncation, and shaping into glyph runs. Shaping, BiDi,
	kerning and line breaking all happen behind this interface; a look and
	the Canvas never see code points.

	Implemented by the text subsystem (modules X1 to X6, FreeType and
	HarfBuzz) and, for deterministic tests, by a test implementation with
	fixed metrics. Thread-safe: several window threads use it at once.
*/
class TextSystem {
public:
	virtual						~TextSystem() = default;

	virtual	float				FontSize(FontRole role) const = 0;
									/*!< Size of the font in logical pixels, as BFont::Size(). */
	virtual	FontHeight			Height(FontRole role) const = 0;
									/*!< Vertical metrics, as BFont::GetHeight(). */
	virtual	float				StringWidth(std::string_view text, FontRole role) const = 0;
									/*!< Width of \a text (UTF-8), as BFont::StringWidth(). */
	virtual	std::string			Truncate(std::string_view text, float width,
									TruncationMode mode, FontRole role) const = 0;
									/*!< \a text shortened with an ellipsis so that it fits
										\a width, as BFont::TruncateString(). */
	virtual	ShapedText			Shape(std::string_view text, FontRole role) const = 0;
									/*!< \a text shaped into glyphs, positions relative to the
										start of the baseline. */
	virtual	std::span<const FontFile> Fonts() const;
									/*!< The font files of the glyph runs of Shape(), for the
										backends; none by default, for text systems whose
										runs are not drawn (the placeholder, test doubles). */

	static	const TextSystem&	Default();
									/*!< The text system of the process: the one given to
										SetDefault(), otherwise a placeholder until the text
										subsystem exists (modules X1 to X6). The placeholder
										measures a 12 px font approximately (0.6 em per
										character, ascent 0.75 em, descent 0.25 em) and
										shapes nothing, so no text is drawn. */
	static	void				SetDefault(const TextSystem* text);
									/*!< Replaces the default; nullptr restores the
										placeholder. The object must outlive its use. */
};


} // namespace ngui
