/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Authors (of Haiku's BeDecorator):
 *		Stephan Aßmus, superstippi@gmx.de
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		John Scipione, jscipione@gmail.com
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 */


#include "looks/be/BeDecorator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "decor/DrawingEngine.h"


namespace ngui::detail {


using compat::BPoint;
using compat::BRect;
using compat::CompatView;
using compat::rgb_color;
using decor::FillRect;
using decor::StrokeLine;
using decor::StrokePoint;
using decor::StrokeRect;


namespace {


constexpr float kBorderResizeLength = 22.0f;


constexpr unsigned char f = 0xff; // way to write 0xff shorter

constexpr unsigned char kInnerShadowBits[] = {
	f, f, f, f, f, f, f, f, f, 0,
	f, f, f, f, f, f, 0, f, 0, f,
	f, f, f, f, f, 0, f, 0, f, 0,
	f, f, f, f, 0, f, 0, 0, 0, 0,
	f, f, f, 0, f, 0, 0, 0, 0, 0,
	f, f, 0, f, 0, 0, 0, 0, 0, 0,
	f, 0, f, 0, 0, 0, 0, 0, 0, 0,
	f, f, 0, 0, 0, 0, 0, 0, 0, 0,
	f, 0, f, 0, 0, 0, 0, 0, 0, 0,
	0, f, 0, 0, 0, 0, 0, 0, 0, 0
};

constexpr unsigned char kOuterShadowBits[] = {
	f, f, f, f, f, f, f, f, f, f,
	f, f, f, f, f, f, f, f, f, f,
	f, f, f, f, f, f, f, f, f, f,
	f, f, f, f, f, f, f, f, f, f,
	f, f, f, f, f, f, f, f, f, 0,
	f, f, f, f, f, f, f, 0, 0, 0,
	f, f, f, f, f, f, 0, f, 0, 0,
	f, f, f, f, f, 0, f, 0, 0, 0,
	f, f, f, f, f, 0, 0, 0, 0, 0,
	f, f, f, f, 0, 0, 0, 0, 0, 0
};

constexpr unsigned char kBigInnerShadowBits[] = {
	f, f, f, f, f, f, f,
	f, f, f, f, f, f, 0,
	f, f, f, f, f, 0, 0,
	f, f, f, f, 0, f, 0,
	f, f, f, 0, f, 0, 0,
	f, f, 0, f, 0, 0, 0,
	f, 0, 0, 0, 0, 0, 0
};

constexpr unsigned char kBigOuterShadowBits[] = {
	f, f, f, f, f, f, f,
	f, f, f, f, f, f, 0,
	f, f, f, f, f, f, 0,
	f, f, f, f, f, f, 0,
	f, f, f, f, f, f, 0,
	f, f, f, f, f, f, 0,
	f, 0, 0, 0, 0, 0, 0
};

constexpr unsigned char kSmallInnerShadowBits[] = {
	f, f, f, 0, 0,
	f, f, 0, f, 0,
	f, 0, f, 0, 0,
	0, f, 0, 0, 0,
	0, 0, 0, 0, 0
};

constexpr unsigned char kSmallOuterShadowBits[] = {
	f, f, f, f, f,
	f, f, f, f, f,
	f, f, f, f, f,
	f, f, f, f, 0,
	f, f, 0, 0, 0
};

constexpr unsigned char kGlintBits[] = {
	0, f, 0,
	f, 0, f,
	0, f, f
};


// What _GetBitmapForButton() puts into a bitmap of \a side × \a side pixels
// and draws at \a left, \a top: the pixels of \a outer that are 0 in
// \a outerColor, then those of \a inner (if any) that are 0 in
// \a innerColor; the others are the background, which lies there already.
// \a rotated reads the bits backwards (turned by 180°).
void
DrawBits(CompatView& view, const unsigned char* outer, const unsigned char* inner,
	std::size_t side, bool rotated, float left, float top, rgb_color outerColor,
	rgb_color innerColor)
{
	const std::size_t size = side * side;
	for (std::size_t i = 0; i < size; i++) {
		const std::size_t at = rotated ? size - 1 - i : i;
		const BPoint point(left + static_cast<float>(at % side),
			top + static_cast<float>(at / side));
		if (outer[i] == 0)
			StrokePoint(view, point, outerColor);
		else if (inner != nullptr && inner[i] == 0)
			StrokePoint(view, point, innerColor);
	}
}


} // namespace


BeDecorator::BeDecorator(const TextSystem& text)
	:
	Decorator(text)
{
}


void
BeDecorator::_GetComponentColors(Component component, ComponentColors _colors,
	const Tab* tab) const
{
	// BeDecorator::GetComponentColors() without the highlights of stack and
	// tile and of resizing
	switch (component) {
		case COMPONENT_TAB:
			if (tab != nullptr && tab->buttonFocus) {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= compat::tint_color(fFocusFrameColor, compat::B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= compat::tint_color(fFocusFrameColor, compat::B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= compat::tint_color(fNonFocusFrameColor, compat::B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= compat::tint_color(fNonFocusFrameColor, compat::B_DARKEN_3_TINT);
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
			const auto shade = [&base](int delta) {
				const auto channel = [delta](std::uint8_t value) {
					return static_cast<std::uint8_t>(std::clamp(value + delta, 0, 255));
				};
				return rgb_color{channel(base.red), channel(base.green), channel(base.blue),
					255};
			};
			_colors[0] = shade(-72);
			_colors[1] = shade(64);
			_colors[2] = shade(-8);
			_colors[3] = shade(-88);
			_colors[4] = shade(-72);
			_colors[5] = shade(-128);
			break;
		}
	}
}


void
BeDecorator::_DrawFrame(CompatView& view, BRect invalid)
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
			// top
			if (invalid.Intersects(fTopBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_TOP_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.left + step, r.top + step),
						BPoint(r.right - step, r.top + step), colors[i]);
				}
				if (fTitleBarRect.IsValid()) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					StrokeLine(view, BPoint(fTitleBarRect.left + 2, fTitleBarRect.bottom + 1),
						BPoint(fTitleBarRect.right - 2, fTitleBarRect.bottom + 1), colors[2]);
				}
			}
			// left
			if (invalid.Intersects(fLeftBorder.InsetByCopy(0,
					-static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.left + step, r.top + step),
						BPoint(r.left + step, r.bottom - step), colors[i]);
				}
			}
			// bottom
			if (invalid.Intersects(fBottomBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_BOTTOM_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.left + step, r.bottom - step),
						BPoint(r.right - step, r.bottom - step),
						colors[(4 - i) == 4 ? 5 : (4 - i)]);
				}
			}
			// right
			if (invalid.Intersects(fRightBorder.InsetByCopy(0,
					-static_cast<float>(fBorderWidth)))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_RIGHT_BORDER, colors, fTopTab);

				for (int i = 0; i < 5; i++) {
					const auto step = static_cast<float>(i);
					StrokeLine(view, BPoint(r.right - step, r.top + step),
						BPoint(r.right - step, r.bottom - step),
						colors[(4 - i) == 4 ? 5 : (4 - i)]);
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

		if (invalid.Intersects(BRect(fRightBorder.right - kBorderResizeLength,
				fBottomBorder.bottom - kBorderResizeLength, fRightBorder.right - 1,
				fBottomBorder.bottom - 1))) {
			StrokeLine(view, BPoint(fRightBorder.left, fBottomBorder.bottom - kBorderResizeLength),
				BPoint(fRightBorder.right - 1, fBottomBorder.bottom - kBorderResizeLength),
				colors[0]);
			StrokeLine(view, BPoint(fRightBorder.right - kBorderResizeLength, fBottomBorder.top),
				BPoint(fRightBorder.right - kBorderResizeLength, fBottomBorder.bottom - 1),
				colors[0]);
		}
	}
}


void
BeDecorator::_DrawTab(CompatView& view, Tab* tab, BRect invalid)
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
	FillRect(view, BRect(tabRect.left + 2, tabRect.top + 2, tabRect.right - 2, tabRect.bottom),
		colors[COLOR_TAB]);

	_DrawTitle(view, tab, tabRect);

	_DrawButtons(view, tab, invalid);
}


void
BeDecorator::_DrawTitle(CompatView& view, Tab* tab, BRect /*r*/)
{
	const BRect& tabRect = tab->tabRect;
	const BRect& closeRect = tab->closeRect;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	view.SetDrawingMode(compat::B_OP_OVER);
	view.SetHighColor(colors[COLOR_TAB_TEXT]);
	view.SetLowColor(colors[COLOR_TAB]);
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
BeDecorator::_DrawClose(CompatView& view, Tab* tab, BRect frame)
{
	// _GetBitmapForButton() of COMPONENT_CLOSE_BUTTON, drawn in place: a
	// bitmap of rect.IntegerWidth() × rect.IntegerHeight() pixels
	const auto width = static_cast<float>(frame.IntegerWidth());
	const auto height = static_cast<float>(frame.IntegerHeight());
	if (width < 1 || height < 1)
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_CLOSE_BUTTON, colors, tab);

	const rgb_color buttonColor(colors[COLOR_BUTTON]);
	const bool isGrayscale = buttonColor.red == buttonColor.green
		&& buttonColor.green == buttonColor.blue;
	const auto plus = [](std::uint8_t value, int delta) {
		return static_cast<std::uint8_t>(std::clamp(value + delta, 0, 255));
	};

	rgb_color buttonColorLight1(buttonColor);
	buttonColorLight1.red = plus(buttonColor.red, 35);
	buttonColorLight1.green = plus(buttonColor.green, 35);
	buttonColorLight1.blue = plus(buttonColor.blue, isGrayscale ? 35 : 0);
		// greyscale color stays grayscale

	rgb_color buttonColorLight2(buttonColor);
	buttonColorLight2.red = plus(buttonColor.red, 52);
	buttonColorLight2.green = plus(buttonColor.green, 52);
	buttonColorLight2.blue = plus(buttonColor.blue, 26);

	rgb_color buttonColorShadow1(buttonColor);
	buttonColorShadow1.red = plus(buttonColor.red, -21);
	buttonColorShadow1.green = plus(buttonColor.green, -21);
	buttonColorShadow1.blue = plus(buttonColor.blue, -21);

	// BeOS R5 shadow2: active: 183, 131, 0 inactive: 160, 160, 160
	rgb_color buttonColorShadow2(buttonColor);
	buttonColorShadow2.red = plus(buttonColor.red, -72);
	buttonColorShadow2.green = plus(buttonColor.green, -72);
	buttonColorShadow2.blue = plus(buttonColor.blue, -72);

	BRect rect(frame.left, frame.top, frame.left + width - 1, frame.top + height - 1);

	// fill the background
	FillRect(view, rect, buttonColor);

	// draw outer bevel
	_DrawBevelRect(view, rect, tab->closePressed, buttonColorLight2, buttonColorShadow2);

	// inset by bevel
	rect.InsetBy(2, 2);

	// fill bg
	FillRect(view, rect, buttonColorLight1);

	if (tab->closePressed) {
		// Draw glint in bottom right, then combined inner and outer
		// shadow in top left, the bitmaps turned by 180°.
		DrawBits(view, kGlintBits, nullptr, 3, true, rect.right - 2, rect.bottom - 2,
			buttonColorLight2, buttonColorLight2);
		DrawBits(view, kOuterShadowBits, kInnerShadowBits, 10, true, rect.left, rect.top,
			buttonColorShadow1, buttonColor);
	} else {
		// draw glint, then draw combined outer and inner shadows
		DrawBits(view, kGlintBits, nullptr, 3, false, rect.left, rect.top, buttonColorLight2,
			buttonColorLight2);
		DrawBits(view, kOuterShadowBits, kInnerShadowBits, 10, false, rect.right - 9,
			rect.bottom - 9, buttonColorShadow1, buttonColor);
	}
}


void
BeDecorator::_DrawZoom(CompatView& view, Tab* tab, BRect frame)
{
	if (frame.IntegerWidth() < 1)
		return;

	// _GetBitmapForButton() of COMPONENT_ZOOM_BUTTON, drawn in place
	const auto width = static_cast<float>(frame.IntegerWidth());
	const auto height = static_cast<float>(frame.IntegerHeight());
	if (height < 1)
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_ZOOM_BUTTON, colors, tab);

	const rgb_color buttonColor(colors[COLOR_BUTTON]);
	const bool isGrayscale = buttonColor.red == buttonColor.green
		&& buttonColor.green == buttonColor.blue;
	const auto plus = [](std::uint8_t value, int delta) {
		return static_cast<std::uint8_t>(std::clamp(value + delta, 0, 255));
	};

	rgb_color buttonColorLight1(buttonColor);
	buttonColorLight1.red = plus(buttonColor.red, 35);
	buttonColorLight1.green = plus(buttonColor.green, 35);
	buttonColorLight1.blue = plus(buttonColor.blue, isGrayscale ? 35 : 0);

	rgb_color buttonColorLight2(buttonColor);
	buttonColorLight2.red = plus(buttonColor.red, 52);
	buttonColorLight2.green = plus(buttonColor.green, 52);
	buttonColorLight2.blue = plus(buttonColor.blue, 26);

	rgb_color buttonColorShadow1(buttonColor);
	buttonColorShadow1.red = plus(buttonColor.red, -21);
	buttonColorShadow1.green = plus(buttonColor.green, -21);
	buttonColorShadow1.blue = plus(buttonColor.blue, -21);

	// BeOS R5 shadow2: active: 210, 158, 0 inactive: 187, 187, 187
	rgb_color buttonColorShadow2(buttonColor);
	buttonColorShadow2.red = plus(buttonColor.red, -45);
	buttonColorShadow2.green = plus(buttonColor.green, -45);
	buttonColorShadow2.blue = plus(buttonColor.blue, -45);

	const BRect rect(frame.left, frame.top, frame.left + width - 1, frame.top + height - 1);

	// fill the background
	FillRect(view, rect, buttonColor);

	// big rect
	BRect bigRect(rect);
	bigRect.left += std::floor(width * 3.0f / 14.0f);
	bigRect.top += std::floor(height * 3.0f / 14.0f);

	// small rect
	BRect smallRect(rect);
	smallRect.right -= std::floor(width * 5.0f / 14.0f);
	smallRect.bottom -= std::floor(height * 5.0f / 14.0f);

	// draw big rect bevel
	_DrawBevelRect(view, bigRect, tab->zoomPressed, buttonColorLight2, buttonColorShadow2);

	// inset past bevel
	bigRect.InsetBy(2, 2);

	// fill big rect bg
	FillRect(view, bigRect, buttonColorLight1);

	// some elements are covered by the small rect
	// so only draw the parts that get shown
	if (tab->zoomPressed) {
		// draw glint, turned by 180°
		DrawBits(view, kGlintBits, nullptr, 3, true, bigRect.right - 2, bigRect.bottom - 2,
			buttonColorLight2, buttonColorLight2);
	} else {
		// draw combined inner and outer shadow
		DrawBits(view, kBigOuterShadowBits, kBigInnerShadowBits, 7, false, bigRect.right - 6,
			bigRect.bottom - 6, buttonColorShadow1, buttonColor);
	}

	// draw small rect bevel
	_DrawBevelRect(view, smallRect, tab->zoomPressed, buttonColorLight2, buttonColorShadow2);

	if (!tab->zoomPressed) {
		// undraw bottom left and top right corners
		StrokePoint(view, smallRect.LeftBottom(), buttonColor);
		StrokePoint(view, smallRect.RightTop(), buttonColor);
	}

	// inset past bevel
	smallRect.InsetBy(2, 2);

	// fill small rect bg
	FillRect(view, smallRect, buttonColorLight1);

	// draw small bitmap
	if (tab->zoomPressed) {
		// draw combined inner and outer shadow, turned by 180°
		DrawBits(view, kSmallOuterShadowBits, kSmallInnerShadowBits, 5, true, smallRect.left,
			smallRect.top, buttonColorShadow1, buttonColor);
	} else {
		// draw combined inner and outer shadow
		DrawBits(view, kSmallOuterShadowBits, kSmallInnerShadowBits, 5, false,
			smallRect.right - 4, smallRect.bottom - 4, buttonColorShadow1, buttonColor);
	}

	// draw glint last (single pixel)
	StrokePoint(view, tab->zoomPressed ? smallRect.RightBottom() : smallRect.LeftTop(),
		buttonColorLight2);
}


void
BeDecorator::_GetButtonSizeAndOffset(const BRect& tabRect, float* _offset, float* _size,
	float* _inset) const
{
	const float tabSize = tabRect.Height();

	*_offset = 5.0f;
	*_inset = 0.0f;

	*_size = std::max(0.0f, tabSize - 7.0f);
}


void
BeDecorator::_DrawBevelRect(CompatView& view, const BRect rect, bool down, rgb_color light,
	rgb_color shadow)
{
	if (down) {
		const BRect inner(rect.InsetByCopy(1.0f, 1.0f));

		StrokeLine(view, rect.LeftBottom(), rect.LeftTop(), shadow);
		StrokeLine(view, rect.LeftTop(), rect.RightTop(), shadow);
		StrokeLine(view, inner.LeftBottom(), inner.LeftTop(), shadow);
		StrokeLine(view, inner.LeftTop(), inner.RightTop(), shadow);

		StrokeLine(view, rect.RightTop(), rect.RightBottom(), light);
		StrokeLine(view, rect.RightBottom(), rect.LeftBottom(), light);
		StrokeLine(view, inner.RightTop(), inner.RightBottom(), light);
		StrokeLine(view, inner.RightBottom(), inner.LeftBottom(), light);
	} else {
		BRect r1(rect);
		r1.left += 1.0f;
		r1.top += 1.0f;

		BRect r2(rect);
		r2.bottom -= 1.0f;
		r2.right -= 1.0f;

		StrokeRect(view, r2, shadow);
			// inner dark box
		StrokeRect(view, rect, shadow);
			// outer dark box
		StrokeRect(view, r1, light);
			// light box
	}
}


} // namespace ngui::detail
