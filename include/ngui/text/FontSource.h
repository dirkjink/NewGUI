/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>

#include <ngui/TextSystem.h>


namespace ngui::text {


/*!	Where a font is: a font file and the face in it. */
struct FontLocation {
			std::string			path;		/*!< The file, UTF-8. */
			std::uint32_t		faceIndex = 0;
									/*!< The face in a collection (.ttc); 0 otherwise. */

			bool				operator==(const FontLocation&) const = default;
									/*!< The same file and face. */
};


/*!	A font of the system, as a FontSource lists it. */
struct SourceFont {
			FontLocation		location;	/*!< Where it is. */
			std::string			family;		/*!< Its family, e.g. "DejaVu Serif". */
			std::string			style;		/*!< Its style, e.g. "Bold Italic". */
			BFontFaceFlags		face;		/*!< How it looks. */
			bool				fixed = false;
									/*!< Monospaced, as the system sees it (fontconfig:
										from the advances of the glyphs, so a font with a
										few wider glyphs may not be, and one with only
										ideographs may). */
};


/*!	The fonts of the system for a FontTextSystem (module X1): which fonts
	there are, and where to find a font for characters the fonts it has do
	not have. Implemented over the system's font services (fontconfig,
	DirectWrite) and, in tests, over fixed files. Thread-safe: several
	window threads shape at once.
*/
class FontSource {
public:
	virtual						~FontSource() = default;

	virtual	std::optional<FontLocation> Fallback(char32_t codePoint,
									const FontFamilyStyle& like) const = 0;
									/*!< A font that has \a codePoint, as like the font
										\a like as the system can find; nullopt if it has
										none. */
	virtual	std::vector<SourceFont> List() const;
									/*!< The fonts of the system, a family and style once,
										for Haiku's get_font_family(); none by default. */
};


/*!	The font source of this system: DirectWrite under Windows, fontconfig
	under Linux and other Unix systems (if its library is there), else none
	(nullptr); CoreText for macOS comes later. */
std::unique_ptr<FontSource> SystemFontSource();


/*!	A FontSource over fontconfig (Linux and other Unix systems except
	macOS), its library libfontconfig.so.1 loaded at run time, so that the
	system's configuration in /etc/fonts applies (a fontconfig of vcpkg
	would look for its own). \a configFile replaces that configuration
	(fonts.conf, for tests); empty for the system's. NotSupported on other
	systems or without the library, IoError if fontconfig cannot read its
	configuration.

	Fallback() asks fontconfig for the fonts sorted by how well they match
	the family, weight and slant of \a like (FcFontSort(), the system's
	preferences and aliases included) and takes the first TrueType or CFF
	font that has the character. List() lists those fonts. A variable font
	stands for its default instance. */
std::expected<std::unique_ptr<FontSource>, Error> CreateFontconfigSource(
	std::string_view configFile = {});


/*!	A FontSource over DirectWrite (Windows 8.1 and later), with the
	system's font collection. NotSupported on other systems, IoError if
	DirectWrite cannot be started.

	Fallback() asks the system's font fallback (IDWriteFontFallback) for the
	font Windows itself would take for the character after the family,
	weight and slant of \a like. List() lists the fonts of the system's
	collection, without the faces DirectWrite only simulates (an emboldened
	or slanted regular face) and fonts that are not local files. A variable
	font stands for its default instance. */
std::expected<std::unique_ptr<FontSource>, Error> CreateDirectWriteSource();


} // namespace ngui::text
