/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BFont as far as the ported looks use it: size, width, height and
// truncation, over the TextSystem of the DrawContext. Metrics scale linearly
// from the role's own size to the size set here; that is exact without
// hinting, and hinting is off (specification 13.4).


#include <string>

#include <ngui/SupportDefs.h>
#include <ngui/TextSystem.h>


namespace ngui::compat {


struct font_height {
	float	ascent;
	float	descent;
	float	leading;
};


// Haiku's truncation modes.
enum {
	B_TRUNCATE_END = 0,
	B_TRUNCATE_BEGINNING = 1,
	B_TRUNCATE_MIDDLE = 2,
	B_TRUNCATE_SMART = 3
};


class BFont {
public:
								BFont();
									// Without metrics until GetFont() fills it, as in
									// `BFont font; view->GetFont(&font);`.
								BFont(const TextSystem& text, FontRole role = FontRole::Plain);

			float				Size() const;
			void				SetSize(float size);
			FontRole			Role() const;
			const TextSystem&	Text() const;

			float				StringWidth(const char* string) const;
			void				GetHeight(font_height* height) const;
			void				TruncateString(std::string* inOut, uint32 mode,
									float width) const;

			// The scale from the role's size to this font's size.
			float				Scale() const;

private:
	static	const TextSystem&	_NoText();

			const TextSystem*	fText;
			FontRole			fRole;
			float				fSize;
};


inline const TextSystem&
BFont::_NoText()
{
	class NoText : public TextSystem {
	public:
		float FontSize(FontRole) const override
		{
			return 0;
		}

		FontHeight Height(FontRole) const override
		{
			return {0, 0, 0};
		}

		float StringWidth(std::string_view, FontRole) const override
		{
			return 0;
		}

		std::string Truncate(std::string_view text, float, TruncationMode,
			FontRole) const override
		{
			return std::string(text);
		}

		ShapedText Shape(std::string_view, FontRole) const override
		{
			return {};
		}
	};
	static const NoText kNoText;
	return kNoText;
}


inline
BFont::BFont()
	:
	BFont(_NoText())
{
}


inline
BFont::BFont(const TextSystem& text, FontRole role)
	:
	fText(&text),
	fRole(role),
	fSize(text.FontSize(role))
{
}


inline float
BFont::Size() const
{
	return fSize;
}


inline void
BFont::SetSize(float size)
{
	if (size > 0)
		fSize = size;
}


inline FontRole
BFont::Role() const
{
	return fRole;
}


inline const TextSystem&
BFont::Text() const
{
	return *fText;
}


inline float
BFont::Scale() const
{
	const float roleSize = fText->FontSize(fRole);
	return roleSize > 0 ? fSize / roleSize : 1.0f;
}


inline float
BFont::StringWidth(const char* string) const
{
	return fText->StringWidth(string != nullptr ? string : "", fRole) * Scale();
}


inline void
BFont::GetHeight(font_height* height) const
{
	const FontHeight metrics = fText->Height(fRole);
	const float scale = Scale();
	*height = {metrics.ascent * scale, metrics.descent * scale, metrics.leading * scale};
}


inline void
BFont::TruncateString(std::string* inOut, uint32 mode, float width) const
{
	const TruncationMode truncation = mode == B_TRUNCATE_BEGINNING ? TruncationMode::Beginning
		: mode == B_TRUNCATE_MIDDLE ? TruncationMode::Middle : TruncationMode::End;
	*inOut = fText->Truncate(*inOut, width / Scale(), truncation, fRole);
}


} // namespace ngui::compat
