/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ngui/Error.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/text/FontSource.h>


namespace ngui::text {


/*!	The text subsystem (modules X1, X2, X4, X5; library ngui_text): fonts,
	shaping with HarfBuzz, line, word and grapheme boundaries with
	libunibreak, metrics and truncation, without hinting (specification
	13.4).

	CreateBundled() uses the fonts that are embedded into the library: Noto
	Sans and Noto Sans Bold for the plain and bold roles, Noto Sans Mono for
	the fixed one (data/fonts/, SIL Open Font License 1.1), the system font
	of Haiku. They give the same metrics on every system, so layouts and
	tests do not depend on the machine (specification 20.3).

	Fallback (module X1): characters a font does not have come in another
	font, a grapheme cluster (UAX #29) at a time: first in the other bundled
	fonts (Noto Sans Mono has arrows and symbols that Noto Sans lacks), then
	in the font a FontSource finds, which is loaded on first use and gets
	the next free FontId (4, 5, ...). Without a font for it, a character
	stays in its font, as its "missing" glyph. A shaped text then has
	several runs (ShapedText::fontRuns); Fonts() grows by the loaded fonts.

	Metrics as Haiku's BFont: Height() from the font's ascender, descender
	and line gap; StringWidth() is the advance of the shaped text, kerning
	included; Truncate() is Haiku's truncate_string() with the advances of
	the single characters (escapements), each in the font it comes in.
	Thread-safe: fonts are only added, and a loaded font is not changed.
*/
class FontTextSystem final : public TextSystem {
public:
	/*!	The size of all roles by default, in logical pixels: Haiku's 12. */
	static constexpr float kDefaultSize = 12;

	static	std::expected<std::unique_ptr<FontTextSystem>, Error> CreateBundled(
									float size = kDefaultSize);
									/*!< The text system with the bundled fonts at \a size,
										without fonts of the system; InvalidArgument for a
										size that is not positive, BadData if a font
										cannot be read. */
	static	std::expected<std::unique_ptr<FontTextSystem>, Error> Create(
									std::unique_ptr<FontSource> fonts,
									float size = kDefaultSize);
									/*!< CreateBundled() with the fallback fonts \a fonts
										finds (none if nullptr). */
								~FontTextSystem() override;
									/*!< Frees the fonts. */

			float				FontSize(FontRole role) const override;
									/*!< The size of \a role. */
			FontHeight			Height(FontRole role) const override;
									/*!< Ascent, descent and leading at that size. */
			float				StringWidth(std::string_view text, FontRole role)
									const override;
									/*!< The advance of \a text shaped. */
			std::string			Truncate(std::string_view text, float width,
									TruncationMode mode, FontRole role) const override;
									/*!< Haiku's truncation with "…". */
			ShapedText			Shape(std::string_view text, FontRole role) const override;
									/*!< Glyphs, positions and clusters (byte offsets in
										\a text) of \a text, in left-to-right order. */
			std::vector<TextBoundaryFlags> Boundaries(std::string_view text) const override;
									/*!< Unicode's boundaries (libunibreak): lines after
										UAX #14, graphemes and words after UAX #29;
										malformed UTF-8 is broken at each code point
										start. */

			std::vector<FontFamilyStyle> Styles() const override;
									/*!< "Noto Sans" Regular (font 1) and Bold (font 2),
										"Noto Sans Mono" Regular (font 3). */
			FontSpec			RoleFont(FontRole role) const override;
									/*!< Fonts 1 to 3 at the size of the text system. */
			std::vector<FontFile> Fonts() const override;
									/*!< The three bundled fonts, ids 1 to 3, then the
										fallback fonts loaded so far. */

			using				TextSystem::Height;
			using				TextSystem::StringWidth;
			using				TextSystem::Truncate;
			using				TextSystem::Shape;

protected:
			FontHeight			DoHeight(const FontSpec& font) const override;
									/*!< The font's ascender, descender and line gap at the
										size. */
			float				DoStringWidth(std::string_view text,
									const FontSpec& font) const override;
									/*!< The advance of \a text shaped. */
			std::string			DoTruncate(std::string_view text, float width,
									TruncationMode mode, const FontSpec& font) const override;
									/*!< Haiku's truncation with "…". */
			ShapedText			DoShape(std::string_view text,
									const FontSpec& font) const override;
									/*!< Glyphs, positions and clusters at the size. */

private:
			struct Font;

								FontTextSystem();

			const Font&			_Font(gfx::FontId id) const;
			const Font*			_FontFor(const Font& primary,
									std::span<const char32_t> cluster) const;
			const Font*			_SourceFont(const Font& primary, char32_t codePoint) const;
			const Font*			_Load(const FontLocation& location) const;
			std::vector<std::pair<std::size_t, const Font*>> _Itemize(std::string_view text,
									const Font& primary) const;

			mutable std::mutex	fLock;
			mutable std::vector<std::unique_ptr<Font>> fFonts;
									// guarded by fLock; only appended
			mutable std::map<std::pair<gfx::FontId, char32_t>, gfx::FontId> fFallbacks;
									// what the source found, 0 for nothing
			mutable std::vector<std::pair<FontLocation, gfx::FontId>> fLoaded;
									// the files loaded, 0 for unreadable
			std::unique_ptr<FontSource> fSource;
			float				fSize = kDefaultSize;
};


} // namespace ngui::text
