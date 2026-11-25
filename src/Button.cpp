/*
 * Copyright 2001-2015 Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Button.cpp (commit b1d497bb69).
 * NewGUI (see Button.h): the mouse is followed asynchronously; no pause
 * before invoking; no icons, archiving or scripting. Sizes are pixel
 * counts: the preferred size and the minimum height of the frame
 * constructor are Haiku's plus 1. Measured with TextSystem::Default() and
 * the plain font role (Haiku: the view's font).
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Mike Wilber
 *		Stefano Ceccherini (burton666@libero.it)
 *		Ivan Tonizza
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */


#include <ngui/Button.h>

#include <algorithm>
#include <cmath>

#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>


namespace ngui {


namespace {


enum {
	FLAG_DEFAULT 		= 0x01,
	FLAG_FLAT			= 0x02,
	FLAG_INSIDE			= 0x04,
	FLAG_WAS_PRESSED	= 0x08,
};


} // namespace


BButton::BButton(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BResizingMode resizingMode, BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode,
		flags | BViewFlag::WillDraw | BViewFlag::FullUpdateOnResize)
{
	// Resize to minimum height if needed
	const TextSystem& text = TextSystem::Default();
	const FontHeight fh = text.Height(FontRole::Plain);
	float minHeight = text.FontSize(FontRole::Plain) + std::ceil(fh.ascent + fh.descent);
	// NewGUI: a pixel count
	minHeight += 1;
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


BButton::BButton(std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BViewFlags flags)
	:
	BControl(name, label, std::move(message),
		flags | BViewFlag::WillDraw | BViewFlag::FullUpdateOnResize)
{
}


BButton::BButton(std::string_view label, std::unique_ptr<BMessage> message)
	:
	BControl("", label, std::move(message),
		BViewFlag::WillDraw | BViewFlag::Navigable | BViewFlag::FullUpdateOnResize)
{
}


BButton::~BButton()
{
}


void
BButton::Draw(DrawContext& context)
{
	gfx::Rect rect = Bounds();
	const gfx::Color background = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));

	// Behave like BeOS: the colors of the button do not depend on the current view colors.
	// This is relied upon by some code that overrides Draw, changes the colors before calling
	// the superclass draw, and don't expect that to have any effect. For example, this is seen
	// in LibLayout used in Wonderbrush.
	const gfx::Color text = context.palette.Get(BColorWhich::ControlText);
	const gfx::Color base = context.palette.Get(BColorWhich::ControlBackground);

	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);
	if (_Flag(FLAG_DEFAULT))
		flags |= ControlState::DefaultButton;
	if (_Flag(FLAG_FLAT) && !IsTracking())
		flags |= ControlState::Flat;
	if (_Flag(FLAG_INSIDE))
		flags |= ControlState::Hover;

	look.DrawButtonFrame(context, rect, base, background, flags);

	if (fBehavior == BBehavior::PopUp)
		look.DrawButtonWithPopUpBackground(context, rect, base, flags);
	else
		look.DrawButtonBackground(context, rect, base, flags);

	look.DrawLabel(context, Label(), 0, rect, base, flags,
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle}, &text);
}


void
BButton::MouseDown(gfx::Point where)
{
	if (!IsEnabled())
		return;

	if (fBehavior == BBehavior::PopUp && _PopUpRect().Contains(where)) {
		if (fPopUpMessage != nullptr)
			(void)BControl::Invoke(fPopUpMessage.get());
		return;
	}

	bool toggleBehavior = fBehavior == BBehavior::Toggle;

	if (toggleBehavior) {
		bool wasPressed = Value() == kControlOn;
		_SetFlag(FLAG_WAS_PRESSED, wasPressed);
		SetValue(wasPressed ? kControlOff : kControlOn);
		Invalidate();
	} else
		SetValue(kControlOn);

	// NewGUI: always as with B_ASYNCHRONOUS_CONTROLS
	SetTracking(true);
	SetMouseEventMask(BEventMask::Pointer);
}


void
BButton::AttachedToWindow()
{
	BControl::AttachedToWindow();

	if (IsDefault())
		Window()->SetDefaultButton(this);
}


void
BButton::KeyDown(std::string_view bytes)
{
	if (bytes.size() == 1 && (bytes[0] == keys::kEnter || bytes[0] == keys::kSpace)) {
		if (!IsEnabled())
			return;

		SetValue(kControlOn);

		// NewGUI: Haiku draws and waits 25 ms here; the window thread does
		// not wait, the pressed state is not drawn.
		(void)Invoke();
	} else
		BControl::KeyDown(bytes);
}


void
BButton::MakeDefault(bool flag)
{
	BButton* oldDefault = nullptr;
	BWindow* window = Window();
	if (window != nullptr)
		oldDefault = window->DefaultButton();

	if (flag) {
		if (_Flag(FLAG_DEFAULT) && oldDefault == this)
			return;

		if (_SetFlag(FLAG_DEFAULT, true)) {
			if (Flags().Has(BViewFlag::SupportsLayout))
				InvalidateLayout();
			else {
				ResizeBy(6.0f, 6.0f);
				MoveBy(-3.0f, -3.0f);
			}
		}

		if (window && oldDefault != this)
			window->SetDefaultButton(this);
	} else {
		if (!_SetFlag(FLAG_DEFAULT, false))
			return;

		if (Flags().Has(BViewFlag::SupportsLayout))
			InvalidateLayout();
		else {
			ResizeBy(-6.0f, -6.0f);
			MoveBy(3.0f, 3.0f);
		}

		if (window && oldDefault == this)
			window->SetDefaultButton(nullptr);
	}
}


void
BButton::SetLabel(std::string_view label)
{
	BControl::SetLabel(label);
}


bool
BButton::IsDefault() const
{
	return _Flag(FLAG_DEFAULT);
}


bool
BButton::IsFlat() const
{
	return _Flag(FLAG_FLAT);
}


void
BButton::SetFlat(bool flat)
{
	if (_SetFlag(FLAG_FLAT, flat))
		Invalidate();
}


BButton::BBehavior
BButton::Behavior() const
{
	return fBehavior;
}


void
BButton::SetBehavior(BBehavior behavior)
{
	if (behavior != fBehavior) {
		fBehavior = behavior;
		InvalidateLayout();
		Invalidate();
	}
}


BMessage*
BButton::PopUpMessage() const
{
	return fPopUpMessage.get();
}


void
BButton::SetPopUpMessage(std::unique_ptr<BMessage> message)
{
	fPopUpMessage = std::move(message);
}


void
BButton::MouseMoved(gfx::Point where, BMouseTransit transit, const BMessage* /*dragMessage*/)
{
	bool inside = (transit != BMouseTransit::Exited) && Bounds().Contains(where);
	if (_SetFlag(FLAG_INSIDE, inside))
		Invalidate();

	if (!IsTracking())
		return;

	if (fBehavior == BBehavior::Toggle) {
		bool pressed = inside ^ _Flag(FLAG_WAS_PRESSED);
		SetValue(pressed ? kControlOn : kControlOff);
	} else {
		if ((Value() == kControlOn) != inside)
			SetValue(inside ? kControlOn : kControlOff);
	}
}


void
BButton::MouseUp(gfx::Point where)
{
	if (!IsTracking())
		return;

	if (Bounds().Contains(where)) {
		if (fBehavior == BBehavior::Toggle)
			SetValue(_Flag(FLAG_WAS_PRESSED) ? kControlOff : kControlOn);

		(void)Invoke();
	} else if (_Flag(FLAG_FLAT))
		Invalidate();

	SetTracking(false);
}


void
BButton::SetValue(int32 value)
{
	if (value != Value())
		BControl::SetValue(value);
}


void
BButton::GetPreferredSize(float* width, float* height)
{
	_ValidatePreferredSize();

	if (width)
		*width = fPreferredSize.w;

	if (height)
		*height = fPreferredSize.h;
}


std::expected<void, Error>
BButton::Invoke(const BMessage* message)
{
	std::expected<void, Error> result = BControl::Invoke(message);

	if (fBehavior != BBehavior::Toggle)
		SetValue(kControlOff);

	return result;
}


gfx::Size
BButton::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


gfx::Size
BButton::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


gfx::Size
BButton::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


void
BButton::LayoutInvalidated(bool /*descendants*/)
{
	// invalidate cached preferred size
	fPreferredSize = {-1, -1};
}


gfx::Size
BButton::_ValidatePreferredSize()
{
	if (fPreferredSize.w < 0) {
		const ControlLook& look = Look();
		const TextSystem& text = TextSystem::Default();
		const BackgroundType backgroundType = fBehavior == BBehavior::PopUp
			? BackgroundType::ButtonWithPopUp : BackgroundType::Button;
		const Insets insets = look.FrameAndBackgroundInsets(text, FrameType::Button,
			backgroundType, IsDefault() ? ControlStateFlags(ControlState::DefaultButton)
				: ControlStateFlags());

		// width
		const float labelSpacing = look.DefaultLabelSpacing(text);
		float width = insets.left + insets.right + labelSpacing - 1;

		const bool hasLabel = !Label().empty();
		if (hasLabel) {
			width = std::max(width, std::ceil(labelSpacing * 3.3f));
			width += std::ceil(text.StringWidth(Label(), FontRole::Plain));
		}

		// height
		float minHorizontalMargins = insets.top + insets.bottom + labelSpacing;
		float height = -1;

		if (hasLabel) {
			const FontHeight fontHeight = text.Height(FontRole::Plain);
			float textHeight = fontHeight.ascent + fontHeight.descent;
			height = std::ceil(textHeight * 1.8f);
			float margins = height - std::ceil(textHeight);
			if (margins < minHorizontalMargins)
				height += minHorizontalMargins - margins;
		}

		// force some minimum width/height values
		width = std::max(width, hasLabel ? (labelSpacing * 12.5f) : labelSpacing);
		height = std::max(height, labelSpacing);

		// NewGUI: pixel counts
		fPreferredSize = {width + 1, height + 1};

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


gfx::Rect
BButton::_PopUpRect() const
{
	if (fBehavior != BBehavior::PopUp)
		return {0, 0, 0, 0};

	const Insets insets = Look().FrameAndBackgroundInsets(TextSystem::Default(),
		FrameType::Button, BackgroundType::ButtonWithPopUp,
		IsDefault() ? ControlStateFlags(ControlState::DefaultButton) : ControlStateFlags());

	// Haiku: rect.left = rect.right - right + 1 with inclusive rectangles,
	// the same pixels as x1 - right on half-open ones.
	gfx::Rect rect = Bounds();
	rect.x0 = rect.x1 - insets.right;
	return rect;
}


inline bool
BButton::_Flag(std::uint32_t flag) const
{
	return (fFlags & flag) != 0;
}


inline bool
BButton::_SetFlag(std::uint32_t flag, bool set)
{
	if (((fFlags & flag) != 0) == set)
		return false;

	if (set)
		fFlags |= flag;
	else
		fFlags &= ~flag;

	return true;
}


} // namespace ngui
