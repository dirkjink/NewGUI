/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ScrollBar.cpp (commit b1d497bb69),
 * without archiving, scripting and B_VALUE_CHANGED. NewGUI (see
 * ScrollBar.h):
 * - The geometry (thumb frame, buttons, values for points) is Haiku's,
 *   computed with inclusive rectangles (compat::BRect) and converted for
 *   the look and the public interface, so the same pixels are hit.
 * - The repeat of a held button is a BMessageRunner that sends kRepeat
 *   every 25 ms; the scroll bar starts repeating 300 ms after the press,
 *   as Haiku's repeater thread (snooze_until(), snooze(25000)).
 * - The modifiers and buttons come from the window's current message
 *   (Haiku: modifiers(), the "buttons" field).
 * - Sizes are pixel counts; ControlLook::ScrollBarWidth() is Haiku's
 *   GetScrollBarWidth(), one less.
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, burton666@libero.it
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Marc Flerackers, mflerackers@androme.be
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/ScrollBar.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

#include <ngui/AppDefs.h>
#include <ngui/Clock.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/MessageRunner.h>
#include <ngui/Messenger.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "compat/CompatView.h"


namespace ngui {


using compat::BPoint;
using compat::BRect;
using compat::FromBRect;
using namespace std::chrono_literals;


namespace {


#define SCROLL_BAR_MAXIMUM_KNOB_SIZE	50
#define SCROLL_BAR_MINIMUM_KNOB_SIZE	9

// Quick constants for determining which arrow is down and are defined with
// respect to double arrow mode. ARROW1 and ARROW4 refer to the outer pair of
// arrows and ARROW2 and ARROW3 refer to the inner ones. ARROW1 points left/up
// and ARROW4 points right/down.
constexpr int32 ARROW1 = 0;
constexpr int32 ARROW2 = 1;
constexpr int32 ARROW3 = 2;
constexpr int32 ARROW4 = 3;
constexpr int32 THUMB = 4;
constexpr int32 NOARROW = -1;

// B_V_SCROLL_BAR_WIDTH, B_H_SCROLL_BAR_HEIGHT (Haiku's sizes)
constexpr float kVScrollBarWidth = 14.0f;
constexpr float kHScrollBarHeight = 14.0f;

constexpr Clock::Time kRepeatDelay = 300ms;
constexpr Clock::Time kRepeatInterval = 25ms;

// NewGUI: the tick of the repeater.
constexpr std::uint32_t kRepeat = FourCharCode("_SBR");

// B_SECONDARY_MOUSE_BUTTON
constexpr std::int32_t kSecondaryMouseButton = 2;


std::mutex sInfoLock;
BScrollBarInfo sInfo;


BRect
ToBRect(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


BPoint
ToBPoint(gfx::Point point)
{
	return BPoint(point.x, point.y);
}


// The modifiers of the message the window is handling.
BModifierFlags
CurrentModifiers(const BView& view)
{
	const BMessage* message
		= view.Window() != nullptr ? view.Window()->CurrentMessage() : nullptr;
	return BModifierFlags::FromRaw(static_cast<std::uint32_t>(
		message != nullptr ? message->GetInt32("modifiers", 0) : 0));
}


} // namespace


BScrollBarInfo
GetScrollBarInfo()
{
	const std::lock_guard lock(sInfoLock);
	return sInfo;
}


std::expected<void, Error>
SetScrollBarInfo(const BScrollBarInfo& info)
{
	// The limits of Haiku's control_scrollbar().
	if (info.minKnobSize < SCROLL_BAR_MINIMUM_KNOB_SIZE
		|| info.minKnobSize > SCROLL_BAR_MAXIMUM_KNOB_SIZE) {
		return std::unexpected(Error{ErrorCode::InvalidArgument,
			"minKnobSize must be within 9 .. 50"});
	}
	const std::lock_guard lock(sInfoLock);
	sInfo = info;
	return {};
}


// Because the R5 version kept a lot of data on server-side, we need to kludge
// our way into binary compatibility
class BScrollBar::Private {
public:
	Private(BScrollBar* scrollBar)
	:
	fScrollBar(scrollBar),
	fEnabled(true),
	fScrollBarInfo(GetScrollBarInfo()),
	fThumbFrame(0.0, 0.0, -1.0, -1.0),
	fDoRepeat(false),
	fClickOffset(0.0, 0.0),
	fThumbInc(0.0),
	fStopValue(0.0),
	fUpArrowsEnabled(true),
	fDownArrowsEnabled(true),
	fBorderHighlighted(false),
	fButtonDown(NOARROW)
	{
		fScrollBarInfo.minKnobSize = (int32)((float)fScrollBarInfo.minKnobSize
			* (TextSystem::Default().FontSize(FontRole::Plain) / 12.0f));
	}

	BScrollBar*			fScrollBar;
	bool				fEnabled;

	BScrollBarInfo		fScrollBarInfo;

	// NewGUI: the repeater (Haiku: a thread)
	std::unique_ptr<BMessageRunner> fRepeater;
	Clock::Time			fRepeaterDelay{};

	BRect				fThumbFrame;
	bool				fDoRepeat;
	BPoint				fClickOffset;
	float				fThumbInc;
	float				fStopValue;

	bool				fUpArrowsEnabled;
	bool				fDownArrowsEnabled;

	bool				fBorderHighlighted;

	int8				fButtonDown;
};


//	#pragma mark - BScrollBar


BScrollBar::BScrollBar(gfx::Rect frame, std::string_view name, BView* target, float min,
	float max, BOrientation direction)
	:
	BView(frame, name, kFollowLeftTop, BViewFlag::WillDraw
		| BViewFlag::FullUpdateOnResize | BViewFlag::FrameEvents),
	fMin(min),
	fMax(max),
	fOrientation(direction)
{
	SetViewColor(std::nullopt);

	fPrivateData = std::make_unique<Private>(this);

	SetTarget(target);

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	SetResizingMode(direction == BOrientation::Vertical
		? BResizingMode{BHorizontalFollow::Right, BVerticalFollow::TopBottom}
		: BResizingMode{BHorizontalFollow::LeftRight, BVerticalFollow::Bottom});
}


BScrollBar::BScrollBar(std::string_view name, BView* target, float min, float max,
	BOrientation direction)
	:
	BView(name, BViewFlag::WillDraw | BViewFlag::FullUpdateOnResize | BViewFlag::FrameEvents),
	fMin(min),
	fMax(max),
	fOrientation(direction)
{
	SetViewColor(std::nullopt);

	fPrivateData = std::make_unique<Private>(this);

	SetTarget(target);

	_UpdateThumbFrame();
	_UpdateArrowButtons();
}


BScrollBar::~BScrollBar()
{
	SetTarget(static_cast<BView*>(nullptr));
}


void
BScrollBar::Draw(DrawContext& context)
{
	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);
	const gfx::Color text = context.palette.Get(BColorWhich::PanelText);
	const ControlLook& look = Look();

	ControlStateFlags flags;
	bool scrollingEnabled = fMin < fMax
		&& fProportion >= 0.0f && fProportion < 1.0f;
	if (scrollingEnabled)
		flags |= ControlState::PartiallyActivated;

	if (!fPrivateData->fEnabled || !scrollingEnabled)
		flags |= ControlState::Disabled;

	bool isFocused = fPrivateData->fBorderHighlighted;
	if (isFocused)
		flags |= ControlState::Focused;

	bool doubleArrows = _DoubleArrows();

	const auto buttonFlags = [&](int32 button) {
		return fPrivateData->fButtonDown == button ? flags | ControlState::Activated : flags;
	};

	// draw border
	BRect rect = ToBRect(Bounds());
	look.DrawScrollBarBorder(context, FromBRect(rect), base, flags, fOrientation);

	// inset past border
	rect.InsetBy(1, 1);

	// draw arrows buttons
	BRect thumbBG = rect;
	if (fOrientation == BOrientation::Horizontal) {
		BRect buttonFrame(rect.left, rect.top,
			rect.left + rect.Height(), rect.bottom);

		look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
			buttonFlags(ARROW1), ArrowDirection::Left, fOrientation,
			fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(rect.Height() + 1, 0.0f);
			look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
				buttonFlags(ARROW2), ArrowDirection::Right, fOrientation,
				fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(rect.right - ((rect.Height() * 2) + 1),
				rect.top);
			look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
				buttonFlags(ARROW3), ArrowDirection::Left, fOrientation,
				fPrivateData->fButtonDown == ARROW3);

			thumbBG.left += rect.Height() * 2 + 2;
			thumbBG.right -= rect.Height() * 2 + 2;
		} else {
			thumbBG.left += rect.Height() + 1;
			thumbBG.right -= rect.Height() + 1;
		}

		buttonFrame.OffsetTo(rect.right - rect.Height(), rect.top);
		look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
			buttonFlags(ARROW4), ArrowDirection::Right, fOrientation,
			fPrivateData->fButtonDown == ARROW4);
	} else {
		BRect buttonFrame(rect.left, rect.top, rect.right,
			rect.top + rect.Width());

		look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
			buttonFlags(ARROW1), ArrowDirection::Up, fOrientation,
			fPrivateData->fButtonDown == ARROW1);

		if (doubleArrows) {
			buttonFrame.OffsetBy(0, rect.Width() + 1);
			look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
				buttonFlags(ARROW2), ArrowDirection::Down, fOrientation,
				fPrivateData->fButtonDown == ARROW2);

			buttonFrame.OffsetTo(rect.left, rect.bottom
				- ((rect.Width() * 2) + 1));
			look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
				buttonFlags(ARROW3), ArrowDirection::Up, fOrientation,
				fPrivateData->fButtonDown == ARROW3);

			thumbBG.top += rect.Width() * 2 + 2;
			thumbBG.bottom -= rect.Width() * 2 + 2;
		} else {
			thumbBG.top += rect.Width() + 1;
			thumbBG.bottom -= rect.Width() + 1;
		}

		buttonFrame.OffsetTo(rect.left, rect.bottom - rect.Width());
		look.DrawScrollBarButton(context, FromBRect(buttonFrame), base, text,
			buttonFlags(ARROW4), ArrowDirection::Down, fOrientation,
			fPrivateData->fButtonDown == ARROW4);
	}

	// fill background besides the thumb
	rect = fPrivateData->fThumbFrame;
	if (fOrientation == BOrientation::Horizontal) {
		gfx::Rect leftOfThumb = FromBRect(BRect(thumbBG.left, thumbBG.top,
			rect.left - 1, thumbBG.bottom));
		gfx::Rect rightOfThumb = FromBRect(BRect(rect.right + 1, thumbBG.top,
			thumbBG.right, thumbBG.bottom));
		look.DrawScrollBarBackground(context, leftOfThumb, rightOfThumb, base, flags,
			fOrientation);
	} else {
		gfx::Rect topOfThumb = FromBRect(BRect(thumbBG.left, thumbBG.top,
			thumbBG.right, rect.top - 1));
		gfx::Rect bottomOfThumb = FromBRect(BRect(thumbBG.left, rect.bottom + 1,
			thumbBG.right, thumbBG.bottom));
		look.DrawScrollBarBackground(context, topOfThumb, bottomOfThumb, base, flags,
			fOrientation);
	}

	// draw thumb
	gfx::Rect thumb = FromBRect(fPrivateData->fThumbFrame);
	look.DrawScrollBarThumb(context, thumb,
		context.palette.Get(BColorWhich::ScrollBarThumb), flags, fOrientation,
		fPrivateData->fScrollBarInfo.knob);
}


void
BScrollBar::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	_UpdateThumbFrame();
}


void
BScrollBar::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kRepeat:
			_Repeat();
			break;

		case kMouseWheelChanged:
		{
			// Must handle this here since BView checks for the existence of
			// scrollbars, which a scrollbar itself does not have
			float deltaX = message->GetFloat("be:wheel_delta_x", 0.0f);
			float deltaY = message->GetFloat("be:wheel_delta_y", 0.0f);

			if (deltaX == 0.0f && deltaY == 0.0f)
				break;

			if (deltaX != 0.0f && deltaY == 0.0f)
				deltaY = deltaX;

			(void)ScrollWithMouseWheelDelta(this, deltaY);
			// NewGUI: done; Haiku fell through to BView, which passed the
			// wheel on to the parents as well.
			break;
		}

		default:
			BView::MessageReceived(message);
	}
}


void
BScrollBar::MouseDown(gfx::Point point)
{
	if (!fPrivateData->fEnabled || fMin == fMax)
		return;

	SetMouseEventMask(BEventMask::Pointer);

	const BMessage* current = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	const int32 buttons = current != nullptr ? current->GetInt32("buttons", 1) : 1;

	const BPoint where = ToBPoint(point);
	if ((buttons & kSecondaryMouseButton) != 0) {
		// special absolute scrolling: move thumb to where we clicked
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset
			= fPrivateData->fThumbFrame.LeftTop() - where;
		if (Orientation() == BOrientation::Horizontal) {
			fPrivateData->fClickOffset.x
				= -fPrivateData->fThumbFrame.Width() / 2;
		} else {
			fPrivateData->fClickOffset.y
				= -fPrivateData->fThumbFrame.Height() / 2;
		}

		const BPoint target = where + fPrivateData->fClickOffset;
		SetValue(_ValueFor({target.x, target.y}));
		return;
	}

	// hit test for the thumb
	if (fPrivateData->fThumbFrame.Contains(where)) {
		fPrivateData->fButtonDown = THUMB;
		fPrivateData->fClickOffset
			= fPrivateData->fThumbFrame.LeftTop() - where;
		Invalidate();
		return;
	}

	// hit test for arrows or empty area
	float scrollValue = 0.0;

	// pressing the shift key scrolls faster
	float buttonStepSize
		= CurrentModifiers(*this).Has(BModifier::Shift) ? fLargeStep : fSmallStep;

	fPrivateData->fButtonDown = static_cast<int8>(_ButtonFor(point));
	switch (fPrivateData->fButtonDown) {
		case ARROW1:
			scrollValue = -buttonStepSize;
			break;

		case ARROW2:
			scrollValue = buttonStepSize;
			break;

		case ARROW3:
			scrollValue = -buttonStepSize;
			break;

		case ARROW4:
			scrollValue = buttonStepSize;
			break;

		case NOARROW:
			// we hit the empty area, figure out which side of the thumb
			if (fOrientation == BOrientation::Vertical) {
				if (where.y < fPrivateData->fThumbFrame.top)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			} else {
				if (where.x < fPrivateData->fThumbFrame.left)
					scrollValue = -fLargeStep;
				else
					scrollValue = fLargeStep;
			}
			_UpdateTargetValue(point);
			break;
	}

	if (scrollValue != 0.0f) {
		SetValue(fValue + scrollValue);
		Invalidate();

		// launch the repeat thread
		// NewGUI: a message runner on the window's looper
		fPrivateData->fRepeaterDelay = Clock::Default().Now() + kRepeatDelay;
		fPrivateData->fThumbInc = scrollValue;
		fPrivateData->fDoRepeat = true;
		if (fPrivateData->fRepeater == nullptr && Window() != nullptr) {
			fPrivateData->fRepeater = std::make_unique<BMessageRunner>(
				BMessenger(this), BMessage(kRepeat), kRepeatInterval);
		}
	}
}


void
BScrollBar::MouseMoved(gfx::Point point, BMouseTransit /*transit*/,
	const BMessage* /*dragMessage*/)
{
	if (!fPrivateData->fEnabled || fMin >= fMax || fProportion >= 1.0f
		|| fProportion < 0.0f) {
		return;
	}

	if (fPrivateData->fButtonDown != NOARROW) {
		if (fPrivateData->fButtonDown == THUMB) {
			const BPoint target = ToBPoint(point) + fPrivateData->fClickOffset;
			SetValue(_ValueFor({target.x, target.y}));
		} else {
			// suspend the repeating if the mouse is not over the button
			bool repeat = _ButtonRectFor(fPrivateData->fButtonDown).Contains(point);
			if (fPrivateData->fDoRepeat != repeat) {
				fPrivateData->fDoRepeat = repeat;
				Invalidate();
			}
		}
	} else {
		// update the value at which we want to stop repeating
		if (fPrivateData->fDoRepeat) {
			_UpdateTargetValue(point);
			// we might have to turn arround
			if ((fValue < fPrivateData->fStopValue
					&& fPrivateData->fThumbInc < 0)
				|| (fValue > fPrivateData->fStopValue
					&& fPrivateData->fThumbInc > 0)) {
				fPrivateData->fThumbInc = -fPrivateData->fThumbInc;
			}
		}
	}
}


void
BScrollBar::MouseUp(gfx::Point /*where*/)
{
	Invalidate();

	fPrivateData->fButtonDown = NOARROW;
	// NewGUI: the repeater stops at once
	fPrivateData->fRepeater.reset();
	fPrivateData->fDoRepeat = false;
}


void
BScrollBar::WindowActivated(bool active)
{
	fPrivateData->fEnabled = active;
	Invalidate();
}


void
BScrollBar::SetValue(float value)
{
	if (value > fMax)
		value = fMax;
	else if (value < fMin)
		value = fMin;
	else if (std::isnan(value) || std::isinf(value))
		return;

	value = std::round(value);

	if (value == fValue)
		return;

	fValue = value;

	_UpdateThumbFrame();
	_UpdateArrowButtons();

	ValueChanged(fValue);
}


float
BScrollBar::Value() const
{
	return fValue;
}


void
BScrollBar::ValueChanged(float newValue)
{
	if (fTarget != nullptr) {
		// cache target bounds
		gfx::Rect targetBounds = fTarget->Bounds();
		// if vertical, check bounds top and scroll if different from newValue
		if (fOrientation == BOrientation::Vertical && targetBounds.y0 != newValue)
			fTarget->ScrollBy(0.0, newValue - targetBounds.y0);

		// if horizontal, check bounds left and scroll if different from newValue
		if (fOrientation == BOrientation::Horizontal && targetBounds.x0 != newValue)
			fTarget->ScrollBy(newValue - targetBounds.x0, 0.0);
	}

	SetValue(newValue);
}


void
BScrollBar::SetProportion(float value)
{
	if (value < 0.0f)
		value = 0.0f;
	else if (value > 1.0f)
		value = 1.0f;

	if (value == fProportion)
		return;

	bool oldEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	fProportion = value;

	bool newEnabled = fPrivateData->fEnabled && fMin < fMax
		&& fProportion < 1.0f && fProportion >= 0.0f;

	_UpdateThumbFrame();

	if (oldEnabled != newEnabled)
		Invalidate();
}


float
BScrollBar::Proportion() const
{
	return fProportion;
}


void
BScrollBar::SetRange(float min, float max)
{
	if (min > max || std::isnan(min) || std::isnan(max)
		|| std::isinf(min) || std::isinf(max)) {
		min = 0.0f;
		max = 0.0f;
	}

	min = std::round(min);
	max = std::round(max);

	if (fMin == min && fMax == max)
		return;

	fMin = min;
	fMax = max;

	if (fValue < fMin || fValue > fMax)
		SetValue(fValue);
	else
		_UpdateThumbFrame();
}


void
BScrollBar::GetRange(float* min, float* max) const
{
	if (min != nullptr)
		*min = fMin;

	if (max != nullptr)
		*max = fMax;
}


void
BScrollBar::SetSteps(float smallStep, float largeStep)
{
	// Under R5, steps can be set only after being attached to a window,
	// probably because the data is kept server-side. We'll just remove
	// that limitation... :P

	// The BeBook also says that we need to specify an integer value even
	// though the step values are floats. For the moment, we'll just make
	// sure that they are integers
	smallStep = std::round(smallStep);
	largeStep = std::round(largeStep);
	if (fSmallStep == smallStep && fLargeStep == largeStep)
		return;

	fSmallStep = smallStep;
	fLargeStep = largeStep;

	if (fProportion == 0.0f) {
		// special case, proportion is based on fLargeStep if it was never
		// set, so it means we need to invalidate here
		_UpdateThumbFrame();
		Invalidate();
	}

	// TODO: test use of fractional values and make them work properly if
	// they don't
}


void
BScrollBar::GetSteps(float* smallStep, float* largeStep) const
{
	if (smallStep != nullptr)
		*smallStep = fSmallStep;

	if (largeStep != nullptr)
		*largeStep = fLargeStep;
}


void
BScrollBar::SetTarget(BView* target)
{
	if (fTarget) {
		// unset the previous target's scrollbar pointer
		if (fOrientation == BOrientation::Vertical)
			fTarget->fVerScroller = nullptr;
		else
			fTarget->fHorScroller = nullptr;
	}

	fTarget = target;
	if (fTarget) {
		if (fOrientation == BOrientation::Vertical)
			fTarget->fVerScroller = this;
		else
			fTarget->fHorScroller = this;
	}
}


void
BScrollBar::SetTarget(std::string_view targetName)
{
	// NOTE 1: BeOS implementation crashes for targetName == NULL
	// NOTE 2: BeOS implementation also does not modify the target
	// if it can't be found
	// NewGUI: without a window nothing happens (Haiku: debugger()).
	if (Window() == nullptr)
		return;

	BView* target = Window()->FindView(targetName);
	if (target != nullptr)
		SetTarget(target);
}


BView*
BScrollBar::Target() const
{
	return fTarget;
}


void
BScrollBar::SetOrientation(BOrientation direction)
{
	if (fOrientation == direction)
		return;

	fOrientation = direction;
	InvalidateLayout();
	Invalidate();
}


BOrientation
BScrollBar::Orientation() const
{
	return fOrientation;
}


void
BScrollBar::SetBorderHighlighted(bool highlight)
{
	if (fPrivateData->fBorderHighlighted == highlight)
		return;

	fPrivateData->fBorderHighlighted = highlight;
	Invalidate();
}


void
BScrollBar::GetPreferredSize(float* _width, float* _height)
{
	// NewGUI: pixel counts; ScrollBarWidth() is Haiku's size, one less.
	const ControlLook& look = Look();
	const TextSystem& text = TextSystem::Default();
	if (fOrientation == BOrientation::Vertical) {
		if (_width)
			*_width = look.ScrollBarWidth(text, BOrientation::Vertical) + 1;
		if (_height)
			*_height = _MinSize().h;
	} else if (fOrientation == BOrientation::Horizontal) {
		if (_width)
			*_width = _MinSize().w;
		if (_height)
			*_height = look.ScrollBarWidth(text, BOrientation::Horizontal) + 1;
	}
}


gfx::Size
BScrollBar::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), _MinSize());
}


gfx::Size
BScrollBar::MaxSize()
{
	gfx::Size maxSize;
	GetPreferredSize(&maxSize.w, &maxSize.h);
	if (fOrientation == BOrientation::Horizontal)
		maxSize.w = kSizeUnlimited;
	else
		maxSize.h = kSizeUnlimited;
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), maxSize);
}


gfx::Size
BScrollBar::PreferredSize()
{
	gfx::Size preferredSize;
	GetPreferredSize(&preferredSize.w, &preferredSize.h);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), preferredSize);
}


// #pragma mark - private


bool
BScrollBar::_DoubleArrows() const
{
	if (!fPrivateData->fScrollBarInfo.doubleArrows)
		return false;

	// if there is not enough room, switch to single arrows even though
	// double arrows is specified
	const BRect bounds = ToBRect(Bounds());
	const float minKnobSize = (float)fPrivateData->fScrollBarInfo.minKnobSize;
	if (fOrientation == BOrientation::Horizontal) {
		return bounds.Width() > (bounds.Height() + 1) * 4
			+ minKnobSize * 2;
	} else {
		return bounds.Height() > (bounds.Width() + 1) * 4
			+ minKnobSize * 2;
	}
}


void
BScrollBar::_UpdateThumbFrame()
{
	BRect bounds = ToBRect(Bounds());
	bounds.InsetBy(1.0, 1.0);

	BRect oldFrame = fPrivateData->fThumbFrame;
	fPrivateData->fThumbFrame = bounds;
	float minSize = (float)fPrivateData->fScrollBarInfo.minKnobSize;
	float maxSize;
	float buttonSize;

	// assume square buttons
	if (fOrientation == BOrientation::Vertical) {
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0f;
	} else {
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0f;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;

	float thumbSize;
	if (fPrivateData->fScrollBarInfo.proportional) {
		float proportion = fProportion;
		if (fMin >= fMax || proportion > 1.0f || proportion < 0.0f)
			proportion = 1.0f;

		if (proportion == 0.0f) {
			// Special case a proportion of 0.0, use the large step value
			// in that case (NOTE: fMin == fMax already handled above)
			// This calculation is based on the assumption that "large step"
			// scrolls by one "page size".
			proportion = fLargeStep / (2 * (fMax - fMin));
			if (proportion > 1.0f)
				proportion = 1.0f;
		}
		thumbSize = maxSize * proportion;
		if (thumbSize < minSize)
			thumbSize = minSize;
	} else
		thumbSize = minSize;

	thumbSize = std::floor(thumbSize + 0.5f);
	thumbSize--;

	// the thumb can be scrolled within the remaining area "maxSize - thumbSize - 1.0"
	float offset = 0.0f;
	if (fMax > fMin) {
		offset = std::floor(((fValue - fMin) / (fMax - fMin))
			* (maxSize - thumbSize - 1.0f));
	}

	if (_DoubleArrows()) {
		offset += buttonSize * 2;
	} else
		offset += buttonSize;

	// visual adjustments (room for darker line between thumb and buttons)
	offset++;

	if (fOrientation == BOrientation::Vertical) {
		fPrivateData->fThumbFrame.bottom = fPrivateData->fThumbFrame.top
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(0.0, offset);
	} else {
		fPrivateData->fThumbFrame.right = fPrivateData->fThumbFrame.left
			+ thumbSize;
		fPrivateData->fThumbFrame.OffsetBy(offset, 0.0);
	}

	// NewGUI: the whole view is recorded anew (11.4).
	if (fPrivateData->fThumbFrame != oldFrame)
		Invalidate();
}


float
BScrollBar::_ValueFor(gfx::Point where) const
{
	BRect bounds = ToBRect(Bounds());
	bounds.InsetBy(1.0f, 1.0f);

	float offset;
	float thumbSize;
	float maxSize;
	float buttonSize;

	if (fOrientation == BOrientation::Vertical) {
		offset = where.y;
		thumbSize = fPrivateData->fThumbFrame.Height();
		maxSize = bounds.Height();
		buttonSize = bounds.Width() + 1.0f;
	} else {
		offset = where.x;
		thumbSize = fPrivateData->fThumbFrame.Width();
		maxSize = bounds.Width();
		buttonSize = bounds.Height() + 1.0f;
	}

	if (_DoubleArrows()) {
		// subtract the size of four buttons
		maxSize -= buttonSize * 4;
		// convert point to inside of area between buttons
		offset -= buttonSize * 2;
	} else {
		// subtract the size of two buttons
		maxSize -= buttonSize * 2;
		// convert point to inside of area between buttons
		offset -= buttonSize;
	}
	// visual adjustments (room for darker line between thumb and buttons)
	maxSize--;
	offset++;

	return std::round(fMin + (offset / (maxSize - thumbSize)
		* (fMax - fMin + 1.0f)));
}


int32
BScrollBar::_ButtonFor(gfx::Point point) const
{
	const BPoint where = ToBPoint(point);
	BRect bounds = ToBRect(Bounds());
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == BOrientation::Vertical
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize, bounds.top + buttonSize);

	if (fOrientation == BOrientation::Vertical) {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(0.0, buttonSize);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.left, bounds.bottom - buttonSize);
		if (rect.Contains(where))
			return ARROW4;
	} else {
		if (rect.Contains(where))
			return ARROW1;

		if (_DoubleArrows()) {
			rect.OffsetBy(buttonSize, 0.0);
			if (rect.Contains(where))
				return ARROW2;

			rect.OffsetTo(bounds.right - 2 * buttonSize, bounds.top);
			if (rect.Contains(where))
				return ARROW3;
		}
		rect.OffsetTo(bounds.right - buttonSize, bounds.top);
		if (rect.Contains(where))
			return ARROW4;
	}

	return NOARROW;
}


gfx::Rect
BScrollBar::_ButtonRectFor(int32 button) const
{
	BRect bounds = ToBRect(Bounds());
	bounds.InsetBy(1.0f, 1.0f);

	float buttonSize = fOrientation == BOrientation::Vertical
		? bounds.Width() + 1.0f
		: bounds.Height() + 1.0f;

	BRect rect(bounds.left, bounds.top,
		bounds.left + buttonSize - 1.0f, bounds.top + buttonSize - 1.0f);

	if (fOrientation == BOrientation::Vertical) {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(0.0, buttonSize);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.left, bounds.bottom - 2 * buttonSize + 1);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.left, bounds.bottom - buttonSize + 1);
				break;
		}
	} else {
		switch (button) {
			case ARROW1:
				break;

			case ARROW2:
				rect.OffsetBy(buttonSize, 0.0);
				break;

			case ARROW3:
				rect.OffsetTo(bounds.right - 2 * buttonSize + 1, bounds.top);
				break;

			case ARROW4:
				rect.OffsetTo(bounds.right - buttonSize + 1, bounds.top);
				break;
		}
	}

	return FromBRect(rect);
}


void
BScrollBar::_UpdateTargetValue(gfx::Point where)
{
	if (fOrientation == BOrientation::Vertical) {
		fPrivateData->fStopValue = _ValueFor({where.x, where.y
			- fPrivateData->fThumbFrame.Height() / 2.0f});
	} else {
		fPrivateData->fStopValue = _ValueFor({where.x
			- fPrivateData->fThumbFrame.Width() / 2.0f, where.y});
	}
}


void
BScrollBar::_UpdateArrowButtons()
{
	bool upEnabled = fValue > fMin;
	if (fPrivateData->fUpArrowsEnabled != upEnabled) {
		fPrivateData->fUpArrowsEnabled = upEnabled;
		Invalidate();
	}

	bool downEnabled = fValue < fMax;
	if (fPrivateData->fDownArrowsEnabled != downEnabled) {
		fPrivateData->fDownArrowsEnabled = downEnabled;
		Invalidate();
	}
}


void
BScrollBar::_Repeat()
{
	// One round of Haiku's ButtonRepeaterThread(): wait for the delay after
	// the press, then step while the button is held over it.
	if (fPrivateData->fRepeater == nullptr
		|| Clock::Default().Now() < fPrivateData->fRepeaterDelay) {
		return;
	}

	if (fPrivateData->fDoRepeat) {
		float value = Value() + fPrivateData->fThumbInc;
		if (fPrivateData->fButtonDown == NOARROW) {
			// in this case we want to stop when we're under the mouse
			if (fPrivateData->fThumbInc > 0.0f && value <= fPrivateData->fStopValue)
				SetValue(value);
			if (fPrivateData->fThumbInc < 0.0f && value >= fPrivateData->fStopValue)
				SetValue(value);
		} else
			SetValue(value);
	}
}


gfx::Size
BScrollBar::_MinSize() const
{
	// NewGUI: pixel counts
	gfx::Size minSize;
	const float minKnobSize = (float)fPrivateData->fScrollBarInfo.minKnobSize;
	if (fOrientation == BOrientation::Horizontal) {
		minSize.w = 2 * kVScrollBarWidth + 2 * minKnobSize + 1;
		minSize.h = kHScrollBarHeight + 1;
	} else {
		minSize.w = kVScrollBarWidth + 1;
		minSize.h = 2 * kHScrollBarHeight + 2 * minKnobSize + 1;
	}

	return minSize;
}


} // namespace ngui
