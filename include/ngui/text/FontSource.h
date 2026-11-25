/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <optional>
#include <string>

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


/*!	The fonts of the system for a FontTextSystem (module X1): where to find
	a font for characters the fonts it has do not have. Implemented over the
	system's font services (fontconfig, DirectWrite) and, in tests, over
	fixed files. Thread-safe: several window threads shape at once.
*/
class FontSource {
public:
	virtual						~FontSource() = default;

	virtual	std::optional<FontLocation> Fallback(char32_t codePoint,
									const FontFamilyStyle& like) const = 0;
									/*!< A font that has \a codePoint, as like the font
										\a like as the system can find; nullopt if it has
										none. */
};


} // namespace ngui::text
