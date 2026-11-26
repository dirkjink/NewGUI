/*
 * Copyright 2001-2008, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/RadioButton.cpp (commit b1d497bb69).
 * NewGUI (see RadioButton.h): asynchronous tracking only; vertically
 * centered in a layout; no icons, archiving, scripting, or BBox cases yet.
 * Sizes are pixel counts (Haiku's plus 1); the knob frame is Haiku's
 * inclusive one as a half-open rectangle, on the same pixels.
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include <ngui/RadioButton.h>

#include <algorithm>
#include <cmath>

#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>


namespace ngui {


BRadioButton::BRadioButton(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BResizingMode resizingMode, BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode,
		flags | BViewFlag::FrameEvents)
{
	// Resize to minimum height if needed for BeOS compatibility
	float minHeight;
	GetPreferredSize(nullptr, &minHeight);
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


BRadioButton::BRadioButton(std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BViewFlags flags)
	:
	BControl(name, label, std::move(message), flags | BViewFlag::FrameEvents)
{
}


BRadioButton::BRadioButton(std::string_view label, std::unique_ptr<BMessage> message)
	:
	BControl("", label, std::move(message),
		BViewFlag::WillDraw | BViewFlag::Navigable | BViewFlag::FrameEvents)
{
}


BRadioButton::~BRadioButton()
{
}


void
BRadioButton::Draw(DrawContext& context)
{
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));

	// its size depends on the text height
	const FontHeight fontHeight = context.text.Height(FontRole::Plain);

	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);
	if (fOutlined)
		flags |= ControlState::Clicked;

	gfx::Rect knobRect(_KnobFrame(fontHeight));
	gfx::Rect rect(knobRect);
	look.DrawRadioButton(context, rect, base, flags);

	// Haiku: knobRect.right + 1 + spacing, with an inclusive right
	gfx::Rect labelRect = Bounds();
	labelRect.x0 = knobRect.x1 + look.DefaultLabelSpacing(context.text);

	const BAlignment alignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle};
	look.DrawLabel(context, Label(), 0, labelRect, base, flags, alignment);
}


void
BRadioButton::MouseDown(gfx::Point /*where*/)
{
	if (!IsEnabled())
		return;

	fOutlined = true;

	// NewGUI: always as with B_ASYNCHRONOUS_CONTROLS
	Invalidate();
	SetTracking(true);
	SetMouseEventMask(BEventMask::Pointer);
}


void
BRadioButton::KeyDown(std::string_view bytes)
{
	// TODO: Add selecting the next button functionality (navigating radio
	// buttons with the cursor keys)!
	if (bytes.size() == 1 && (bytes[0] == keys::kEnter || bytes[0] == keys::kSpace)) {
		// override B_RETURN, which BControl would use to toggle the value
		// but we don't allow setting the control to "off", only "on"
		if (IsEnabled() && !Value()) {
			SetValue(kControlOn);
			(void)Invoke();
		}
	} else
		BControl::KeyDown(bytes);
}


void
BRadioButton::SetValue(int32 value)
{
	if (value != Value()) {
		BControl::SetValueNoUpdate(value);
		Invalidate();
	}

	if (value == 0)
		return;

	// turn off the other radio buttons among the siblings
	// (NewGUI: the BBox cases come with BBox)
	BView* parent = Parent();
	BView* child = nullptr;

	if (parent != nullptr)
		child = parent->ChildAt(0);
	else if (Window() != nullptr)
		child = Window()->ChildAt(0);

	while (child != nullptr) {
		BRadioButton* radio = dynamic_cast<BRadioButton*>(child);
		if (radio != nullptr && (radio != this))
			radio->SetValue(kControlOff);

		child = child->NextSibling();
	}
}


void
BRadioButton::GetPreferredSize(float* _width, float* _height)
{
	const TextSystem& text = TextSystem::Default();
	const FontHeight fontHeight = text.Height(FontRole::Plain);

	// Haiku: rect.right + rect.left and rect.bottom + rect.top of its
	// inclusive knob frame
	gfx::Rect rect(_KnobFrame(fontHeight));
	float width = (rect.x1 - 1) + rect.x0;
	float height = (rect.y1 - 1) + rect.y0;

	if (!Label().empty()) {
		width += Look().DefaultLabelSpacing(text)
			+ std::ceil(text.StringWidth(Label(), FontRole::Plain));
		height = std::max(height,
			std::ceil(6.0f + fontHeight.ascent + fontHeight.descent));
	}

	// NewGUI: pixel counts
	if (_width != nullptr)
		*_width = width + 1;

	if (_height != nullptr)
		*_height = height + 1;
}


void
BRadioButton::MouseUp(gfx::Point where)
{
	if (!IsTracking())
		return;

	fOutlined = Bounds().Contains(where);
	if (fOutlined) {
		fOutlined = false;
		if (Value() != kControlOn) {
			SetValue(kControlOn);
			(void)Invoke();
		}
	}
	Invalidate();

	SetTracking(false);
}


void
BRadioButton::MouseMoved(gfx::Point where, BMouseTransit /*transit*/,
	const BMessage* /*dragMessage*/)
{
	if (!IsTracking())
		return;

	bool inside = Bounds().Contains(where);

	if (fOutlined != inside) {
		fOutlined = inside;
		Invalidate();
	}
}


void
BRadioButton::FrameResized(float newWidth, float newHeight)
{
	Invalidate();
	BControl::FrameResized(newWidth, newHeight);
}


gfx::Size
BRadioButton::MaxSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), gfx::Size{width, height});
}


BAlignment
BRadioButton::LayoutAlignment()
{
	// NewGUI: vertically centered (Haiku: unset, see RadioButton.h)
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle});
}


gfx::Rect
BRadioButton::_KnobFrame() const
{
	return _KnobFrame(TextSystem::Default().Height(FontRole::Plain));
}


gfx::Rect
BRadioButton::_KnobFrame(const FontHeight& fontHeight) const
{
	// Same as BCheckBox: Haiku's BRect(0, 2, ceilf(3 + ascent),
	// ceilf(5 + ascent)), inclusive
	return {0.0f, 2.0f, std::ceil(3.0f + fontHeight.ascent) + 1,
		std::ceil(5.0f + fontHeight.ascent) + 1};
}


// #pragma mark - accessibility


void
BRadioButton::GetAccessibleInfo(AccessibleInfo* info) const
{
	BControl::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	info->role = AccessibleRole::RadioButton;
	if (Value() == kControlOn)
		info->states |= AccessibleState::Checked;
}


} // namespace ngui
