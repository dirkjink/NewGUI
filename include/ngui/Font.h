/*
 * Copyright 2005-2009, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/TextSystem.h>


namespace ngui {


/*!	A font: a family and style at a size (Haiku's BFont), a facade to the
	text system (TextSystem::Default()). It names its font; measuring
	resolves the name to the text system's font file (Spec()), so a BFont
	stays valid when the text system changes.

	The fonts of the roles: Plain(), Bold() and Fixed() (Haiku's
	be_plain_font, be_bold_font and be_fixed_font); the families and styles
	there are: Families() and Styles() (Haiku's count_font_families(),
	get_font_family() and the style counterparts).

	Differences to Haiku: no shear, rotation, spacing, encoding, flags or
	escapements (the text system draws neither slanted nor rotated text);
	SetFamilyAndStyle() and SetFamilyAndFace() report an unknown family or
	style as NotFound (Haiku: B_ERROR) and leave the font as it was; a face
	the family has no style for leaves the style (Haiku asks the
	app_server, which picks the nearest).
*/
class BFont {
public:
								BFont();
									/*!< The plain font (be_plain_font). */

	static	BFont				Plain();
									/*!< The font of labels and most controls
										(be_plain_font). */
	static	BFont				Bold();
									/*!< The font of titles (be_bold_font). */
	static	BFont				Fixed();
									/*!< The monospaced font (be_fixed_font). */
	static	BFont				OfRole(FontRole role);
									/*!< The font of \a role at its size. */

			std::expected<void, Error> SetFamilyAndStyle(std::string_view family,
									std::string_view style);
									/*!< Family and style; an empty family keeps the
										family, an empty style takes the family's regular
										style (or its first). */
			std::expected<void, Error> SetFamilyAndFace(std::string_view family,
									BFontFaceFlags face);
									/*!< The style of \a family that has \a face; an empty
										family keeps the family. */
			void				SetSize(float size);
									/*!< In logical pixels; a size not above 0 is
										ignored. */
			void				SetFace(BFontFaceFlags face);
									/*!< The style of the family with \a face (only Bold
										and Italic choose; other flags are ignored). */

			void				GetFamilyAndStyle(std::string* family,
									std::string* style) const;
									/*!< Its family and style. */
			float				Size() const;
									/*!< Its size. */
			BFontFaceFlags		Face() const;
									/*!< The face of its style. */
			bool				IsFixed() const;
									/*!< Whether it is monospaced. */

			void				GetHeight(FontHeight* height) const;
									/*!< Ascent, descent and leading. */
			float				StringWidth(std::string_view string) const;
									/*!< The width of \a string (UTF-8). */
			void				TruncateString(std::string* inOut, TruncationMode mode,
									float width) const;
									/*!< Shortens \a inOut with an ellipsis to fit
										\a width. */

			FontSpec			Spec() const;
									/*!< The text system's font file and the size: what it
										measures and shapes. */

			bool				operator==(const BFont& other) const;
									/*!< Same family, style and size. */

	static	std::vector<std::string> Families();
									/*!< The font families, in the text system's order. */
	static	std::vector<std::string> Styles(std::string_view family);
									/*!< The styles of \a family. */

private:
								BFont(std::string family, std::string style, float size);

			std::string			fFamily;
			std::string			fStyle;
			float				fSize;
};


} // namespace ngui
