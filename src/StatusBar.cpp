/*
 * Copyright 2001-2015, Haiku, Inc. All Rights Reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/StatusBar.cpp (commit b1d497bb69),
 * without archiving and scripting. NewGUI (see StatusBar.h):
 * - The geometry is Haiku's, computed with inclusive rectangles
 *   (compat::BRect); sizes are pixel counts, Haiku's plus 1.
 * - The bar color follows the palette until SetBarColor(); the background
 *   is the parent's view color (Haiku: AdoptParentColors(), low color), and
 *   the view fills nothing itself.
 * - Text is measured with TextSystem::Default() and drawn with the
 *   DrawContext's text system, plain font role.
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Joseph Groover <looncraz@looncraz.net>
 */


#include <ngui/StatusBar.h>

#include <algorithm>
#include <cmath>

#include <ngui/AppDefs.h>
#include <ngui/ControlLook.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>

#include "compat/CompatView.h"


namespace ngui {


using compat::BRect;
using compat::FromBRect;


namespace {


BRect
ToBRect(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


float
StringWidth(std::string_view text)
{
	return TextSystem::Default().StringWidth(text, FontRole::Plain);
}


} // namespace


BStatusBar::BStatusBar(gfx::Rect frame, std::string_view name, std::string_view label,
	std::string_view trailingLabel)
	:
	BView(frame, name, kFollowLeftTop, BViewFlag::WillDraw | BViewFlag::FrameEvents),
	fLabel(label),
	fTrailingLabel(trailingLabel)
{
}


BStatusBar::BStatusBar(std::string_view name, std::string_view label,
	std::string_view trailingLabel)
	:
	BView(name, BViewFlag::WillDraw | BViewFlag::FrameEvents),
	fLabel(label),
	fTrailingLabel(trailingLabel)
{
}


BStatusBar::~BStatusBar()
{
}


void
BStatusBar::AttachedToWindow()
{
	BView::AttachedToWindow();

	// resize so that the height fits
	float width, height;
	GetPreferredSize(&width, &height);
	ResizeTo(Bounds().Width(), height);
}


void
BStatusBar::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	Invalidate();
}


void
BStatusBar::GetPreferredSize(float* _width, float* _height)
{
	if (_width) {
		// AttachedToWindow() might not have been called yet
		*_width = std::ceil(StringWidth(fLabel))
			+ std::ceil(StringWidth(fTrailingLabel))
			+ std::ceil(StringWidth(fText))
			+ std::ceil(StringWidth(fTrailingText))
			+ 5
			+ 1;	// NewGUI: pixel counts
	}

	if (_height) {
		float labelHeight = 0;
		if (_HasText()) {
			const FontHeight fontHeight = TextSystem::Default().Height(FontRole::Plain);
			labelHeight = std::ceil(fontHeight.ascent + fontHeight.descent) + 6;
		}

		*_height = labelHeight + BarHeight()
			+ 1;	// NewGUI: pixel counts
	}
}


gfx::Size
BStatusBar::MinSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), gfx::Size{width, height});
}


gfx::Size
BStatusBar::MaxSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), gfx::Size{kSizeUnlimited, height});
}


gfx::Size
BStatusBar::PreferredSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), gfx::Size{width, height});
}


void
BStatusBar::Draw(DrawContext& context)
{
	const gfx::Color backgroundColor = _Background(context);

	const TextSystem& textSystem = context.text;
	const FontHeight fontHeight = textSystem.Height(FontRole::Plain);
	BRect barFrame = ToBRect(_BarFrame());
	BRect outerFrame = barFrame.InsetByCopy(-2, -2);

	// NewGUI: the background is the parent's (no view color)

	// Draw labels/texts

	BRect rect = outerFrame;
	rect.top = 0;
	rect.bottom = outerFrame.top - 1;

	{
		// update labels
		std::string leftText = fLabel + fText;
		std::string rightText = fTrailingText + fTrailingLabel;

		float baseLine = std::ceil(fontHeight.ascent) + 1;
		float textDivider = rect.right;

		if (!rightText.empty()) {
			rightText = textSystem.Truncate(rightText, rect.Width(),
				TruncationMode::Beginning, FontRole::Plain);
			textDivider -= textSystem.StringWidth(rightText, FontRole::Plain);
		}

		if (!leftText.empty()) {
			float width = std::max(0.0f, textDivider - rect.left);
			leftText = textSystem.Truncate(leftText, width, TruncationMode::End,
				FontRole::Plain);
		}

		gfx::Color textColor = context.palette.Get(BColorWhich::PanelText);

		if (backgroundColor != context.palette.Get(BColorWhich::PanelBackground)) {
			if (compat::ToRgbColor(backgroundColor).IsLight())
				textColor = gfx::Color::FromRgba8(0, 0, 0, 255);
			else
				textColor = gfx::Color::FromRgba8(255, 255, 255, 255);
		}

		const gfx::Paint paint{.color = textColor};
		if (!leftText.empty()) {
			const ShapedText shaped = textSystem.Shape(leftText, FontRole::Plain);
			context.canvas.DrawGlyphRun(shaped.Run(), {rect.left, baseLine}, paint);
		}

		if (!rightText.empty()) {
			const ShapedText shaped = textSystem.Shape(rightText, FontRole::Plain);
			context.canvas.DrawGlyphRun(shaped.Run(), {textDivider, baseLine}, paint);
		}
	}

	// Draw bar

	gfx::Rect frame = FromBRect(outerFrame);
	const gfx::Color barColor = fBarColor.value_or(
		context.palette.Get(BColorWhich::StatusBar));
	Look().DrawStatusBar(context, frame, backgroundColor, barColor,
		_BarPosition(_BarFrame()));
}


void
BStatusBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kUpdateStatusBar:
		{
			const float delta = message->GetFloat("delta", 0.0f);
			std::optional<std::string_view> text;
			std::optional<std::string_view> trailingText;
			if (auto found = message->FindString("text"))
				text = *found;
			if (auto found = message->FindString("trailing_text"))
				trailingText = *found;
			Update(delta, text, trailingText);
			break;
		}

		case kResetStatusBar:
		{
			const std::string label(message->GetString("label", ""));
			const std::string trailingLabel(message->GetString("trailing_label", ""));
			Reset(label, trailingLabel);
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


void
BStatusBar::SetBarColor(gfx::Color color)
{
	fBarColor = color;
	Invalidate();
}


void
BStatusBar::SetBarHeight(float barHeight)
{
	float oldHeight = BarHeight();

	fCustomBarHeight = true;
	fBarHeight = barHeight;

	if (barHeight == oldHeight)
		return;

	// resize so that the height fits
	if (Flags().Has(BViewFlag::SupportsLayout))
		InvalidateLayout();
	else {
		float width, height;
		GetPreferredSize(&width, &height);
		ResizeTo(Bounds().Width(), height);
	}
}


void
BStatusBar::SetText(std::string_view string)
{
	_SetTextData(fText, string);
}


void
BStatusBar::SetTrailingText(std::string_view string)
{
	_SetTextData(fTrailingText, string);
}


void
BStatusBar::SetMaxValue(float max)
{
	// R5 and/or Zeta's SetMaxValue does not trigger an invalidate here.
	// this is probably not ideal behavior, but it does break apps in some cases
	// as observed with SpaceMonitor.
	// TODO: revisit this when we break binary compatibility
	fMax = max;
}


void
BStatusBar::Update(float delta, std::optional<std::string_view> text,
	std::optional<std::string_view> trailingText)
{
	// If any of these are NULL, the existing text remains (BeBook)
	// NewGUI: copies, the views may point into the texts
	const std::string newText(text.value_or(fText));
	const std::string newTrailingText(trailingText.value_or(fTrailingText));
	BStatusBar::SetTo(fCurrent + delta, newText, newTrailingText);
}


void
BStatusBar::Reset(std::string_view label, std::string_view trailingLabel)
{
	// Reset replaces the label and trailing label with copies of the
	// strings passed as arguments. If either argument is NULL, the
	// label or trailing label will be deleted and erased.
	fLabel = label;
	fTrailingLabel = trailingLabel;

	// Reset deletes and erases any text or trailing text
	fText.clear();
	fTrailingText.clear();

	fCurrent = 0;
	fMax = 100;

	Invalidate();
}


void
BStatusBar::SetTo(float value, std::string_view text, std::string_view trailingText)
{
	SetText(text);
	SetTrailingText(trailingText);

	if (value > fMax)
		value = fMax;
	else if (value < 0)
		value = 0;
	if (value == fCurrent)
		return;

	const gfx::Rect barFrame = _BarFrame();
	float oldPosition = _BarPosition(barFrame);

	fCurrent = value;

	float newPosition = _BarPosition(barFrame);
	if (oldPosition == newPosition)
		return;

	// NewGUI: the whole view is recorded anew (11.4), no update area.
	Invalidate();
}


float
BStatusBar::CurrentValue() const
{
	return fCurrent;
}


float
BStatusBar::MaxValue() const
{
	return fMax;
}


gfx::Color
BStatusBar::BarColor() const
{
	return fBarColor.value_or(Palette::Default().Get(BColorWhich::StatusBar));
}


float
BStatusBar::BarHeight() const
{
	if (!fCustomBarHeight && fBarHeight == -1) {
		// the default bar height is as height as the label
		const FontHeight fontHeight = TextSystem::Default().Height(FontRole::Plain);
		fBarHeight = fontHeight.ascent + fontHeight.descent + 5;
	}

	return std::ceil(fBarHeight);
}


const std::string&
BStatusBar::Text() const
{
	return fText;
}


const std::string&
BStatusBar::TrailingText() const
{
	return fTrailingText;
}


const std::string&
BStatusBar::Label() const
{
	return fLabel;
}


const std::string&
BStatusBar::TrailingLabel() const
{
	return fTrailingLabel;
}


// #pragma mark - private


void
BStatusBar::_SetTextData(std::string& text, std::string_view source)
{
	// If there were no changes, we don't have to do anything
	if (text == source)
		return;

	bool oldHasText = _HasText();
	text = source;

	if (oldHasText != _HasText())
		InvalidateLayout();

	Invalidate();
}


/*!
	Returns the inner bar frame without the surrounding bevel.
*/
gfx::Rect
BStatusBar::_BarFrame() const
{
	float top = 2;
	if (_HasText()) {
		const FontHeight height = TextSystem::Default().Height(FontRole::Plain);
		top = std::ceil(height.ascent + height.descent) + 6;
	}

	const BRect bounds = ToBRect(Bounds());
	return FromBRect(BRect(2, top, bounds.right - 2, top + BarHeight() - 4));
}


float
BStatusBar::_BarPosition(const gfx::Rect& frame) const
{
	const BRect barFrame = ToBRect(frame);
	if (fCurrent == 0)
		return barFrame.left - 1;

	return std::round(barFrame.left - 1
		+ (fCurrent * (barFrame.Width() + 3) / fMax));
}


bool
BStatusBar::_HasText() const
{
	// Force BeOS behavior where the size of the BStatusBar always included
	// room for labels, even when there weren't any.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return true;

	return !fLabel.empty() || !fTrailingLabel.empty() || !fTrailingText.empty()
		|| !fText.empty();
}


gfx::Color
BStatusBar::_Background(const DrawContext& context) const
{
	// NewGUI: AdoptParentColors() and the low color
	if (Parent() != nullptr && Parent()->ViewColor().has_value())
		return *Parent()->ViewColor();

	return context.palette.Get(BColorWhich::PanelBackground);
}


} // namespace ngui
