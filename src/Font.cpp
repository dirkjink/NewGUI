/*
 * Copyright 2001-2016, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * NewGUI's BFont (see Font.h): Haiku's API over the text system instead of
 * the app_server. Family and style are kept by name and resolved against
 * TextSystem::Default() when the font is measured.
 */


#include <ngui/Font.h>

#include <algorithm>
#include <optional>
#include <utility>


namespace ngui {


namespace {


// The style \a family / \a style of \a text, or nullptr.
const FontFamilyStyle*
FindStyle(const std::vector<FontFamilyStyle>& styles, std::string_view family,
	std::string_view style)
{
	for (const FontFamilyStyle& entry : styles) {
		if (entry.family == family && entry.style == style)
			return &entry;
	}
	return nullptr;
}


// The style of \a family whose face has the bold and italic of \a face, or
// nullptr.
const FontFamilyStyle*
FindFace(const std::vector<FontFamilyStyle>& styles, std::string_view family,
	BFontFaceFlags face)
{
	const bool bold = face.Has(BFontFace::Bold);
	const bool italic = face.Has(BFontFace::Italic);
	for (const FontFamilyStyle& entry : styles) {
		if (entry.family == family && entry.face.Has(BFontFace::Bold) == bold
			&& entry.face.Has(BFontFace::Italic) == italic) {
			return &entry;
		}
	}
	return nullptr;
}


} // namespace


BFont::BFont()
	:
	BFont(Plain())
{
}


BFont
BFont::Plain()
{
	return OfRole(FontRole::Plain);
}


BFont
BFont::Bold()
{
	return OfRole(FontRole::Bold);
}


BFont
BFont::Fixed()
{
	return OfRole(FontRole::Fixed);
}


BFont
BFont::OfRole(FontRole role)
{
	const TextSystem& text = TextSystem::Default();
	const FontSpec spec = text.RoleFont(role);
	for (const FontFamilyStyle& entry : text.Styles()) {
		if (entry.font == spec.font)
			return BFont(entry.family, entry.style, spec.size);
	}
	return BFont({}, {}, spec.size);
}


BFont::BFont(std::string family, std::string style, float size)
	:
	fFamily(std::move(family)),
	fStyle(std::move(style)),
	fSize(size)
{
}


std::expected<void, Error>
BFont::SetFamilyAndStyle(std::string_view family, std::string_view style)
{
	const std::vector<FontFamilyStyle> styles = TextSystem::Default().Styles();
	if (family.empty())
		family = fFamily;

	const FontFamilyStyle* found = nullptr;
	if (style.empty()) {
		found = FindFace(styles, family, BFontFace::Regular);
		if (found == nullptr) {
			const auto first = std::find_if(styles.begin(), styles.end(),
				[family](const FontFamilyStyle& entry) { return entry.family == family; });
			if (first != styles.end())
				found = &*first;
		}
	} else
		found = FindStyle(styles, family, style);

	if (found == nullptr)
		return Failure(ErrorCode::NotFound, "no such font family or style");

	fFamily = found->family;
	fStyle = found->style;
	return {};
}


std::expected<void, Error>
BFont::SetFamilyAndFace(std::string_view family, BFontFaceFlags face)
{
	const std::vector<FontFamilyStyle> styles = TextSystem::Default().Styles();
	if (family.empty())
		family = fFamily;

	const FontFamilyStyle* found = FindFace(styles, family, face);
	if (found == nullptr)
		return Failure(ErrorCode::NotFound, "no such font family or face");

	fFamily = found->family;
	fStyle = found->style;
	return {};
}


void
BFont::SetSize(float size)
{
	if (size > 0)
		fSize = size;
}


void
BFont::SetFace(BFontFaceFlags face)
{
	(void)SetFamilyAndFace(fFamily, face);
}


void
BFont::GetFamilyAndStyle(std::string* family, std::string* style) const
{
	if (family != nullptr)
		*family = fFamily;
	if (style != nullptr)
		*style = fStyle;
}


float
BFont::Size() const
{
	return fSize;
}


BFontFaceFlags
BFont::Face() const
{
	const std::optional<FontFamilyStyle> entry = TextSystem::Default().Style(fFamily, fStyle);
	return entry.has_value() ? entry->face : BFontFaceFlags(BFontFace::Regular);
}


bool
BFont::IsFixed() const
{
	const std::optional<FontFamilyStyle> entry = TextSystem::Default().Style(fFamily, fStyle);
	return entry.has_value() && entry->fixed;
}


void
BFont::GetHeight(FontHeight* height) const
{
	if (height != nullptr)
		*height = TextSystem::Default().Height(Spec());
}


float
BFont::StringWidth(std::string_view string) const
{
	return TextSystem::Default().StringWidth(string, Spec());
}


void
BFont::TruncateString(std::string* inOut, TruncationMode mode, float width) const
{
	if (inOut == nullptr)
		return;
	*inOut = TextSystem::Default().Truncate(*inOut, width, mode, Spec());
}


FontSpec
BFont::Spec() const
{
	const TextSystem& text = TextSystem::Default();
	if (const std::optional<FontFamilyStyle> entry = text.Style(fFamily, fStyle))
		return {entry->font, fSize};

	// a font the text system does not have: its plain font
	return {text.RoleFont(FontRole::Plain).font, fSize};
}


bool
BFont::operator==(const BFont& other) const
{
	return fFamily == other.fFamily && fStyle == other.fStyle && fSize == other.fSize;
}


std::vector<std::string>
BFont::Families()
{
	std::vector<std::string> families;
	for (const FontFamilyStyle& entry : TextSystem::Default().Styles()) {
		if (std::find(families.begin(), families.end(), entry.family) == families.end())
			families.push_back(entry.family);
	}
	return families;
}


std::vector<std::string>
BFont::Styles(std::string_view family)
{
	std::vector<std::string> styles;
	for (const FontFamilyStyle& entry : TextSystem::Default().Styles()) {
		if (entry.family == family)
			styles.push_back(entry.style);
	}
	return styles;
}


} // namespace ngui
