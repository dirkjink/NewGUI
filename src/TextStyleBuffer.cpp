/*
 * Copyright 2001-2006, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/textview_support/StyleBuffer.cpp
 * (commit b1d497bb69); see TextStyleBuffer.h.
 *
 * Authors (Haiku):
 *		Marc Flerackers, mflerackers@androme.be
 *		Stefano Ceccherini, burton666@libero.it
 */


#include "TextStyleBuffer.h"

#include <algorithm>
#include <string>


namespace ngui {


namespace {


void
SetStyleFromMode(BFontModeFlags mode, const BFont* fromFont, BFont* toFont,
	const std::optional<gfx::Color>* fromColor, std::optional<gfx::Color>* toColor)
{
	if (fromFont != nullptr && toFont != nullptr) {
		if (mode.Has(BFontMode::FamilyAndStyle)) {
			std::string family;
			std::string style;
			fromFont->GetFamilyAndStyle(&family, &style);
			(void)toFont->SetFamilyAndStyle(family, style);
		}

		if (mode.Has(BFontMode::Face))
			toFont->SetFace(fromFont->Face());

		if (mode.Has(BFontMode::Size))
			toFont->SetSize(fromFont->Size());

		// NewGUI: no shear and false bold width
	}

	if (fromColor != nullptr && toColor != nullptr && (mode.Raw() == 0 || mode == kFontAll))
		*toColor = *fromColor;
}


// Haiku's FixupMode(): clears from \a mode what the styles do not share.
// NewGUI: family and style are compared by name (Haiku compared the whole
// fonts, so that different sizes cleared B_FONT_FAMILY_AND_STYLE too).
void
FixupMode(const TextStyle& firstStyle, const TextStyle& otherStyle, BFontModeFlags& mode,
	bool& sameColor)
{
	if (mode.Has(BFontMode::FamilyAndStyle)) {
		std::string firstFamily;
		std::string firstStyleName;
		std::string otherFamily;
		std::string otherStyleName;
		firstStyle.font.GetFamilyAndStyle(&firstFamily, &firstStyleName);
		otherStyle.font.GetFamilyAndStyle(&otherFamily, &otherStyleName);
		if (firstFamily != otherFamily || firstStyleName != otherStyleName)
			mode &= ~BFontModeFlags(BFontMode::FamilyAndStyle);
	}
	if (mode.Has(BFontMode::Size)) {
		if (firstStyle.font.Size() != otherStyle.font.Size())
			mode &= ~BFontModeFlags(BFontMode::Size);
	}
	if (firstStyle.color != otherStyle.color)
		sameColor = false;
}


} // namespace


TextStyleBuffer::TextStyleBuffer(const BFont& font, std::optional<gfx::Color> color)
	:
	fNullStyle{font, color}
{
}


void
TextStyleBuffer::InvalidateNullStyle()
{
	fValidNullStyle = false;
}


bool
TextStyleBuffer::IsValidNullStyle() const
{
	return fValidNullStyle;
}


void
TextStyleBuffer::SyncNullStyle(int32 offset)
{
	if (fValidNullStyle || fRuns.empty())
		return;

	fNullStyle = fRuns[static_cast<std::size_t>(OffsetToRun(offset))].style;

	fValidNullStyle = true;
}


void
TextStyleBuffer::SetNullStyle(BFontModeFlags mode, const BFont* font,
	const std::optional<gfx::Color>* color, int32 offset)
{
	if (!fValidNullStyle && !fRuns.empty())
		fNullStyle = fRuns[static_cast<std::size_t>(OffsetToRun(offset - 1))].style;

	SetStyleFromMode(mode, font, &fNullStyle.font, color, &fNullStyle.color);

	fValidNullStyle = true;
}


const TextStyle&
TextStyleBuffer::NullStyle() const
{
	return fNullStyle;
}


void
TextStyleBuffer::SetStyleRange(int32 fromOffset, int32 toOffset, int32 textLen,
	BFontModeFlags mode, const BFont* font, const std::optional<gfx::Color>* color)
{
	// Copies, as Haiku's pointers into the null style stay valid while it
	// changes.
	const TextStyle nullStyle = fNullStyle;
	if (font == nullptr)
		font = &nullStyle.font;

	if (color == nullptr)
		color = &nullStyle.color;

	if (fromOffset == toOffset) {
		SetNullStyle(mode, font, color, fromOffset);
		return;
	}

	// NewGUI: within the text; a reversed range styles nothing
	toOffset = std::min(toOffset, textLen);
	if (fromOffset >= toOffset)
		return;

	if (fRuns.empty()) {
		// the first run: the font and color as given
		fRuns.push_back({fromOffset, TextStyle{*font, *color}});
		return;
	}

	int32 offset = fromOffset;
	int32 runIndex = OffsetToRun(offset);
	TextStyle lastStyle;
	do {
		const TextStyleRunEntry runDesc = fRuns[static_cast<std::size_t>(runIndex)];
		int32 runEnd = textLen;
		if (runIndex < NumRuns() - 1)
			runEnd = fRuns[static_cast<std::size_t>(runIndex + 1)].offset;

		TextStyle style = runDesc.style;
		SetStyleFromMode(mode, font, &style.font, color, &style.color);
		lastStyle = style;

		if (runDesc.offset == offset && runIndex > 0
			&& fRuns[static_cast<std::size_t>(runIndex - 1)].style == style) {
			_RemoveRuns(runIndex);
			runIndex--;
		}

		if (style != runDesc.style) {
			if (offset > runDesc.offset) {
				fRuns.insert(fRuns.begin() + runIndex + 1, {offset, style});
				runIndex++;
			} else
				fRuns[static_cast<std::size_t>(runIndex)].style = style;

			if (toOffset < runEnd)
				fRuns.insert(fRuns.begin() + runIndex + 1, {toOffset, runDesc.style});
		}

		runIndex++;
		offset = runEnd;
	} while (offset < toOffset);

	if (offset == toOffset && runIndex < NumRuns()
		&& fRuns[static_cast<std::size_t>(runIndex)].style == lastStyle) {
		_RemoveRuns(runIndex);
	}
}


TextStyle
TextStyleBuffer::GetStyle(int32 offset) const
{
	if (fRuns.empty())
		return fNullStyle;

	return fRuns[static_cast<std::size_t>(OffsetToRun(offset))].style;
}


std::vector<TextStyleRunEntry>
TextStyleBuffer::GetStyleRange(int32 startOffset, int32 endOffset) const
{
	if (fRuns.empty())
		return {{0, fNullStyle}};

	const int32 startIndex = OffsetToRun(startOffset);
	const int32 endIndex = OffsetToRun(endOffset);

	int32 numStyles = endIndex - startIndex + 1;
	if (numStyles < 1)
		numStyles = 1;

	std::vector<TextStyleRunEntry> result;
	for (int32 index = 0; index < numStyles; index++) {
		TextStyleRunEntry run = fRuns[static_cast<std::size_t>(startIndex + index)];
		run.offset -= startOffset;
		if (run.offset < 0)
			run.offset = 0;
		result.push_back(run);
	}

	return result;
}


void
TextStyleBuffer::RemoveStyleRange(int32 fromOffset, int32 toOffset)
{
	if (fRuns.empty())
		return;

	int32 fromIndex = OffsetToRun(fromOffset);
	int32 toIndex = OffsetToRun(toOffset) - 1;

	const int32 count = toIndex - fromIndex;
	if (count > 0) {
		_RemoveRuns(fromIndex + 1, count);
		toIndex = fromIndex;
	}

	BumpOffset(fromOffset - toOffset, fromIndex + 1);

	if (toIndex == fromIndex && toIndex < NumRuns() - 1)
		fRuns[static_cast<std::size_t>(toIndex + 1)].offset = fromOffset;

	if (fromIndex < NumRuns() - 1) {
		if (fRuns[static_cast<std::size_t>(fromIndex)].offset
				== fRuns[static_cast<std::size_t>(fromIndex + 1)].offset) {
			_RemoveRuns(fromIndex);
			fromIndex--;
		}
	}

	if (fromIndex >= 0 && fromIndex < NumRuns() - 1) {
		if (fRuns[static_cast<std::size_t>(fromIndex)].style
				== fRuns[static_cast<std::size_t>(fromIndex + 1)].style) {
			_RemoveRuns(fromIndex + 1);
		}
	}
}


void
TextStyleBuffer::ContinuousGetStyle(BFont* outFont, BFontModeFlags* ioMode,
	std::optional<gfx::Color>* outColor, bool* sameColor, int32 fromOffset,
	int32 toOffset) const
{
	BFontModeFlags mode = kFontAll;

	if (fRuns.empty()) {
		if (ioMode != nullptr)
			*ioMode = mode;
		if (outFont != nullptr)
			*outFont = fNullStyle.font;
		if (outColor != nullptr)
			*outColor = fNullStyle.color;
		if (sameColor != nullptr)
			*sameColor = true;
		return;
	}

	const int32 fromIndex = OffsetToRun(fromOffset);
	const int32 toIndex = OffsetToRun(toOffset - 1);

	if (fromIndex == toIndex) {
		const TextStyle& style = fRuns[static_cast<std::size_t>(fromIndex)].style;

		if (ioMode != nullptr)
			*ioMode = mode;
		if (outFont != nullptr)
			*outFont = style.font;
		if (outColor != nullptr)
			*outColor = style.color;
		if (sameColor != nullptr)
			*sameColor = true;
	} else {
		bool oneColor = true;
		const TextStyle theStyle = fRuns[static_cast<std::size_t>(toIndex)].style;

		for (int32 i = fromIndex; i < toIndex; i++)
			FixupMode(fRuns[static_cast<std::size_t>(i)].style, theStyle, mode, oneColor);

		if (ioMode != nullptr)
			*ioMode = mode;
		if (outFont != nullptr)
			*outFont = theStyle.font;
		if (outColor != nullptr)
			*outColor = theStyle.color;
		if (sameColor != nullptr)
			*sameColor = oneColor;
	}
}


int32
TextStyleBuffer::OffsetToRun(int32 offset) const
{
	// Haiku's _BStyleRunDescBuffer_::OffsetToRun(): a binary search
	const auto itemCount = static_cast<int32>(fRuns.size());
	if (itemCount <= 1)
		return 0;

	int32 minIndex = 0;
	int32 maxIndex = itemCount;
	int32 index = 0;

	while (minIndex < maxIndex) {
		index = (minIndex + maxIndex) >> 1;
		if (offset >= fRuns[static_cast<std::size_t>(index)].offset) {
			if (index >= itemCount - 1
				|| offset < fRuns[static_cast<std::size_t>(index + 1)].offset) {
				break;
			} else
				minIndex = index + 1;
		} else
			maxIndex = index;
	}

	return index;
}


void
TextStyleBuffer::BumpOffset(int32 delta, int32 index)
{
	for (int32 i = std::max(index, 0); i < NumRuns(); i++)
		fRuns[static_cast<std::size_t>(i)].offset += delta;
}


int32
TextStyleBuffer::NumRuns() const
{
	return static_cast<int32>(fRuns.size());
}


const TextStyleRunEntry&
TextStyleBuffer::RunAt(int32 index) const
{
	return fRuns[static_cast<std::size_t>(index)];
}


void
TextStyleBuffer::_RemoveRuns(int32 index, int32 count)
{
	fRuns.erase(fRuns.begin() + index, fRuns.begin() + index + count);
}


} // namespace ngui
