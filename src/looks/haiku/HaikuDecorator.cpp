/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Authors (of Haiku's DefaultDecorator):
 *		Stephan Aßmus, superstippi@gmx.de
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Ryan Leavengood, leavengood@gmail.com
 *		Philippe Saint-Pierre, stpere@gmail.com
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Joseph Groover, looncraz@looncraz.net
 *		Tri-Edge AI
 *		Jacob Secunda, secundja@gmail.com
 */


#include "looks/haiku/HaikuDecorator.h"

#include <cmath>
#include <cstdint>

#include "decor/DrawingEngine.h"


namespace ngui::detail {


using compat::BGradientLinear;
using compat::BPoint;
using compat::BRect;
using compat::CompatView;
using compat::rgb_color;
using compat::tint_color;
using decor::FillRect;
using decor::StrokeLine;
using decor::StrokeRect;


namespace {


// COLORS_INDEX(): the color of the line \a i of a border \a borderWidth
// wide, out of \a nominalLimit.
int
ColorsIndex(int i, std::int32_t borderWidth, int nominalLimit)
{
	return static_cast<int>((static_cast<float>(i) / static_cast<float>(borderWidth))
		* static_cast<float>(nominalLimit));
}


} // namespace


HaikuDecorator::HaikuDecorator(const TextSystem& text)
	:
	Decorator(text)
{
}


void
HaikuDecorator::_GetComponentColors(Component component, ComponentColors _colors,
	const Tab* tab) const
{
	// DefaultDecorator::GetComponentColors() without the highlight of
	// resizing
	switch (component) {
		case COMPONENT_TAB:
			if (tab != nullptr && tab->buttonFocus) {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fFocusFrameColor, compat::B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fFocusFrameColor, compat::B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fNonFocusFrameColor, compat::B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fNonFocusFrameColor, compat::B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fNonFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fNonFocusTextColor;
			}
			break;

		case COMPONENT_CLOSE_BUTTON:
		case COMPONENT_ZOOM_BUTTON:
			if (tab != nullptr && tab->buttonFocus) {
				_colors[COLOR_BUTTON] = fFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fFocusTabColorLight;
			} else {
				_colors[COLOR_BUTTON] = fNonFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fNonFocusTabColorLight;
			}
			break;

		case COMPONENT_LEFT_BORDER:
		case COMPONENT_RIGHT_BORDER:
		case COMPONENT_TOP_BORDER:
		case COMPONENT_BOTTOM_BORDER:
		case COMPONENT_RESIZE_CORNER:
		default:
		{
			const rgb_color base = tab != nullptr && tab->buttonFocus
				? fFocusFrameColor : fNonFocusFrameColor;
			_colors[0] = tint_color(base, compat::B_DARKEN_2_TINT);
			_colors[1] = tint_color(base, compat::B_LIGHTEN_2_TINT);
			_colors[2] = base;
			_colors[3] = tint_color(base, (compat::B_DARKEN_1_TINT + compat::B_NO_TINT) / 2);
			_colors[4] = tint_color(base, compat::B_DARKEN_2_TINT);
			_colors[5] = tint_color(base, compat::B_DARKEN_3_TINT);
			break;
		}
	}
}


void
HaikuDecorator::_DrawFrame(CompatView& view, BRect rect)
{
	if (fTopTab->look == Look::NoBorder)
		return;

	if (fBorderWidth <= 0)
		return;

	// TODO: While this works, it does not look so crisp at higher resolutions.

	// Draw the border frame
	BRect border = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());
	switch (fTopTab->look) {
		case Look::Titled:
		{
			// top
			if (rect.Intersects(fTopBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_TOP_BORDER, colors, fTopTab);

				for (int i = 0; i < fBorderWidth; i++) {
					const int colorsIndex = ColorsIndex(i, fBorderWidth, 5);
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(border.left + step, border.top + step),
						BPoint(border.right - step, border.top + step), colors[colorsIndex]);
				}
				if (fTitleBarRect.IsValid()) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					const int overdraw
						= static_cast<int>(std::ceil(static_cast<float>(fBorderWidth) / 5.0f));
					for (int i = 1; i <= overdraw; i++) {
						const auto step = static_cast<float>(i);
						StrokeLine(view,
							BPoint(fTitleBarRect.left + 2, fTitleBarRect.bottom + step),
							BPoint(fTitleBarRect.right - 2, fTitleBarRect.bottom + step),
							colors[2]);
					}
				}
			}
			// left
			if (rect.Intersects(fLeftBorder.InsetByCopy(0, -static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

				for (int i = 0; i < fBorderWidth; i++) {
					const int colorsIndex = ColorsIndex(i, fBorderWidth, 5);
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(border.left + step, border.top + step),
						BPoint(border.left + step, border.bottom - step), colors[colorsIndex]);
				}
			}
			// bottom
			if (rect.Intersects(fBottomBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_BOTTOM_BORDER, colors, fTopTab);

				for (int i = 0; i < fBorderWidth; i++) {
					const int colorsIndex = ColorsIndex(i, fBorderWidth, 5);
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(border.left + step, border.bottom - step),
						BPoint(border.right - step, border.bottom - step),
						colors[(4 - colorsIndex) == 4 ? 5 : (4 - colorsIndex)]);
				}
			}
			// right
			if (rect.Intersects(fRightBorder.InsetByCopy(0, -static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_RIGHT_BORDER, colors, fTopTab);

				for (int i = 0; i < fBorderWidth; i++) {
					const int colorsIndex = ColorsIndex(i, fBorderWidth, 5);
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(border.right - step, border.top + step),
						BPoint(border.right - step, border.bottom - step),
						colors[(4 - colorsIndex) == 4 ? 5 : (4 - colorsIndex)]);
				}
			}
			break;
		}

		case Look::Bordered:
		{
			// TODO: Draw the borders individually!
			ComponentColors colors;
			_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

			StrokeRect(view, border, colors[5]);
			break;
		}

		case Look::NoBorder:
			// don't draw a border frame
			break;
	}

	// Draw the resize knob if we're supposed to
	if (fResizable && fTopTab->look == Look::Titled) {
		ComponentColors colors;
		_GetComponentColors(COMPONENT_RESIZE_CORNER, colors, fTopTab);

		if (rect.Intersects(BRect(fRightBorder.right - fBorderResizeLength,
				fBottomBorder.bottom - fBorderResizeLength, fRightBorder.right - 1,
				fBottomBorder.bottom - 1))) {
			StrokeLine(view,
				BPoint(fRightBorder.left, fBottomBorder.bottom - fBorderResizeLength),
				BPoint(fRightBorder.right - 1, fBottomBorder.bottom - fBorderResizeLength),
				colors[0]);
			StrokeLine(view,
				BPoint(fRightBorder.right - fBorderResizeLength, fBottomBorder.top),
				BPoint(fRightBorder.right - fBorderResizeLength, fBottomBorder.bottom - 1),
				colors[0]);
		}
	}
}


void
HaikuDecorator::_DrawTab(CompatView& view, Tab* tab, BRect invalid)
{
	const BRect& tabRect = tab->tabRect;
	// If a window has a tab, this will draw it and any buttons which are
	// in it.
	if (!tabRect.IsValid() || !invalid.Intersects(tabRect))
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	// outer frame
	StrokeLine(view, tabRect.LeftTop(), tabRect.LeftBottom(), colors[COLOR_TAB_FRAME_LIGHT]);
	StrokeLine(view, tabRect.LeftTop(), tabRect.RightTop(), colors[COLOR_TAB_FRAME_LIGHT]);
	StrokeLine(view, tabRect.RightTop(), tabRect.RightBottom(), colors[COLOR_TAB_FRAME_DARK]);

	const float tabBottom = tabRect.bottom;

	// bevel
	StrokeLine(view, BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.left + 1, tabBottom), colors[COLOR_TAB_BEVEL]);
	StrokeLine(view, BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.right - 1, tabRect.top + 1), colors[COLOR_TAB_BEVEL]);

	StrokeLine(view, BPoint(tabRect.right - 1, tabRect.top + 2),
		BPoint(tabRect.right - 1, tabBottom), colors[COLOR_TAB_SHADOW]);

	// fill
	BGradientLinear gradient;
	gradient.SetStart(tabRect.LeftTop());
	gradient.AddColor(colors[COLOR_TAB_LIGHT], 0);
	gradient.AddColor(colors[COLOR_TAB], 255);

	gradient.SetEnd(tabRect.LeftBottom());
	FillRect(view, BRect(tabRect.left + 2, tabRect.top + 2, tabRect.right - 2, tabBottom),
		gradient);

	_DrawTitle(view, tab, tabRect);

	_DrawButtons(view, tab, invalid);
}


void
HaikuDecorator::_DrawTitle(CompatView& view, Tab* tab, BRect /*rect*/)
{
	const BRect& tabRect = tab->tabRect;
	const BRect& closeRect = tab->closeRect;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	view.SetDrawingMode(compat::B_OP_OVER);
	view.SetHighColor(colors[COLOR_TAB_TEXT]);
	view.SetFont(&fFont);

	// figure out position of text
	compat::font_height fontHeight;
	fFont.GetHeight(&fontHeight);

	BPoint titlePos;
	titlePos.x = closeRect.IsValid() ? closeRect.right + tab->textOffset
		: tabRect.left + tab->textOffset;
	titlePos.y = std::floor(((tabRect.top + 2.0f) + tabRect.bottom + fontHeight.ascent
		+ fontHeight.descent) / 2.0f - fontHeight.descent + 0.5f);

	view.DrawString(tab->truncatedTitle.c_str(), titlePos);

	view.SetDrawingMode(compat::B_OP_COPY);
}


void
HaikuDecorator::_DrawClose(CompatView& view, Tab* tab, BRect frame)
{
	// _GetBitmapForButton() of COMPONENT_CLOSE_BUTTON, drawn in place: a
	// bitmap of rect.IntegerWidth() × rect.IntegerHeight() pixels
	const auto width = static_cast<float>(frame.IntegerWidth());
	const auto height = static_cast<float>(frame.IntegerHeight());
	if (width < 1 || height < 1)
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_CLOSE_BUTTON, colors, tab);

	const BRect rect(frame.left, frame.top, frame.left + width - 1, frame.top + height - 1);
	_DrawBlendedRect(view, rect, tab->closePressed, colors);
}


void
HaikuDecorator::_DrawZoom(CompatView& view, Tab* tab, BRect frame)
{
	if (frame.IntegerWidth() < 1)
		return;

	// _GetBitmapForButton() of COMPONENT_ZOOM_BUTTON, drawn in place on a
	// transparent background
	const auto width = static_cast<float>(frame.IntegerWidth());
	const auto height = static_cast<float>(frame.IntegerHeight());
	if (height < 1)
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_ZOOM_BUTTON, colors, tab);

	const BRect rect(frame.left, frame.top, frame.left + width - 1, frame.top + height - 1);

	float inset = std::floor(width / 4.0f);
	BRect zoomRect(rect);
	zoomRect.left += inset;
	zoomRect.top += inset;
	_DrawBlendedRect(view, zoomRect, tab->zoomPressed, colors);

	inset = std::floor(width / 2.1f);
	zoomRect = rect;
	zoomRect.right -= inset;
	zoomRect.bottom -= inset;
	_DrawBlendedRect(view, zoomRect, tab->zoomPressed, colors);
}


void
HaikuDecorator::_DrawBlendedRect(CompatView& view, const BRect rect, bool down,
	const ComponentColors& colors)
{
	// figure out which colors to use
	rgb_color startColor;
	rgb_color endColor;
	if (down) {
		startColor = tint_color(colors[COLOR_BUTTON], compat::B_DARKEN_1_TINT);
		endColor = colors[COLOR_BUTTON_LIGHT];
	} else {
		startColor = tint_color(colors[COLOR_BUTTON], compat::B_LIGHTEN_MAX_TINT);
		endColor = colors[COLOR_BUTTON];
	}

	// fill
	const BRect fillRect(rect.InsetByCopy(1.0f, 1.0f));

	BGradientLinear gradient;
	gradient.SetStart(fillRect.LeftTop());
	gradient.SetEnd(fillRect.RightBottom());
	gradient.AddColor(startColor, 0);
	gradient.AddColor(endColor, 255);

	FillRect(view, fillRect, gradient);

	// outline
	StrokeRect(view, rect, tint_color(colors[COLOR_BUTTON], compat::B_DARKEN_2_TINT));
}


} // namespace ngui::detail
