/*
 * Copyright 2001-2006, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The styles of a BTextView's text (W5f): Haiku's BTextView::StyleBuffer
// with its "null style", the style of text inserted without styles of its
// own. Internal.


#include <optional>
#include <vector>

#include <ngui/Font.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Color.h>


namespace ngui {


// A font and a color; no color is the text view's text color (NewGUI:
// Haiku keeps a color in every style).
struct TextStyle {
	BFont						font;
	std::optional<gfx::Color>	color;

	bool operator==(const TextStyle& other) const = default;
};


// A style from an offset on (Haiku's STEStyleRun).
struct TextStyleRunEntry {
	int32		offset;
	TextStyle	style;
};


// Ported from Haiku: src/kits/interface/textview_support/StyleBuffer.cpp
// (commit b1d497bb69). NewGUI: one vector of runs, styles compared by value
// (Haiku: run descriptions pointing into a buffer of shared style
// records); no ascent and descent per record (the TextLayout measures).
class TextStyleBuffer {
public:
								TextStyleBuffer(const BFont& font,
									std::optional<gfx::Color> color);

			void				InvalidateNullStyle();
			bool				IsValidNullStyle() const;
			// The null style becomes the style at \a offset, if it is not
			// valid.
			void				SyncNullStyle(int32 offset);
			void				SetNullStyle(BFontModeFlags mode, const BFont* font,
									const std::optional<gfx::Color>* color, int32 offset);
			const TextStyle&	NullStyle() const;

			// \a font and \a color, as far as \a mode says, from \a from to
			// \a to of a text of \a textLength; an empty range sets the null
			// style.
			void				SetStyleRange(int32 from, int32 to, int32 textLength,
									BFontModeFlags mode, const BFont* font,
									const std::optional<gfx::Color>* color);
			TextStyle			GetStyle(int32 offset) const;
			// The runs from \a start to \a end (inclusive, as Haiku's), their
			// offsets relative to \a start.
			std::vector<TextStyleRunEntry> GetStyleRange(int32 start, int32 end) const;
			void				RemoveStyleRange(int32 from, int32 to);
			// The font common to \a from to \a to: \a mode keeps what all
			// runs share; \a sameColor whether they have one color.
			void				ContinuousGetStyle(BFont* font, BFontModeFlags* mode,
									std::optional<gfx::Color>* color, bool* sameColor,
									int32 from, int32 to) const;

			int32				OffsetToRun(int32 offset) const;
			void				BumpOffset(int32 delta, int32 index);
			int32				NumRuns() const;
			const TextStyleRunEntry& RunAt(int32 index) const;

private:
			void				_RemoveRuns(int32 index, int32 count = 1);

			std::vector<TextStyleRunEntry> fRuns;
			TextStyle			fNullStyle;
			bool				fValidNullStyle = true;
};


} // namespace ngui
