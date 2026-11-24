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

#include <ngui/Flags.h>
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


/*!	How a face of a font looks, as Haiku's B_*_FACE flags. A text system
	offers the faces it has fonts for; the bundled fonts have Regular and
	Bold. */
enum class BFontFace : std::uint16_t {
	Italic = 0x0001,		/*!< B_ITALIC_FACE */
	Underscore = 0x0002,	/*!< B_UNDERSCORE_FACE */
	Negative = 0x0004,		/*!< B_NEGATIVE_FACE */
	Outlined = 0x0008,		/*!< B_OUTLINED_FACE */
	Strikeout = 0x0010,		/*!< B_STRIKEOUT_FACE */
	Bold = 0x0020,			/*!< B_BOLD_FACE */
	Regular = 0x0040,		/*!< B_REGULAR_FACE */
	Condensed = 0x0080,		/*!< B_CONDENSED_FACE */
	Light = 0x0100,			/*!< B_LIGHT_FACE */
	Heavy = 0x0200			/*!< B_HEAVY_FACE */
};
NGUI_FLAG_ENUM(BFontFace)

/*!	A set of BFontFace. */
using BFontFaceFlags = Flags<BFontFace>;


/*!	A style of a font family that a text system has a font for (Haiku: a
	font_family with a font_style, its face and B_IS_FIXED). */
struct FontFamilyStyle {
			std::string			family;		/*!< The family, e.g. "Noto Sans". */
			std::string			style;		/*!< The style, e.g. "Bold". */
			gfx::FontId			font;		/*!< The font file of the style. */
			BFontFaceFlags		face;		/*!< How it looks. */
			bool				fixed;		/*!< Monospaced. */
};


/*!	A font at a size, as the text system measures and shapes it: what a
	BFont comes down to. */
struct FontSpec {
			gfx::FontId			font;		/*!< The font file (FontFamilyStyle::font). */
			float				size;		/*!< In logical pixels. */
};


/*!	Vertical metrics of a font, as Haiku's font_height. */
struct FontHeight {
			float				ascent;		/*!< Above the baseline. */
			float				descent;	/*!< Below the baseline. */
			float				leading;	/*!< Extra space between lines. */
};


/*!	The boundaries in a text (module X4), for TextSystem::Boundaries(): a
	flag at offset i stands for the boundary before byte i. */
enum class TextBoundary : std::uint8_t {
	Grapheme = 1u << 0,			/*!< A caret may stand here: between user-perceived
									characters (UAX #29), never inside a code point. */
	Line = 1u << 1,				/*!< A line may begin here (UAX #14). */
	MandatoryLine = 1u << 2,	/*!< A line must begin here: after a line break. */
	Word = 1u << 3				/*!< A word, or the run between two words, begins or
									ends here (UAX #29). */
};
NGUI_FLAG_ENUM(TextBoundary)

/*!	A set of TextBoundary. */
using TextBoundaryFlags = Flags<TextBoundary>;


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
	virtual	std::vector<TextBoundaryFlags> Boundaries(std::string_view text) const;
									/*!< The boundaries of \a text (UTF-8): text.size() + 1
										flags, the first and last with Grapheme and Word.
										By default simple rules: a grapheme per code
										point, lines after spaces and tabs, mandatory
										after "\n", words of ASCII letters, digits, "_"
										and non-ASCII characters. The text subsystem
										follows Unicode (UAX #14 and #29, libunibreak). */
			std::vector<float>	CaretPositions(std::string_view text, FontRole role) const;
									/*!< The x of a caret before each byte of \a text and
										after it (text.size() + 1 values), relative to the
										start, left to right: from the clusters of
										Shape(), shared evenly by the code points of a
										cluster; without clusters from StringWidth() of
										each beginning. A byte inside a code point has the
										value of its start. */
	virtual	std::vector<FontFamilyStyle> Styles() const;
									/*!< The families and styles it has fonts for, as
										Haiku's get_font_family() and get_font_style().
										By default the three roles: "Default" (Regular,
										font 1; Bold, font 2) and "Default Mono" (Regular,
										font 3). */
	virtual	FontSpec			RoleFont(FontRole role) const;
									/*!< The font of \a role at its size. By default font
										1, 2 or 3 at FontSize(). */
			FontHeight			Height(const FontSpec& font) const;
									/*!< Vertical metrics of \a font (DoHeight()). */
			float				StringWidth(std::string_view text, const FontSpec& font) const;
									/*!< Width of \a text in \a font (DoStringWidth()). */
			std::string			Truncate(std::string_view text, float width,
									TruncationMode mode, const FontSpec& font) const;
									/*!< Truncate() in \a font (DoTruncate()). */
			ShapedText			Shape(std::string_view text, const FontSpec& font) const;
									/*!< \a text shaped in \a font (DoShape()). */
			std::vector<float>	CaretPositions(std::string_view text,
									const FontSpec& font) const;
									/*!< CaretPositions() in \a font. */
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

protected:
	virtual	FontHeight			DoHeight(const FontSpec& font) const;
									/*!< Height() of a FontSpec. By default the metrics of
										the role of its font file, scaled to its size. */
	virtual	float				DoStringWidth(std::string_view text,
									const FontSpec& font) const;
									/*!< StringWidth() in a FontSpec; by default scaled
										from the role. */
	virtual	std::string			DoTruncate(std::string_view text, float width,
									TruncationMode mode, const FontSpec& font) const;
									/*!< Truncate() in a FontSpec; by default by the role
										with the width scaled. */
	virtual	ShapedText			DoShape(std::string_view text, const FontSpec& font) const;
									/*!< Shape() in a FontSpec; by default the role's run,
										scaled. */
	static	FontRole			RoleOf(gfx::FontId font);
									/*!< The role of font 1, 2 or 3 (others: Plain), for
										the defaults. */
};


} // namespace ngui
