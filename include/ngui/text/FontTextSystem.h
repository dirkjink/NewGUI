/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/GlyphRun.h>


namespace ngui::text {


/*!	The text subsystem (modules X1, X2, X4, X5; library ngui_text): fonts,
	shaping with HarfBuzz, line, word and grapheme boundaries with
	libunibreak, metrics and truncation, without hinting (specification
	13.4).

	CreateBundled() uses the fonts that are embedded into the library: Noto
	Sans and Noto Sans Bold for the plain and bold roles, Noto Sans Mono for
	the fixed one (data/fonts/, SIL Open Font License 1.1), the system font
	of Haiku. They give the same metrics on every system, so layouts and
	tests do not depend on the machine (specification 20.3). Fonts of the
	system and fallback chains for other scripts come later.

	Metrics as Haiku's BFont: Height() from the font's ascender, descender
	and line gap; StringWidth() is the advance of the shaped text, kerning
	included; Truncate() is Haiku's truncate_string() with the advances of
	the single characters (escapements). Thread-safe: the fonts are not
	changed after construction.
*/
class FontTextSystem final : public TextSystem {
public:
	/*!	The size of all roles by default, in logical pixels: Haiku's 12. */
	static constexpr float kDefaultSize = 12;

	static	std::expected<std::unique_ptr<FontTextSystem>, Error> CreateBundled(
									float size = kDefaultSize);
									/*!< The text system with the bundled fonts at \a size;
										InvalidArgument for a size that is not positive,
										BadData if a font cannot be read. */
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

			std::span<const FontFile> Fonts() const override;
									/*!< The three bundled fonts, ids 1 to 3. */

private:
			struct Font;

								FontTextSystem();

			const Font&			_Font(FontRole role) const;

			std::vector<std::unique_ptr<Font>> fFonts;
			std::vector<FontFile> fFiles;
			float				fSize = kDefaultSize;
};


} // namespace ngui::text
