/*
 * Copyright 2001-2009 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/StringItem.cpp (commit b1d497bb69),
 * without archiving. NewGUI: the text is shaped by the DrawContext's text
 * system in the plain role and drawn in the color the list gives.
 *
 * Authors (Haiku):
 *		Ulrich Wimboeck
 *		Marc Flerackers (mflerackers@androme.be)
 *		Rene Gollent
 */


#include <ngui/StringItem.h>

#include <cmath>

#include <ngui/ControlLook.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/View.h>
#include <ngui/gfx/Canvas.h>


namespace ngui {


BStringItem::BStringItem(std::string_view text, uint32 level, bool expanded)
	:
	BListItem(level, expanded)
{
	SetText(text);
}


BStringItem::~BStringItem()
{
}


void
BStringItem::DrawItem(BView* owner, DrawContext& context, gfx::Rect frame,
	const gfx::Color& textColor, bool complete)
{
	if (fText.empty())
		return;

	if (IsSelected() || complete) {
		gfx::Color color;
		if (IsSelected())
			color = context.palette.Get(BColorWhich::ListSelectedBackground);
		else {
			color = owner->ViewColor().value_or(
				context.palette.Get(BColorWhich::ListBackground));
		}

		context.canvas.FillRect(frame, gfx::Paint{.color = color});
	}

	const ShapedText shaped = context.text.Shape(fText, FontRole::Plain);
	context.canvas.DrawGlyphRun(shaped.Run(),
		{frame.x0 + owner->Look().DefaultLabelSpacing(context.text),
			frame.y0 + fBaselineOffset},
		gfx::Paint{.color = textColor});
}


void
BStringItem::SetText(std::string_view text)
{
	fText = text;
}


const std::string&
BStringItem::Text() const
{
	return fText;
}


void
BStringItem::Update(BView* owner, const TextSystem& text)
{
	if (!fText.empty()) {
		SetWidth(text.StringWidth(fText, FontRole::Plain)
			+ owner->Look().DefaultLabelSpacing(text));
	}

	const FontHeight fheight = text.Height(FontRole::Plain);

	fBaselineOffset = 2 + std::ceil(fheight.ascent + fheight.leading / 2);

	SetHeight(std::ceil(fheight.ascent) + std::ceil(fheight.descent)
		+ std::ceil(fheight.leading) + 4);
}


float
BStringItem::BaselineOffset() const
{
	return fBaselineOffset;
}


} // namespace ngui
