/*
 * Copyright 2001-2016 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Slider.cpp (commit b1d497bb69),
 * without archiving, scripting, icons and the off-screen view (Haiku has it
 * switched off, USE_OFF_SCREEN_VIEW 0). NewGUI (see Slider.h):
 * - The geometry is Haiku's: BarFrame(), ThumbFrame() and the positions
 *   are computed with inclusive rectangles (compat::BRect) and converted
 *   at the public interface (FromBRect / ToBRect), so the pixels are the
 *   same; the focus mark is drawn through CompatView.
 * - Sizes are pixel counts: the public sizes are Haiku's plus 1; the
 *   minimum size is cached in Haiku's convention.
 * - Asynchronous tracking only; the modification message is sent with
 *   Invoke() (Haiku: InvokeNotify(..., B_CONTROL_MODIFIED)).
 * - Draw() does not fill the background; the view does (its view color).
 * - Text is measured with TextSystem::Default() and drawn with the
 *   DrawContext's text system, plain font role.
 *
 * Authors (Haiku):
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Marc Flerackers (mflerackers@androme.be)
 */


#include <ngui/Slider.h>

#include <algorithm>
#include <cmath>

#include <ngui/AppDefs.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>

#include "compat/CompatView.h"


namespace ngui {


using compat::BPoint;
using compat::BRect;
using compat::FromBRect;


namespace {


// A half-open rectangle as Haiku's inclusive one on the same pixels.
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


FontHeight
GetFontHeight()
{
	return TextSystem::Default().Height(FontRole::Plain);
}


} // namespace


BSlider::BSlider(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, int32 minValue, int32 maxValue,
	BThumbStyle thumbType, BResizingMode resizingMode, BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode, flags),
	fMinValue(minValue),
	fMaxValue(maxValue),
	fStyle(thumbType),
	fOrientation(BOrientation::Horizontal)
{
	_InitBarColor();
	_InitObject();
	SetValue(0);
}


BSlider::BSlider(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, int32 minValue, int32 maxValue,
	BOrientation posture, BThumbStyle thumbType, BResizingMode resizingMode,
	BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode, flags),
	fMinValue(minValue),
	fMaxValue(maxValue),
	fStyle(thumbType),
	fOrientation(posture)
{
	_InitBarColor();
	_InitObject();
	SetValue(0);
}


BSlider::BSlider(std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, int32 minValue, int32 maxValue,
	BOrientation posture, BThumbStyle thumbType, BViewFlags flags)
	:
	BControl(name, label, std::move(message), flags),
	fMinValue(minValue),
	fMaxValue(maxValue),
	fStyle(thumbType),
	fOrientation(posture)
{
	_InitBarColor();
	_InitObject();
	SetValue(0);
}


BSlider::~BSlider()
{
}


void
BSlider::_InitBarColor()
{
	const Palette palette = Palette::Current();
	SetBarColor(Look().SliderBarColor(palette, palette.Get(BColorWhich::PanelBackground)));
	// NewGUI: the bar follows the application's colors until set
	fDefaultBarColor = true;
	UseFillColor(false, nullptr);
}


void
BSlider::_InitObject()
{
	fLocation = {0, 0};
	fInitialLocation = {0, 0};

	fUpdateText.clear();
	fMinSize = {-1, -1};
	fMaxUpdateTextWidth = -1.0f;
}


void
BSlider::AttachedToWindow()
{
	BControl::AttachedToWindow();

	// AdoptSystemColors()
	SetViewUIColor(BColorWhich::PanelBackground);

	ResizeToPreferred();

	int32 value = Value();
	SetValue(value);
		// makes sure the value is within valid bounds
	_SetLocationForValue(Value());
		// makes sure the location is correct
	UpdateTextChanged();
}


void
BSlider::FrameResized(float w, float h)
{
	BControl::FrameResized(w, h);

	BRect bounds(ToBRect(Bounds()));

	if (bounds.right <= 0.0f || bounds.bottom <= 0.0f)
		return;

	Invalidate();
}


void
BSlider::KeyDown(std::string_view bytes)
{
	if (!IsEnabled() || IsHidden() || bytes.empty())
		return;

	int32 newValue = Value();

	switch (bytes[0]) {
		case keys::kLeftArrow:
		case keys::kDownArrow:
			newValue -= KeyIncrementValue();
			break;

		case keys::kRightArrow:
		case keys::kUpArrow:
			newValue += KeyIncrementValue();
			break;

		case keys::kHome:
			newValue = fMinValue;
			break;

		case keys::kEnd:
			newValue = fMaxValue;
			break;

		default:
			BControl::KeyDown(bytes);
			return;
	}

	if (newValue < fMinValue)
		newValue = fMinValue;

	if (newValue > fMaxValue)
		newValue = fMaxValue;

	if (newValue != Value()) {
		fInitialLocation = _Location();
		SetValue(newValue);
		if (ModificationMessage() != nullptr)
			(void)Invoke(ModificationMessage());
	}
}


void
BSlider::KeyUp(std::string_view /*bytes*/)
{
	if (fInitialLocation != _Location()) {
		// The last KeyDown event triggered the modification message or no
		// notification at all, we may also have sent the modification message
		// continually while the user kept pressing the key. In either case,
		// finish with the final message to make the behavior consistent with
		// changing the value by mouse.
		(void)Invoke();
		// NewGUI: once; Haiku compared again on every key up
		fInitialLocation = _Location();
	}
}


/*!
	Makes sure the \a point is within valid bounds.
	Returns \c true if the relevant coordinate (depending on the orientation
	of the slider) differs from \a comparePoint.
*/
bool
BSlider::_ConstrainPoint(gfx::Point& point, gfx::Point comparePoint) const
{
	if (fOrientation == BOrientation::Horizontal) {
		if (point.x != comparePoint.x) {
			if (point.x < _MinPosition())
				point.x = _MinPosition();
			else if (point.x > _MaxPosition())
				point.x = _MaxPosition();

			return true;
		}
	} else {
		if (point.y != comparePoint.y) {
			if (point.y > _MinPosition())
				point.y = _MinPosition();
			else if (point.y < _MaxPosition())
				point.y = _MaxPosition();

			return true;
		}
	}

	return false;
}


void
BSlider::MouseDown(gfx::Point point)
{
	if (!IsEnabled())
		return;

	if (BarFrame().Contains(point) || ThumbFrame().Contains(point))
		fInitialLocation = _Location();

	_ConstrainPoint(point, fInitialLocation);
	SetValue(ValueForPoint(point));

	if (_Location() != fInitialLocation && ModificationMessage() != nullptr)
		(void)Invoke(ModificationMessage());

	// NewGUI: always as with B_ASYNCHRONOUS_CONTROLS
	SetTracking(true);
	SetMouseEventMask(BEventMask::Pointer);
}


void
BSlider::MouseUp(gfx::Point point)
{
	if (IsTracking()) {
		if (_Location() != fInitialLocation)
			(void)Invoke();

		SetTracking(false);
	} else
		BControl::MouseUp(point);
}


void
BSlider::MouseMoved(gfx::Point point, BMouseTransit transit, const BMessage* message)
{
	if (IsTracking()) {
		if (_ConstrainPoint(point, _Location())) {
			int32 value = ValueForPoint(point);
			if (value != Value()) {
				SetValue(value);
				if (ModificationMessage() != nullptr)
					(void)Invoke(ModificationMessage());
			}
		}
	} else
		BControl::MouseMoved(point, transit, message);
}


void
BSlider::SetLabel(std::string_view label)
{
	BControl::SetLabel(label);
}


void
BSlider::SetLimitLabels(std::string_view minLabel, std::string_view maxLabel)
{
	fMinLimitLabel = minLabel;
	fMaxLimitLabel = maxLabel;

	InvalidateLayout();

	// TODO: This is for backwards compatibility and should
	// probably be removed when breaking binary compatiblity.
	// Applications like our own Mouse rely on this behavior.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		ResizeToPreferred();

	Invalidate();
}


const std::string&
BSlider::MinLimitLabel() const
{
	return fMinLimitLabel;
}


const std::string&
BSlider::MaxLimitLabel() const
{
	return fMaxLimitLabel;
}


void
BSlider::SetValue(int32 value)
{
	if (value < fMinValue)
		value = fMinValue;

	if (value > fMaxValue)
		value = fMaxValue;

	if (value == Value())
		return;

	_SetLocationForValue(value);

	BControl::SetValueNoUpdate(value);

	// NewGUI: the whole view is recorded anew (11.4), no invalid area.
	Invalidate();

	UpdateTextChanged();
}


int32
BSlider::ValueForPoint(gfx::Point location) const
{
	float min;
	float max;
	float position;
	if (fOrientation == BOrientation::Horizontal) {
		min = _MinPosition();
		max = _MaxPosition();
		position = location.x;
	} else {
		max = _MinPosition();
		min = _MaxPosition();
		position = min + (max - location.y);
	}

	if (position < min)
		position = min;

	if (position > max)
		position = max;

	return (int32)std::round(((position - min) * (float)(fMaxValue - fMinValue)
		/ (max - min)) + (float)fMinValue);
}


void
BSlider::SetPosition(float position)
{
	if (position <= 0.0f)
		SetValue(fMinValue);
	else if (position >= 1.0f)
		SetValue(fMaxValue);
	else
		SetValue((int32)(position * (float)(fMaxValue - fMinValue) + (float)fMinValue));
}


float
BSlider::Position() const
{
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0.0f)
		range = 1.0f;

	return (float)(Value() - fMinValue) / range;
}


void
BSlider::GetLimits(int32* minimum, int32* maximum) const
{
	if (minimum != nullptr)
		*minimum = fMinValue;

	if (maximum != nullptr)
		*maximum = fMaxValue;
}


// #pragma mark - drawing


void
BSlider::Draw(DrawContext& context)
{
	// NewGUI: the view color is filled by the view (Haiku filled the area
	// outside the bar here).
	DrawSlider(context);
}


void
BSlider::DrawSlider(DrawContext& context)
{
	DrawBar(context);
	DrawHashMarks(context);
	DrawThumb(context);
	DrawFocusMark(context);
	DrawText(context);
}


void
BSlider::DrawBar(DrawContext& context)
{
	gfx::Rect frame = BarFrame();
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);

	gfx::Color rightFillColor = fBarColor;
	gfx::Color leftFillColor = fUseFillColor ? fFillColor : fBarColor;
	look.DrawSliderBar(context, frame, base, leftFillColor, rightFillColor, Position(),
		flags, fOrientation);
}


void
BSlider::DrawHashMarks(DrawContext& context)
{
	if (fHashMarks == BHashMarkLocation::None)
		return;

	gfx::Rect frame = HashMarksFrame();
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);

	look.DrawSliderHashMarks(context, frame, base, fHashMarkCount, fHashMarks, flags,
		fOrientation);
}


void
BSlider::DrawThumb(DrawContext& context)
{
	if (Style() == BThumbStyle::Block)
		_DrawBlockThumb(context);
	else
		_DrawTriangleThumb(context);
}


void
BSlider::DrawFocusMark(DrawContext& context)
{
	if (!IsFocus())
		return;

	compat::CompatView view(context);
	BRect frame = ToBRect(ThumbFrame());

	view.PushState();
	view.SetHighColor(view.UiColor(compat::B_KEYBOARD_NAVIGATION_COLOR));

	if (fStyle == BThumbStyle::Block) {
		frame.left += 2.0f;
		frame.top += 2.0f;
		frame.right -= 3.0f;
		frame.bottom -= 3.0f;
		view.StrokeRect(frame);
	} else {
		if (fOrientation == BOrientation::Horizontal) {
			view.StrokeLine(BPoint(frame.left, frame.bottom + 2.0f),
				BPoint(frame.right, frame.bottom + 2.0f));
		} else {
			view.StrokeLine(BPoint(frame.left - 2.0f, frame.top),
				BPoint(frame.left - 2.0f, frame.bottom));
		}
	}

	view.PopState();
}


void
BSlider::DrawText(DrawContext& context)
{
	BRect bounds(ToBRect(Bounds()));
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	const gfx::Color text = context.palette.Get(BColorWhich::PanelText);
	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);

	const TextSystem& textSystem = context.text;
	const FontHeight fontHeight = textSystem.Height(FontRole::Plain);
	const auto stringWidth = [&](std::string_view string) {
		return textSystem.StringWidth(string, FontRole::Plain);
	};

	if (Orientation() == BOrientation::Horizontal) {
		if (!Label().empty()) {
			look.DrawLabel(context, Label(), base, flags,
				gfx::Point{0.0f, std::ceil(fontHeight.ascent)}, &text);
		}

		// the update text is updated in SetValue() only
		if (!fUpdateText.empty()) {
			look.DrawLabel(context, fUpdateText, base, flags,
				gfx::Point{bounds.right - stringWidth(fUpdateText),
					std::ceil(fontHeight.ascent)}, &text);
		}

		if (!fMinLimitLabel.empty()) {
			look.DrawLabel(context, fMinLimitLabel, base, flags,
				gfx::Point{0.0f, bounds.bottom - fontHeight.descent}, &text);
		}

		if (!fMaxLimitLabel.empty()) {
			look.DrawLabel(context, fMaxLimitLabel, base, flags,
				gfx::Point{bounds.right - stringWidth(fMaxLimitLabel),
					bounds.bottom - fontHeight.descent}, &text);
		}
	} else {
		float lineHeight = std::ceil(fontHeight.ascent) + std::ceil(fontHeight.descent)
			+ std::ceil(fontHeight.leading);
		float baseLine = std::ceil(fontHeight.ascent);

		if (!Label().empty()) {
			look.DrawLabel(context, Label(), base, flags,
				gfx::Point{(bounds.Width() - stringWidth(Label())) / 2.0f, baseLine},
				&text);
			baseLine += lineHeight;
		}

		if (!fMaxLimitLabel.empty()) {
			look.DrawLabel(context, fMaxLimitLabel, base, flags,
				gfx::Point{(bounds.Width() - stringWidth(fMaxLimitLabel)) / 2.0f,
					baseLine}, &text);
		}

		baseLine = bounds.bottom - std::ceil(fontHeight.descent);

		if (!fMinLimitLabel.empty()) {
			look.DrawLabel(context, fMinLimitLabel, base, flags,
				gfx::Point{(bounds.Width() - stringWidth(fMinLimitLabel)) / 2.0f,
					baseLine}, &text);
				baseLine -= lineHeight;
		}

		if (!fUpdateText.empty()) {
			look.DrawLabel(context, fUpdateText, base, flags,
				gfx::Point{(bounds.Width() - stringWidth(fUpdateText)) / 2.0f, baseLine},
				&text);
		}
	}
}


// #pragma mark -


std::string
BSlider::UpdateText() const
{
	return {};
}


void
BSlider::UpdateTextChanged()
{
	// update text label (NewGUI: the whole view is recorded anew, so there
	// is no area to invalidate)
	const std::string oldUpdateText = fUpdateText;
	fUpdateText = UpdateText();
	if (fUpdateText != oldUpdateText)
		Invalidate();

	float oldMaxUpdateTextWidth = fMaxUpdateTextWidth;
	fMaxUpdateTextWidth = MaxUpdateTextWidth();
	if (oldMaxUpdateTextWidth != fMaxUpdateTextWidth)
		InvalidateLayout();
}


gfx::Rect
BSlider::BarFrame() const
{
	BRect frame(ToBRect(Bounds()));

	const FontHeight fontHeight = GetFontHeight();

	float textHeight = std::ceil(fontHeight.ascent) + std::ceil(fontHeight.descent);
	float leading = std::ceil(fontHeight.leading);

	float thumbInset;
	if (fStyle == BThumbStyle::Block)
		thumbInset = 8.0f;
	else
		thumbInset = 7.0f;

	const bool hasLabel = !Label().empty();
	if (Orientation() == BOrientation::Horizontal) {
		frame.left = thumbInset;
		frame.top = 6.0f + (hasLabel || !fUpdateText.empty() ? textHeight + 4.0f : 0.0f);
		frame.right -= thumbInset;
		frame.bottom = frame.top + fBarThickness;
	} else {
		frame.left = std::floor((frame.Width() - fBarThickness) / 2.0f);
		frame.top = thumbInset;
		if (hasLabel)
			frame.top += textHeight;

		if (!fMaxLimitLabel.empty()) {
			frame.top += textHeight;
			if (hasLabel)
				frame.top += leading;
		}

		frame.right = frame.left + fBarThickness;
		frame.bottom = frame.bottom - thumbInset;
		if (!fMinLimitLabel.empty())
			frame.bottom -= textHeight;

		if (!fUpdateText.empty()) {
			frame.bottom -= textHeight;
			if (!fMinLimitLabel.empty())
				frame.bottom -= leading;
		}
	}

	return FromBRect(frame);
}


gfx::Rect
BSlider::HashMarksFrame() const
{
	BRect frame(ToBRect(BarFrame()));

	if (fOrientation == BOrientation::Horizontal) {
		frame.top -= 6.0f;
		frame.bottom += 6.0f;
	} else {
		frame.left -= 6.0f;
		frame.right += 6.0f;
	}

	return FromBRect(frame);
}


gfx::Rect
BSlider::ThumbFrame() const
{
	// TODO: The slider looks really ugly and broken when it is too little.
	// I would suggest using BarFrame() here to get the top and bottom coords
	// and spread them further apart for the thumb

	BRect frame(ToBRect(Bounds()));

	const FontHeight fontHeight = GetFontHeight();

	float textHeight = std::ceil(fontHeight.ascent) + std::ceil(fontHeight.descent);

	const bool hasLabel = !Label().empty();
	if (fStyle == BThumbStyle::Block) {
		if (Orientation() == BOrientation::Horizontal) {
			frame.left = std::floor(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.top = 2 + (hasLabel || !fUpdateText.empty() ? textHeight + 4 : 0);
			frame.right = frame.left + 17;
			frame.bottom = frame.top + fBarThickness + 7;
		} else {
			frame.left = std::floor((frame.Width() - fBarThickness) / 2) - 4;
			frame.top = std::floor(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 8;
			frame.right = frame.left + fBarThickness + 7;
			frame.bottom = frame.top + 17;
		}
	} else {
		if (Orientation() == BOrientation::Horizontal) {
			frame.left = std::floor(Position() * (_MaxPosition()
				- _MinPosition()) + _MinPosition()) - 6;
			frame.right = frame.left + 12;
			frame.top = 3 + fBarThickness + (hasLabel ? textHeight + 4 : 0);
			frame.bottom = frame.top + 8;
		} else {
			frame.left = std::floor((frame.Width() + fBarThickness) / 2) - 3;
			frame.top = std::floor(Position() * (_MaxPosition()
				- _MinPosition())) + _MinPosition() - 6;
			frame.right = frame.left + 8;
			frame.bottom = frame.top + 12;
		}
	}

	return FromBRect(frame);
}


void
BSlider::GetPreferredSize(float* _width, float* _height)
{
	gfx::Size preferredSize = PreferredSize();

	if (Orientation() == BOrientation::Horizontal) {
		if (_width != nullptr) {
			// NOTE: For compatibility reasons, a horizontal BSlider
			// never shrinks horizontally. This only affects applications
			// which do not use the new layout system.
			*_width = std::max(Bounds().Width(), preferredSize.w);
		}

		if (_height != nullptr)
			*_height = preferredSize.h;
	} else {
		if (_width != nullptr)
			*_width = preferredSize.w;
		if (_height != nullptr) {
			// NOTE: Similarly, a vertical BSlider never shrinks
			// vertically. This only affects applications which do not
			// use the new layout system.
			*_height = std::max(Bounds().Height(), preferredSize.h);
		}
	}
}


void
BSlider::SetModificationMessage(std::unique_ptr<BMessage> message)
{
	fModificationMessage = std::move(message);
}


BMessage*
BSlider::ModificationMessage() const
{
	return fModificationMessage.get();
}


void
BSlider::SetSnoozeAmount(int32 snoozeTime)
{
	if (snoozeTime < 10000)
		snoozeTime = 10000;
	else if (snoozeTime > 1000000)
		snoozeTime = 1000000;

	fSnoozeAmount = snoozeTime;
}


int32
BSlider::SnoozeAmount() const
{
	return fSnoozeAmount;
}


void
BSlider::SetKeyIncrementValue(int32 incrementValue)
{
	fKeyIncrementValue = incrementValue;
}


int32
BSlider::KeyIncrementValue() const
{
	return fKeyIncrementValue;
}


void
BSlider::SetHashMarkCount(int32 hashMarkCount)
{
	fHashMarkCount = hashMarkCount;
	Invalidate();
}


int32
BSlider::HashMarkCount() const
{
	return fHashMarkCount;
}


void
BSlider::SetHashMarks(BHashMarkLocation where)
{
	fHashMarks = where;
// TODO: enable if the hashmark look is influencing the control size!
//	InvalidateLayout();
	Invalidate();
}


BHashMarkLocation
BSlider::HashMarks() const
{
	return fHashMarks;
}


void
BSlider::SetStyle(BThumbStyle style)
{
	fStyle = style;
	InvalidateLayout();
	Invalidate();
}


BThumbStyle
BSlider::Style() const
{
	return fStyle;
}


void
BSlider::SetBarColor(gfx::Color barColor)
{
	fBarColor = barColor;
	fDefaultBarColor = false;
	Invalidate();
}


void
BSlider::MessageReceived(BMessage* message)
{
	// NewGUI: the default bar in the application's new colors
	if (message->what == kColorsUpdated && fDefaultBarColor) {
		const Palette palette = Palette::Current();
		fBarColor = Look().SliderBarColor(palette, palette.Get(BColorWhich::PanelBackground));
	}
	BControl::MessageReceived(message);
}


gfx::Color
BSlider::BarColor() const
{
	return fBarColor;
}


void
BSlider::UseFillColor(bool useFill, const gfx::Color* barColor)
{
	fUseFillColor = useFill;

	if (useFill && barColor)
		fFillColor = *barColor;

	Invalidate();
}


bool
BSlider::FillColor(gfx::Color* barColor) const
{
	if (barColor && fUseFillColor)
		*barColor = fFillColor;

	return fUseFillColor;
}


BOrientation
BSlider::Orientation() const
{
	return fOrientation;
}


void
BSlider::SetOrientation(BOrientation posture)
{
	if (fOrientation == posture)
		return;

	fOrientation = posture;
	InvalidateLayout();
	Invalidate();
}


float
BSlider::BarThickness() const
{
	return fBarThickness;
}


void
BSlider::SetBarThickness(float thickness)
{
	if (thickness < 1.0f)
		thickness = 1.0f;
	else
		thickness = std::round(thickness);

	if (thickness != fBarThickness) {
		fBarThickness = thickness;
		Invalidate();
		InvalidateLayout();
	}
}


void
BSlider::SetLimits(int32 minimum, int32 maximum)
{
	if (minimum <= maximum) {
		fMinValue = minimum;
		fMaxValue = maximum;

		int32 value = Value();
		value = std::max(minimum, value);
		value = std::min(maximum, value);

		if (value != Value())
			SetValue(value);
	}
}


float
BSlider::MaxUpdateTextWidth()
{
	// very simplistic implementation that assumes the string will be widest
	// at the maximum value
	int32 value = Value();
	SetValueNoUpdate(fMaxValue);
	float width = StringWidth(UpdateText());
	SetValueNoUpdate(value);
	// in case the derived class uses a fixed buffer, the contents
	// should be reset for the old value
	(void)UpdateText();

	return width;
}


// #pragma mark - layout related


gfx::Size
BSlider::MinSize()
{
	gfx::Size minSize = _ValidateMinSize();
	// NewGUI: pixel counts
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		gfx::Size{minSize.w + 1, minSize.h + 1});
}


gfx::Size
BSlider::MaxSize()
{
	gfx::Size maxSize = _ValidateMinSize();
	// NewGUI: pixel counts
	maxSize = {maxSize.w + 1, maxSize.h + 1};
	if (fOrientation == BOrientation::Horizontal)
		maxSize.w = kSizeUnlimited;
	else
		maxSize.h = kSizeUnlimited;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


gfx::Size
BSlider::PreferredSize()
{
	gfx::Size preferredSize = _ValidateMinSize();
	if (fOrientation == BOrientation::Horizontal)
		preferredSize.w = std::max(100.0f, preferredSize.w);
	else
		preferredSize.h = std::max(100.0f, preferredSize.h);

	// NewGUI: pixel counts
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		gfx::Size{preferredSize.w + 1, preferredSize.h + 1});
}


void
BSlider::LayoutInvalidated(bool /*descendants*/)
{
	// invalidate cached preferred size
	fMinSize = {-1, -1};
}


// #pragma mark - private


void
BSlider::_DrawBlockThumb(DrawContext& context)
{
	gfx::Rect frame = ThumbFrame();
	const gfx::Color base = context.palette.Get(BColorWhich::ControlBackground);
	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);

	look.DrawSliderThumb(context, frame, base, flags, fOrientation);
}


void
BSlider::_DrawTriangleThumb(DrawContext& context)
{
	gfx::Rect frame = ThumbFrame();
	const gfx::Color base = context.palette.Get(BColorWhich::ControlBackground);
	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);

	look.DrawSliderTriangle(context, frame, base, flags, fOrientation);
}


gfx::Point
BSlider::_Location() const
{
	return fLocation;
}


void
BSlider::_SetLocationForValue(int32 value)
{
	gfx::Point loc;
	float range = (float)(fMaxValue - fMinValue);
	if (range == 0)
		range = 1;

	float pos = (float)(value - fMinValue) / range *
		(_MaxPosition() - _MinPosition());

	if (fOrientation == BOrientation::Horizontal) {
		loc.x = std::ceil(_MinPosition() + pos);
		loc.y = 0;
	} else {
		loc.x = 0;
		loc.y = std::floor(_MaxPosition() - pos);
	}
	fLocation = loc;
}


float
BSlider::_MinPosition() const
{
	BRect bar(ToBRect(BarFrame()));
	if (fOrientation == BOrientation::Horizontal)
		return bar.left + 1.0f;

	return bar.bottom - 1.0f;
}


float
BSlider::_MaxPosition() const
{
	BRect bar(ToBRect(BarFrame()));
	if (fOrientation == BOrientation::Horizontal)
		return bar.right - 1.0f;

	return bar.top + 1.0f;
}


gfx::Size
BSlider::_ValidateMinSize()
{
	if (fMinSize.w >= 0) {
		// the preferred size is up to date
		return fMinSize;
	}

	const FontHeight fontHeight = GetFontHeight();

	float width = 0.0f;
	float height = 0.0f;

	if (fMaxUpdateTextWidth < 0.0f)
		fMaxUpdateTextWidth = MaxUpdateTextWidth();

	const bool hasLabel = !Label().empty();
	if (Orientation() == BOrientation::Horizontal) {
		height = 12.0f + fBarThickness;
		int32 rows = 0;

		float labelWidth = 0;
		int32 labelRows = 0;
		float labelSpacing = StringWidth("M") * 2;
		if (hasLabel) {
			labelWidth = StringWidth(Label());
			labelRows = 1;
		}
		if (fMaxUpdateTextWidth > 0.0f) {
			if (labelWidth > 0)
				labelWidth += labelSpacing;
			labelWidth += fMaxUpdateTextWidth;
			labelRows = 1;
		}
		rows += labelRows;

		if (!MinLimitLabel().empty())
			width = StringWidth(MinLimitLabel());
		if (!MaxLimitLabel().empty()) {
			// some space between the labels
			if (!MinLimitLabel().empty())
				width += labelSpacing;

			width += StringWidth(MaxLimitLabel());
		}

		if (labelWidth > width)
			width = labelWidth;
		if (width < 32.0f)
			width = 32.0f;

		if (!MinLimitLabel().empty() || !MaxLimitLabel().empty())
			rows++;

		height += (float)rows * (std::ceil(fontHeight.ascent)
			+ std::ceil(fontHeight.descent) + 4.0f);
	} else {
		// B_VERTICAL
		width = 12.0f + fBarThickness;
		height = 32.0f;

		float lineHeightNoLeading = std::ceil(fontHeight.ascent)
			+ std::ceil(fontHeight.descent);
		float lineHeight = lineHeightNoLeading + std::ceil(fontHeight.leading);

		// find largest label
		float labelWidth = 0;
		if (hasLabel) {
			labelWidth = StringWidth(Label());
			height += lineHeightNoLeading;
		}
		if (!MaxLimitLabel().empty()) {
			labelWidth = std::max(labelWidth, StringWidth(MaxLimitLabel()));
			height += hasLabel ? lineHeight : lineHeightNoLeading;
		}
		if (!MinLimitLabel().empty()) {
			labelWidth = std::max(labelWidth, StringWidth(MinLimitLabel()));
			height += lineHeightNoLeading;
		}
		if (fMaxUpdateTextWidth > 0.0f) {
			labelWidth = std::max(labelWidth, fMaxUpdateTextWidth);
			height += !MinLimitLabel().empty() ? lineHeight : lineHeightNoLeading;
		}

		width = std::max(labelWidth, width);
	}

	fMinSize = {width, height};

	ResetLayoutInvalidation();

	return fMinSize;
}


// #pragma mark - accessibility


void
BSlider::GetAccessibleInfo(AccessibleInfo* info) const
{
	BControl::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	info->role = AccessibleRole::Slider;
	int32 minimum = 0;
	int32 maximum = 0;
	GetLimits(&minimum, &maximum);
	info->numericValue = Value();
	info->minimum = minimum;
	info->maximum = maximum;
	info->step = KeyIncrementValue();
	info->value = UpdateText();
	info->actions.Clear(AccessibleAction::Click);
	if (IsEnabled()) {
		info->actions |= AccessibleAction::Increment | AccessibleAction::Decrement
			| AccessibleAction::SetValue;
	}
}


std::expected<void, Error>
BSlider::PerformAccessibleAction(AccessibleAction action, const BMessage& data)
{
	if (action != AccessibleAction::Increment && action != AccessibleAction::Decrement
		&& action != AccessibleAction::SetValue) {
		return BView::PerformAccessibleAction(action, data);
	}
	if (!IsEnabled())
		return Failure(ErrorCode::NotSupported, "the slider is disabled");
	if (action == AccessibleAction::SetValue) {
		const auto number = data.FindDouble("number");
		if (!number.has_value())
			return Failure(ErrorCode::InvalidArgument, "SetValue needs a \"number\"");
		const double clamped = std::clamp(*number, -2147483648.0, 2147483647.0);
		SetValue(static_cast<int32>(std::lround(clamped)));
		(void)Invoke();
		return {};
	}
	// as the arrow keys do
	KeyDown(std::string(1, action == AccessibleAction::Increment ? keys::kRightArrow
		: keys::kLeftArrow));
	return {};
}


} // namespace ngui
