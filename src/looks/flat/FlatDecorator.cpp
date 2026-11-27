/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Authors (of Haiku's FlatDecorator):
 *		Stephan Aßmus, superstippi@gmx.de
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Ryan Leavengood, leavengood@gmail.com
 *		Philippe Saint-Pierre, stpere@gmail.com
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Joseph Groover <looncraz@looncraz.net>
 *		Nahuel Tello <nhtello@unarix.com.ar>
 */


#include "looks/flat/FlatDecorator.h"

#include <cmath>

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


constexpr float kBorderResizeLength = 22.0f;


} // namespace


FlatDecorator::FlatDecorator(const TextSystem& text)
	:
	Decorator(text)
{
}


void
FlatDecorator::_GetComponentColors(Component component, ComponentColors _colors,
	const Tab* tab) const
{
	// FlatDecorator::GetComponentColors() without the highlights of stack and
	// tile and of resizing
	const bool focus = tab != nullptr && tab->buttonFocus;
	switch (component) {
		case COMPONENT_TAB:
			if (focus) {
				_colors[COLOR_TAB_FRAME_LIGHT] = tint_color(fFocusTabColor, 1.0f);
				_colors[COLOR_TAB_FRAME_DARK] = tint_color(fFocusTabColor, 1.2f);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT] = tint_color(fNonFocusTabColor, 1.0f);
				_colors[COLOR_TAB_FRAME_DARK] = tint_color(fNonFocusTabColor, 1.2f);
				_colors[COLOR_TAB] = fNonFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fNonFocusTextColor;
			}
			break;

		case COMPONENT_CLOSE_BUTTON:
		case COMPONENT_ZOOM_BUTTON:
			if (focus) {
				_colors[COLOR_BUTTON] = fFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fFocusTabColorLight;
			} else {
				_colors[COLOR_BUTTON] = fNonFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fNonFocusTabColorLight;
			}
			break;

		case COMPONENT_TOP_BORDER:
		{
			const rgb_color tabColor = focus ? fFocusTabColor : fNonFocusTabColor;
			const rgb_color frameColor = focus ? fFocusFrameColor : fNonFocusFrameColor;
			_colors[0] = tint_color(tabColor, 1.2f); // borde exterior
			_colors[1] = tint_color(tabColor, 1.0f); // borde top
			_colors[2] = tint_color(tabColor, 1.0f); // borde top
			_colors[3] = tint_color(tabColor, 1.0f); // borde top
			_colors[4] = tint_color(frameColor, 1.1f); // borde interior
			_colors[5] = tint_color(frameColor, 1.1f); // borde menu 1
			break;
		}
		case COMPONENT_RESIZE_CORNER:
			if (focus) {
				_colors[0] = tint_color(fFocusFrameColor, 1.25f); // borde exterior
				_colors[1] = tint_color(fFocusFrameColor, 1.0f); // borde top
				_colors[2] = tint_color(fFocusFrameColor, 1.0f); // borde top
				_colors[3] = tint_color(fFocusTabColor, 1.0f); // borde top
				_colors[4] = tint_color(fFocusFrameColor, 1.1f); // borde interior
				_colors[5] = tint_color(fFocusFrameColor, 1.1f); // borde menu 1
			} else {
				_colors[0] = tint_color(fNonFocusFrameColor, 1.25f); // borde exterior
				_colors[1] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[2] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[3] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[4] = tint_color(fNonFocusFrameColor, 1.1f); // borde interior
				_colors[5] = tint_color(fNonFocusFrameColor, 1.1f); // borde menu 1
			}
			break;
		case COMPONENT_LEFT_BORDER:
		case COMPONENT_RIGHT_BORDER:
			if (focus) {
				_colors[0] = tint_color(fFocusFrameColor, 1.25f); // borde exterior
				_colors[1] = tint_color(fFocusFrameColor, compat::B_NO_TINT);
				_colors[2] = tint_color(fFocusFrameColor, compat::B_NO_TINT);
				_colors[3] = tint_color(fFocusFrameColor, compat::B_NO_TINT);
				_colors[4] = tint_color(fFocusFrameColor, 1.05f); // borde interior
				_colors[5] = tint_color(fFocusFrameColor, 1.1f); // borde menu 1
				_colors[6] = tint_color(fFocusTabColor, 1.2f); // border tab to be part
			} else {
				_colors[0] = tint_color(fNonFocusFrameColor, 1.25f); // borde exterior
				_colors[1] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[2] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[3] = tint_color(fNonFocusFrameColor, compat::B_NO_TINT);
				_colors[4] = tint_color(fNonFocusFrameColor, 1.05f); // borde interior
				_colors[5] = tint_color(fNonFocusFrameColor, 1.0f); // borde menu 1
				_colors[6] = tint_color(fNonFocusTabColor, 1.2f); // border tab to be part
			}
			break;
		case COMPONENT_BOTTOM_BORDER:
		default:
		{
			const rgb_color base = focus ? fFocusFrameColor : fNonFocusFrameColor;
			_colors[0] = tint_color(base, 1.25f); // borde exterior
			_colors[1] = tint_color(base, compat::B_NO_TINT);
			_colors[2] = tint_color(base, compat::B_NO_TINT);
			_colors[3] = tint_color(base, compat::B_NO_TINT);
			_colors[4] = tint_color(base, 1.1f); // borde interior
			_colors[5] = tint_color(base, 1.1f); // borde menu 1
			break;
		}
	}
}


void
FlatDecorator::_DrawFrame(CompatView& view, BRect rect)
{
	if (fTopTab->look == Look::NoBorder)
		return;

	if (fBorderWidth <= 0)
		return;

	// Draw the border frame
	BRect r = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());
	switch (fTopTab->look) {
		case Look::Titled:
		{
			// left
			if (rect.Intersects(fLeftBorder.InsetByCopy(0, -static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.left + step, r.top + step),
						BPoint(r.left + step, r.bottom - step), colors[i]);
				}
				// redraw line to be part of tab title
				StrokeLine(view, BPoint(r.left, r.top), BPoint(r.left, r.top + 4), colors[6]);
			}
			// bottom
			if (rect.Intersects(fBottomBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_BOTTOM_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.left + step, r.bottom - step),
						BPoint(r.right - step, r.bottom - step), colors[i]);
				}
			}
			// right
			if (rect.Intersects(fRightBorder.InsetByCopy(0, -static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_RIGHT_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.right - step, r.top + step),
						BPoint(r.right - step, r.bottom - step), colors[i]);
				}
				// redraw line to be part of tab title
				StrokeLine(view, BPoint(r.right, r.top), BPoint(r.right, r.top + 4), colors[6]);
			}
			// top
			if (rect.Intersects(fTopBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_TOP_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					if (i < 4) {
						StrokeLine(view, BPoint(r.left + 1, r.top + step),
							BPoint(r.right - 1, r.top + step),
							tint_color(colors[i], step * 0.01f + 1));
					} else {
						StrokeLine(view, BPoint(r.left + 1, r.top + step),
							BPoint(r.right - 1, r.top + step), tint_color(colors[3], 1.1f));
					}
				}
			}
			break;
		}

		case Look::Bordered:
		{
			// TODO: Draw the borders individually!
			ComponentColors colors;
			_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

			StrokeRect(view, r, colors[5]);
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

		if (rect.Intersects(BRect(fRightBorder.right - kBorderResizeLength,
				fBottomBorder.bottom - kBorderResizeLength, fRightBorder.right - 1,
				fBottomBorder.bottom - 1))) {
			StrokeLine(view, BPoint(fRightBorder.left, fBottomBorder.bottom - kBorderResizeLength),
				BPoint(fRightBorder.right - 1, fBottomBorder.bottom - kBorderResizeLength),
				tint_color(colors[1], 1.2f));
			StrokeLine(view, BPoint(fRightBorder.right - kBorderResizeLength, fBottomBorder.top),
				BPoint(fRightBorder.right - kBorderResizeLength, fBottomBorder.bottom - 1),
				tint_color(colors[1], 1.2f));

			// Try to draw line in yellow to the resize place
			for (int i = 1; i < 4; i++) {
				const auto step = static_cast<float>(i);
				StrokeLine(view,
					BPoint(fRightBorder.left + step,
						fBottomBorder.bottom - kBorderResizeLength + 1),
					BPoint(fRightBorder.left + step, fBottomBorder.bottom - 1),
					tint_color(colors[3], (step * 0.06f) + 1));
			}
			const int rez[] = {4, 3, 2, 1};
			for (int i = 1; i < 4; i++) {
				const auto step = static_cast<float>(i);
				StrokeLine(view,
					BPoint(fRightBorder.right - kBorderResizeLength + 1,
						fBottomBorder.bottom - step),
					BPoint(fRightBorder.right - step, fBottomBorder.bottom - step),
					tint_color(colors[3], (static_cast<float>(rez[i]) * 0.06f) + 1));
			}
		}
	}
}


void
FlatDecorator::_DrawTab(CompatView& view, Tab* tab, BRect invalid)
{
	const BRect& tabRect = tab->tabRect;
	// If a window has a tab, this will draw it and any buttons which are
	// in it.
	if (!tabRect.IsValid() || !invalid.Intersects(tabRect))
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	// outer frame (the same with and without the focus)
	StrokeLine(view, tabRect.LeftTop(), tabRect.LeftBottom(), colors[COLOR_TAB_FRAME_DARK]);
	StrokeLine(view, tabRect.LeftTop(), tabRect.RightTop(), colors[COLOR_TAB_FRAME_DARK]);
	StrokeLine(view, tabRect.RightTop(), tabRect.RightBottom(), colors[COLOR_TAB_FRAME_DARK]);

	const float tabBottom = tabRect.bottom;

	// bevel
	StrokeLine(view, BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.left + 1, tabBottom), colors[COLOR_TAB]);
	StrokeLine(view, BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.right - 1, tabRect.top + 1), tint_color(colors[COLOR_TAB], 0.9f));

	StrokeLine(view, BPoint(tabRect.right - 1, tabRect.top + 2),
		BPoint(tabRect.right - 1, tabBottom), colors[COLOR_TAB]);

	// fill
	BGradientLinear gradient;
	gradient.SetStart(tabRect.LeftTop());
	if (tab->buttonFocus) {
		gradient.AddColor(tint_color(colors[COLOR_TAB], 0.6f), 0);
		gradient.AddColor(tint_color(colors[COLOR_TAB], 1.0f), 200);
	} else {
		gradient.AddColor(tint_color(colors[COLOR_TAB], 0.9f), 0);
		gradient.AddColor(tint_color(colors[COLOR_TAB], 1.0f), 150);
	}

	gradient.SetEnd(tabRect.LeftBottom());
	FillRect(view, BRect(tabRect.left + 2, tabRect.top + 2, tabRect.right - 2, tabBottom),
		gradient);

	_DrawTitle(view, tab, tabRect);

	_DrawButtons(view, tab, invalid);
}


void
FlatDecorator::_DrawTitle(CompatView& view, Tab* tab, BRect /*rect*/)
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
FlatDecorator::_DrawClose(CompatView& view, Tab* tab, BRect frame)
{
	// _GetBitmapForButton() of COMPONENT_CLOSE_BUTTON, drawn in place
	const auto width = static_cast<float>(frame.IntegerWidth());
	const auto height = static_cast<float>(frame.IntegerHeight());
	if (width < 1 || height < 1)
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_CLOSE_BUTTON, colors, tab);

	const BRect rect(frame.left, frame.top, frame.left + width - 1, frame.top + height - 1);
	_DrawBlendedRect(view, rect, tab->buttonFocus ? tab->closePressed : true, colors);
}


void
FlatDecorator::_DrawZoom(CompatView& view, Tab* tab, BRect frame)
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
	const bool down = tab->buttonFocus ? tab->zoomPressed : true;

	float inset = std::floor(width / 4.0f);
	BRect zoomRect(rect);
	zoomRect.left += inset;
	zoomRect.top += inset;
	_DrawBlendedRect(view, zoomRect, down, colors);

	inset = std::floor(width / 2.1f);
	zoomRect = rect;
	zoomRect.right -= inset;
	zoomRect.bottom -= inset;
	_DrawBlendedRect(view, zoomRect, down, colors);
}


void
FlatDecorator::_DrawBlendedRect(CompatView& view, const BRect rect, bool down,
	const ComponentColors& colors)
{
	// (the bitmap's background is transparent: nothing to fill)

	// figure out which colors to use
	rgb_color startColor;
	rgb_color endColor;
	if (down) {
		startColor = tint_color(colors[COLOR_BUTTON], 1.0f);
		endColor = tint_color(colors[COLOR_BUTTON], 0.9f);
	} else {
		startColor = tint_color(colors[COLOR_BUTTON], 0.95f);
		endColor = tint_color(colors[COLOR_BUTTON], 0.5f);
	}

	// fill
	const BRect fillRect(rect.InsetByCopy(1.0f, 1.0f));

	BGradientLinear gradient;
	gradient.SetStart(fillRect.LeftBottom());
	gradient.SetEnd(fillRect.LeftTop());
	gradient.AddColor(startColor, 0);
	gradient.AddColor(endColor, 250);

	FillRect(view, fillRect, gradient);

	// outline
	StrokeRect(view, rect, tint_color(colors[COLOR_BUTTON], 1.25f));
}


} // namespace ngui::detail
