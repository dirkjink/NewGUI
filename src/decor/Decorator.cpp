/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Authors (of Haiku's Decorator and TabDecorator):
 *		Stephan Aßmus, superstippi@gmx.de
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Ryan Leavengood, leavengood@gmail.com
 *		Philippe Saint-Pierre, stpere@gmail.com
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 *		Clemens Zeidler, haiku@clemens-zeidler.de
 *		Joseph Groover, looncraz@looncraz.net
 *		Jacob Secunda, secundaja@gmail.com
 */


#include "decor/Decorator.h"

#include <algorithm>
#include <cmath>

#include <ngui/Palette.h>
#include <ngui/TextSystem.h>

#include "looks/be/BeDecorator.h"


namespace ngui::detail {


using compat::BPoint;
using compat::BRect;
using compat::rgb_color;


namespace {


constexpr float kBorderResizeLength = 22.0f;
constexpr float kResizeKnobSize = 18.0f;


gfx::Rect
HalfOpen(const BRect& rect)
{
	if (!rect.IsValid())
		return {0, 0, 0, 0};
	return compat::FromBRect(rect);
}


} // namespace


Decorator::Decorator(const TextSystem& text)
	:
	fText(text),
	fFont(text, FontRole::Bold),
	fFrame(0, 0, -1, -1)
{
	_DoLayout();
}


Decorator::~Decorator() = default;


void
Decorator::SetLook(Look look)
{
	if (fTab.look == look)
		return;
	fTab.look = look;
	_DoLayout();
}


Decorator::Look
Decorator::GetLook() const
{
	return fTab.look;
}


void
Decorator::SetFlags(bool closable, bool zoomable, bool resizable)
{
	fTab.closable = closable;
	fTab.zoomable = zoomable;
	fResizable = resizable;
	_DoLayout();
}


bool
Decorator::IsClosable() const
{
	return fTab.closable;
}


bool
Decorator::IsZoomable() const
{
	return fTab.zoomable;
}


bool
Decorator::IsResizable() const
{
	return fResizable;
}


void
Decorator::SetTitle(std::string_view title)
{
	fTab.title = title;
	// TabDecorator::_SetTitle(): the tab's width depends on the title
	_DoLayout();
}


const std::string&
Decorator::Title() const
{
	return fTab.title;
}


const std::string&
Decorator::TruncatedTitle() const
{
	return fTab.truncatedTitle;
}


void
Decorator::SetFocus(bool focused)
{
	fTab.isFocused = focused;
	// TabDecorator::_SetFocus()
	fTab.buttonFocus = focused;
}


bool
Decorator::IsFocus() const
{
	return fTab.isFocused;
}


void
Decorator::SetContentSize(float width, float height)
{
	const BRect frame(0, 0, std::max(width, 0.0f) - 1, std::max(height, 0.0f) - 1);
	if (frame == fFrame)
		return;
	fFrame = frame;
	_DoLayout();
}


void
Decorator::SetPressed(Region button, bool pressed)
{
	if (button == Region::CloseButton)
		fTab.closePressed = pressed;
	else if (button == Region::ZoomButton)
		fTab.zoomPressed = pressed;
}


bool
Decorator::IsPressed(Region button) const
{
	if (button == Region::CloseButton)
		return fTab.closePressed;
	if (button == Region::ZoomButton)
		return fTab.zoomPressed;
	return false;
}


bool
Decorator::SetTabLocation(float location)
{
	// TabDecorator::_SetTabLocation() for one tab
	Tab* tab = &fTab;
	BRect& tabRect = tab->tabRect;
	if (!tabRect.IsValid())
		return false;

	if (location < 0)
		location = 0;

	const float maxLocation = fRightBorder.right - fLeftBorder.left - tabRect.Width();
	if (location > maxLocation)
		location = maxLocation;

	const float delta = std::floor(location - static_cast<float>(tab->tabOffset));
	if (delta == 0.0f)
		return false;

	tabRect.OffsetBy(delta, 0);
	tab->tabOffset = static_cast<std::uint32_t>(location);
	_LayoutTabItems(tab, tabRect);
	tab->tabLocation = maxLocation > 0.0f
		? static_cast<float>(tab->tabOffset) / maxLocation : 0.0f;
	fTitleBarRect = tabRect;
	return true;
}


float
Decorator::TabOffset() const
{
	return static_cast<float>(fTab.tabOffset);
}


float
Decorator::TabLocation() const
{
	return fTab.tabLocation;
}


gfx::Rect
Decorator::Bounds() const
{
	BRect bounds = fFrame;
	if (fBorderWidth > 0)
		bounds = bounds | fBorderRect;
	if (fTitleBarRect.IsValid())
		bounds = bounds | fTitleBarRect;
	return compat::FromBRect(bounds);
}


std::vector<gfx::Rect>
Decorator::Footprint() const
{
	// TabDecorator::_GetFootprint()
	std::vector<gfx::Rect> footprint;
	if (fTopTab->look == Look::NoBorder)
		return footprint;
	for (const BRect& border : {fTopBorder, fLeftBorder, fRightBorder, fBottomBorder}) {
		if (border.IsValid())
			footprint.push_back(HalfOpen(border));
	}
	if (fTopTab->look == Look::Bordered)
		return footprint;
	if (fTitleBarRect.IsValid())
		footprint.push_back(HalfOpen(fTitleBarRect));
	return footprint;
}


gfx::Rect
Decorator::TabRect() const
{
	return HalfOpen(fTab.tabRect);
}


gfx::Rect
Decorator::CloseRect() const
{
	return HalfOpen(fTab.closeRect);
}


gfx::Rect
Decorator::ZoomRect() const
{
	return HalfOpen(fTab.zoomRect);
}


float
Decorator::BorderWidth() const
{
	return static_cast<float>(fBorderWidth);
}


float
Decorator::TabHeight() const
{
	// the rows above the border (the tab's last one lies on it), as Haiku's
	// BWindow::_GetDecoratorSize()
	return fTitleBarRect.IsValid() ? fTitleBarRect.Height() : 0.0f;
}


Decorator::Region
Decorator::RegionAt(gfx::Point where) const
{
	// the pixel the point lies in, as Haiku's integer coordinates
	const BPoint point(std::floor(where.x), std::floor(where.y));

	// Decorator::RegionAt(): the buttons and the tab
	if (fTopTab->look == Look::Titled) {
		if (fTab.closeRect.Contains(point))
			return Region::CloseButton;
		if (fTab.zoomRect.Contains(point))
			return Region::ZoomButton;
		if (fTab.tabRect.Contains(point))
			return Region::Tab;
	}
	if (fBorderWidth <= 0)
		return Region::None;

	// TabDecorator::RegionAt(): the borders; NewGUI's titled windows have
	// no resize knob of B_DOCUMENT_WINDOW_LOOK
	if (fLeftBorder.Contains(point))
		return Region::LeftBorder;
	if (fTopBorder.Contains(point))
		return Region::TopBorder;

	// Part of the bottom and right borders may be a resize-region, so we have
	// to check explicitly, if it has been it.
	Region region;
	if (fRightBorder.Contains(point))
		region = Region::RightBorder;
	else if (fBottomBorder.Contains(point))
		region = Region::BottomBorder;
	else
		return Region::None;

	// check resize area
	if (fResizable && fTopTab->look == Look::Titled) {
		const BRect resizeRect(BPoint(fBottomBorder.right - fBorderResizeLength,
			fBottomBorder.bottom - fBorderResizeLength), fBottomBorder.RightBottom());
		if (resizeRect.Contains(point))
			return Region::RightBottomCorner;
	}

	return region;
}


void
Decorator::Draw(DrawContext& context)
{
	_UpdateColors(context.palette);
	compat::CompatView view(context);
	view.SetFont(&fFont);
	// TabDecorator::Draw(): the frame, then the tab
	_DrawFrame(view, fBorderRect);
	if (fTitleBarRect.IsValid())
		_DrawTab(view, &fTab, fTitleBarRect);
}


void
Decorator::_GetButtonSizeAndOffset(const BRect& tabRect, float* _offset, float* _size,
	float* _inset) const
{
	// TabDecorator::_GetButtonSizeAndOffset()
	const float tabSize = tabRect.Height();
	*_offset = std::floor(fFont.Size() / 2.3f);
	*_inset = std::floor(fFont.Size() / 6.0f);
	// "+ 2" so that the rects are centered within the solid area
	// (without the 2 pixels for the top border)
	*_size = tabSize - 2 * *_offset + *_inset;
}


void
Decorator::_DrawButtons(compat::CompatView& view, Tab* tab, const BRect& invalid)
{
	// TabDecorator::_DrawButtons()
	if (tab->closable && invalid.Intersects(tab->closeRect))
		_DrawClose(view, tab, tab->closeRect);
	if (tab->zoomable && invalid.Intersects(tab->zoomRect))
		_DrawZoom(view, tab, tab->zoomRect);
}


void
Decorator::_UpdateColors(const Palette& palette)
{
	// TabDecorator::UpdateColors()
	const auto color = [&palette](BColorWhich which) {
		return compat::ToRgbColor(palette.Get(which));
	};
	fFocusFrameColor = color(BColorWhich::WindowBorder);
	fFocusTabColor = color(BColorWhich::WindowTab);
	fFocusTabColorLight = compat::tint_color(fFocusTabColor,
		(compat::B_LIGHTEN_MAX_TINT + compat::B_LIGHTEN_2_TINT) / 2);
	fFocusTabColorBevel = compat::tint_color(fFocusTabColor, compat::B_LIGHTEN_2_TINT);
	fFocusTabColorShadow = compat::tint_color(fFocusTabColor,
		(compat::B_DARKEN_1_TINT + compat::B_NO_TINT) / 2);
	fFocusTextColor = color(BColorWhich::WindowText);

	fNonFocusFrameColor = color(BColorWhich::WindowInactiveBorder);
	fNonFocusTabColor = color(BColorWhich::WindowInactiveTab);
	fNonFocusTabColorLight = compat::tint_color(fNonFocusTabColor,
		(compat::B_LIGHTEN_MAX_TINT + compat::B_LIGHTEN_2_TINT) / 2);
	fNonFocusTabColorBevel = compat::tint_color(fNonFocusTabColor, compat::B_LIGHTEN_2_TINT);
	fNonFocusTabColorShadow = compat::tint_color(fNonFocusTabColor,
		(compat::B_DARKEN_1_TINT + compat::B_NO_TINT) / 2);
	fNonFocusTextColor = color(BColorWhich::WindowInactiveText);
}


bool
Decorator::_IsFocus(const Tab* tab) const
{
	return tab->isFocused;
}


void
Decorator::_DoLayout()
{
	// TabDecorator::_DoLayout(): every rectangle from the content's
	bool hasTab = false;

	const float scaleFactor = std::max(fFont.Size() / 12.0f, 1.0f);

	switch (fTopTab->look) {
		case Look::Titled:
			hasTab = true;
			fBorderWidth = 5;
			break;
		case Look::Bordered:
			fBorderWidth = 1;
			break;
		case Look::NoBorder:
			fBorderWidth = 0;
			break;
	}

	fBorderWidth = static_cast<std::int32_t>(static_cast<float>(fBorderWidth) * scaleFactor);
	fResizeKnobSize = kResizeKnobSize * scaleFactor;
	fBorderResizeLength = kBorderResizeLength * scaleFactor;
	const auto border = static_cast<float>(fBorderWidth);

	// calculate left/top/right/bottom borders
	if (fBorderWidth > 0) {
		// NOTE: no overlapping, the left and right border rects
		// don't include the corners!
		fLeftBorder.Set(fFrame.left - border, fFrame.top, fFrame.left - 1, fFrame.bottom);
		fRightBorder.Set(fFrame.right + 1, fFrame.top, fFrame.right + border, fFrame.bottom);
		fTopBorder.Set(fFrame.left - border, fFrame.top - border, fFrame.right + border,
			fFrame.top - 1);
		fBottomBorder.Set(fFrame.left - border, fFrame.bottom + 1, fFrame.right + border,
			fFrame.bottom + border);
	} else {
		// no border
		fLeftBorder.Set(0.0f, 0.0f, -1.0f, -1.0f);
		fRightBorder.Set(0.0f, 0.0f, -1.0f, -1.0f);
		fTopBorder.Set(0.0f, 0.0f, -1.0f, -1.0f);
		fBottomBorder.Set(0.0f, 0.0f, -1.0f, -1.0f);
	}

	fBorderRect = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());

	// calculate resize rect
	if (fBorderWidth > 1) {
		fResizeRect.Set(fBottomBorder.right - fResizeKnobSize,
			fBottomBorder.bottom - fResizeKnobSize, fBottomBorder.right, fBottomBorder.bottom);
	} else {
		// no border or one pixel border (menus and such)
		fResizeRect.Set(0, 0, -1, -1);
	}

	if (hasTab) {
		_DoTabLayout();
		return;
	}
	// no tab
	fTab.tabRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
	fTab.closeRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
	fTab.zoomRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
	fTitleBarRect.Set(0.0f, 0.0f, -1.0f, -1.0f);
}


void
Decorator::_DoTabLayout()
{
	// TabDecorator::_DoTabLayout() for one tab
	float tabSize = 0;
	float tabOffset = _SingleTabOffsetAndSize(tabSize);
	const auto border = static_cast<float>(fBorderWidth);

	Tab* tab = &fTab;
	BRect& tabRect = tab->tabRect;
	// distance from one item of the tab bar to another.
	// In this case the text and close/zoom rects
	tab->textOffset = _DefaultTextOffset();

	compat::font_height fontHeight;
	fFont.GetHeight(&fontHeight);

	const float spacing = border * 1.4f;
	tabRect.Set(fFrame.left - border,
		fFrame.top - border - std::ceil(fontHeight.ascent + fontHeight.descent + spacing),
		((fFrame.right - fFrame.left) < (spacing * 5)
			? fFrame.left + (spacing * 5) : fFrame.right) + border,
		fFrame.top - border);

	float offset;
	float size;
	float inset;
	_GetButtonSizeAndOffset(tabRect, &offset, &size, &inset);

	// tab->minTabSize contains just the room for the buttons
	tab->minTabSize = inset * 2 + tab->textOffset;
	if (tab->closable)
		tab->minTabSize += offset + size;
	if (tab->zoomable)
		tab->minTabSize += offset + size;

	// tab->maxTabSize contains tab->minTabSize + the width required for the
	// title
	tab->maxTabSize = _TitleWidth(tab->title);
	if (tab->maxTabSize > 0.0f)
		tab->maxTabSize += tab->textOffset;
	tab->maxTabSize += tab->minTabSize;

	tabSize = fFrame.Width() + border * 2;
	if (tabSize < tab->minTabSize)
		tabSize = tab->minTabSize;
	if (tabSize > tab->maxTabSize)
		tabSize = tab->maxTabSize;

	// layout buttons and truncate text
	tabRect.right = tabRect.left + tabSize;

	// make sure fTabOffset is within limits and apply it to
	// the tabRect
	tab->tabOffset = static_cast<std::uint32_t>(tabOffset);
	if (tab->tabLocation != 0.0f
		&& static_cast<float>(tab->tabOffset)
			> (fRightBorder.right - fLeftBorder.left - tabRect.Width())) {
		tab->tabOffset = static_cast<std::uint32_t>(std::max(0.0f,
			fRightBorder.right - fLeftBorder.left - tabRect.Width()));
	}
	tabRect.OffsetBy(static_cast<float>(tab->tabOffset), 0);
	tabOffset += tabRect.Width();

	// finally, layout the buttons and text within the tab rect
	fTitleBarRect = tab->tabRect;
	_LayoutTabItems(tab, tab->tabRect);
}


void
Decorator::_LayoutTabItems(Tab* tab, const BRect& tabRect)
{
	// TabDecorator::_LayoutTabItems() for one tab
	float offset;
	float size;
	float inset;
	_GetButtonSizeAndOffset(tabRect, &offset, &size, &inset);

	// default textOffset
	tab->textOffset = _DefaultTextOffset();

	BRect& closeRect = tab->closeRect;
	BRect& zoomRect = tab->zoomRect;

	// calculate close rect based on the tab rectangle
	closeRect.Set(tabRect.left + offset, tabRect.top + offset, tabRect.left + offset + size,
		tabRect.top + offset + size);

	zoomRect.Set(tabRect.right - offset - size, tabRect.top + offset, tabRect.right - offset,
		tabRect.top + offset + size);

	// hidden buttons have no width
	if (!tab->closable)
		closeRect.right = closeRect.left - offset;
	if (!tab->zoomable)
		zoomRect.left = zoomRect.right + offset;

	// calculate room for title
	// TODO: the +2 is there because the title often appeared
	//	truncated for no apparent reason - OTOH the title does
	//	also not appear perfectly in the middle
	size = (zoomRect.left - closeRect.right) - tab->textOffset * 2 + inset;

	// NewGUI: a tab without room for text (a window without a size yet)
	// truncates to nothing, as Haiku's TruncateString() does
	tab->truncatedTitle = tab->title;
	fFont.TruncateString(&tab->truncatedTitle, compat::B_TRUNCATE_MIDDLE,
		std::max(size, 0.0f));
}


// NewGUI: the title's width as the truncation measures it. Haiku measures
// the title and truncates it by the same advances; the text system shapes
// the width (kerning) but truncates by the single characters' advances, so
// the smallest whole width that keeps the title whole.
float
Decorator::_TitleWidth(const std::string& title) const
{
	const float shaped = std::ceil(fFont.StringWidth(title.c_str()));
	if (title.empty())
		return shaped;
	const float scale = fFont.Scale();
	for (float width = shaped; width < shaped * 2 + 16; width += 1) {
		if (fText.Truncate(title, width / scale, TruncationMode::Middle, fFont.Role())
				== title) {
			return width;
		}
	}
	return shaped;
}


float
Decorator::_DefaultTextOffset() const
{
	// TabDecorator::_DefaultTextOffset() of a titled window
	return static_cast<float>(static_cast<std::int32_t>(static_cast<float>(fBorderWidth) * 3.6f));
}


float
Decorator::_SingleTabOffsetAndSize(float& tabSize)
{
	tabSize = fRightBorder.right - fLeftBorder.left;
	float maxLocation = tabSize - fTab.maxTabSize;
	if (maxLocation < 0)
		maxLocation = 0;

	return std::floor(fTab.tabLocation * maxLocation);
}


std::unique_ptr<Decorator>
MakeDecorator(std::string_view lookId, const TextSystem& text)
{
	// the looks with a decorator; "haiku" and "flat" follow
	if (lookId == "be")
		return std::make_unique<BeDecorator>(text);
	return nullptr;
}


} // namespace ngui::detail
