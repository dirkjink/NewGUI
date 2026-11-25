/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Flags.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>


namespace ngui::gfx {
class Canvas;
struct Paint;
} // namespace ngui::gfx


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


/*!	The direction of a run of text (module X3). */
enum class TextDirection : std::uint8_t {
	LeftToRight,	/*!< Latin, Greek, Cyrillic, CJK, numbers ... */
	RightToLeft		/*!< Arabic, Hebrew ... */
};


/*!	A stretch of a line in one direction (UAX #9): its byte offsets, in the
	visual order of the line's runs. */
struct DirectionRun {
			std::size_t			start = 0;	/*!< Offset of its first byte. */
			std::size_t			end = 0;	/*!< Offset after its last byte. */
			TextDirection		direction = TextDirection::LeftToRight;
											/*!< Its direction. */

			bool				operator==(const DirectionRun&) const = default;
									/*!< The same stretch and direction. */
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
			std::uint32_t		faceIndex = 0;
									/*!< The face in a collection (.ttc); 0 otherwise. */
};


/*!	A stretch of the glyphs of a ShapedText in one font (module X1). */
struct ShapedFontRun {
			gfx::FontId			font = 0;	/*!< The font of the stretch. */
			std::size_t			end = 0;	/*!< The glyph after it; it begins where the
												stretch before it ends. */
};


/*!	Shaped text that owns its glyphs, positions, text and clusters; Runs()
	views it as the GlyphRuns the Canvas draws, one per font, and Draw()
	draws them. Characters the font asked for does not have come in the
	fonts of a fallback (module X1). The glyphs are in visual order, left to
	right; in right-to-left parts (module X3) the clusters decrease. */
struct ShapedText {
			gfx::FontId			font = 0;	/*!< The font asked for; the glyphs are in it
												unless #fontRuns says otherwise. */
			float				sizePx = 0;	/*!< Font size in logical pixels. */
			std::vector<std::uint32_t> glyphs;
									/*!< Glyph indices of the font file. */
			std::vector<gfx::Point> positions;
									/*!< Baseline position of each glyph, relative to the origin. */
			std::u8string		text;		/*!< The shaped text, UTF-8. */
			std::vector<std::uint32_t> clusters;
									/*!< Per glyph the byte offset of its cluster in #text. */
			float				advance = 0;
									/*!< Width of the whole text. */
			std::vector<ShapedFontRun> fontRuns;
									/*!< The fonts of the glyphs, in order, if not all are
										in #font; empty if they are. */

			std::vector<gfx::GlyphRun> Runs() const;
									/*!< Views of this text as GlyphRuns, one per stretch in
										a font (none without glyphs); valid as long as this
										object is not changed. Clusters stay offsets in the
										whole #text. */
			void				Draw(gfx::Canvas& canvas, gfx::Point origin,
									const gfx::Paint& paint) const;
									/*!< Draws all runs at \a origin (the start of the
										baseline). */
			gfx::FontId			FontOf(std::size_t glyph) const;
									/*!< The font of glyph \a glyph. */
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
	virtual	std::optional<FontFamilyStyle> Style(std::string_view family,
									std::string_view style) const;
									/*!< The style \a style of \a family, if it has a font
										for it; by default from Styles(). For BFont, which
										looks its font up for every measurement. */
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

			ShapedText			ShapeRun(std::string_view text, const FontSpec& font,
									TextDirection direction) const;
									/*!< \a text shaped as one run in \a direction, without
										the bidirectional algorithm (DoShapeRun()): for a
										layout that orders its runs itself. The glyphs
										are in visual order, left to right; a right-to-left
										run's clusters decrease. */
			std::vector<float>	CaretPositions(std::string_view text,
									const FontSpec& font, TextDirection direction) const;
									/*!< CaretPositions() of the run ShapeRun() gives: x
										relative to the run's left edge. A caret before a
										character of a right-to-left run stands at its
										right edge, so the first value is the run's width
										and the last 0. */
	virtual	std::vector<DirectionRun> VisualRuns(std::string_view paragraph,
									std::size_t lineStart, std::size_t lineEnd) const;
									/*!< The runs of the line from \a lineStart to
										\a lineEnd of \a paragraph (offsets in it), in
										visual order (UAX #9 with rule L1 and L2). By
										default one left-to-right run; the text subsystem
										follows Unicode with SheenBidi, the paragraph's
										direction from its first strong character. */
	virtual	TextDirection		ParagraphDirection(std::string_view paragraph) const;
									/*!< The direction of \a paragraph: that of its first
										strong character (UAX #9, rules P2 and P3), left to
										right without one. By default left to right. */
	virtual	std::vector<FontFile> Fonts() const;
									/*!< The font files of the glyph runs of Shape(), for the
										backends; none by default, for text systems whose
										runs are not drawn (the placeholder, test doubles).
										A text system may load fonts while shaping (a
										fallback); it only appends them, so a caller
										that has handed the first n to a backend hands
										the rest. */

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

	/*!	Told about each text a view cut to fit (the layout lint,
		specification 20.6): the text and what is shown of it. */
	using TruncationObserver = std::function<void(std::string_view text,
		std::string_view shown)>;

	static	void				SetTruncationObserver(TruncationObserver observer);
									/*!< Observes the cuts on the calling thread (a
										window's); nullptr ends it. */
	static	void				ReportTruncation(std::string_view text,
									std::string_view shown);
									/*!< Tells the observer of the calling thread, if
										\a shown differs from \a text; called where views
										cut their labels (BStringView, BStatusBar,
										BMenuItem, BFont::TruncateString()). */

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
	virtual	ShapedText			DoShapeRun(std::string_view text, const FontSpec& font,
									TextDirection direction) const;
									/*!< ShapeRun(); by default DoShape(), which is left to
										right in the defaults. */
	static	FontRole			RoleOf(gfx::FontId font);
									/*!< The role of font 1, 2 or 3 (others: Plain), for
										the defaults. */

private:
			std::vector<float>	CaretsOf(std::string_view text, const ShapedText& shaped,
									bool rightToLeft, const FontSpec& font) const;
};


} // namespace ngui
