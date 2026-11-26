/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/CheckBox.cpp (commit b1d497bb69).
 * NewGUI (see CheckBox.h): asynchronous tracking only, no icons, archiving
 * or scripting. Sizes are pixel counts (Haiku's plus 1); the box frame is
 * Haiku's inclusive one as a half-open rectangle, on the same pixels.
 * Measured with TextSystem::Default() and the plain font role.
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stephan Aßmus <superstippi@gmx.de>
 */


#include <ngui/CheckBox.h>

#include <algorithm>
#include <cmath>

#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/TextSystem.h>


namespace ngui {


BCheckBox::BCheckBox(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BResizingMode resizingMode, BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode, flags)
{
	// Resize to minimum height if needed
	const FontHeight fontHeight = TextSystem::Default().Height(FontRole::Plain);
	float minHeight = std::ceil(6.0f + fontHeight.ascent + fontHeight.descent);
	// NewGUI: a pixel count
	minHeight += 1;
	if (Bounds().Height() < minHeight)
		ResizeTo(Bounds().Width(), minHeight);
}


BCheckBox::BCheckBox(std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BViewFlags flags)
	:
	BControl(name, label, std::move(message),
		flags | BViewFlag::WillDraw | BViewFlag::Navigable)
{
}


BCheckBox::BCheckBox(std::string_view label, std::unique_ptr<BMessage> message)
	:
	BControl("", label, std::move(message), BViewFlag::WillDraw | BViewFlag::Navigable)
{
}


BCheckBox::~BCheckBox()
{
}


void
BCheckBox::Draw(DrawContext& context)
{
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));

	const ControlLook& look = Look();
	ControlStateFlags flags = look.Flags(*this);
	if (fOutlined)
		flags |= ControlState::Clicked;

	gfx::Rect checkBoxRect(_CheckBoxFrame());
	gfx::Rect rect(checkBoxRect);
	look.DrawCheckBox(context, rect, base, flags);

	// Haiku: checkBoxRect.right + 1 + spacing, with an inclusive right
	gfx::Rect labelRect = Bounds();
	labelRect.x0 = checkBoxRect.x1 + look.DefaultLabelSpacing(context.text);

	const BAlignment alignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle};
	look.DrawLabel(context, Label(), 0, labelRect, base, flags, alignment);
}


void
BCheckBox::KeyDown(std::string_view bytes)
{
	if (bytes.size() == 1 && (bytes[0] == keys::kEnter || bytes[0] == keys::kSpace)) {
		if (!IsEnabled())
			return;

		SetValue(_NextState());
		(void)Invoke();
	} else {
		// skip the BControl implementation
		BView::KeyDown(bytes);
	}
}


void
BCheckBox::MouseDown(gfx::Point /*where*/)
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
BCheckBox::MouseUp(gfx::Point where)
{
	if (!IsTracking())
		return;

	bool inside = Bounds().Contains(where);

	if (fOutlined != inside) {
		fOutlined = inside;
		Invalidate();
	}

	if (fOutlined) {
		fOutlined = false;
		SetValue(_NextState());
		(void)Invoke();
	} else {
		Invalidate();
	}

	SetTracking(false);
}


void
BCheckBox::MouseMoved(gfx::Point where, BMouseTransit /*transit*/,
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
BCheckBox::GetPreferredSize(float* width, float* height)
{
	_ValidatePreferredSize();

	if (width)
		*width = fPreferredSize.w;

	if (height)
		*height = fPreferredSize.h;
}


gfx::Size
BCheckBox::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


gfx::Size
BCheckBox::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


gfx::Size
BCheckBox::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


BAlignment
BCheckBox::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle});
}


void
BCheckBox::SetValue(int32 value)
{
	// We only accept three possible values.
	switch (value) {
		case kControlOff:
		case kControlOn:
		case kControlPartiallyOn:
			break;
		default:
			value = kControlOn;
			break;
	}

	if (value != Value()) {
		BControl::SetValueNoUpdate(value);
		Invalidate();
	}
}


bool
BCheckBox::IsPartialStateToOff() const
{
	return fPartialToOff;
}


void
BCheckBox::SetPartialStateToOff(bool partialToOff)
{
	fPartialToOff = partialToOff;
}


void
BCheckBox::LayoutInvalidated(bool /*descendants*/)
{
	// invalidate cached preferred size
	fPreferredSize = kUnsetSize;
}


gfx::Rect
BCheckBox::_CheckBoxFrame(const FontHeight& fontHeight) const
{
	// Haiku's BRect(0, 2, ceilf(3 + ascent), ceilf(5 + ascent)), inclusive
	return {0.0f, 2.0f, std::ceil(3.0f + fontHeight.ascent) + 1,
		std::ceil(5.0f + fontHeight.ascent) + 1};
}


gfx::Rect
BCheckBox::_CheckBoxFrame() const
{
	return _CheckBoxFrame(TextSystem::Default().Height(FontRole::Plain));
}


gfx::Size
BCheckBox::_ValidatePreferredSize()
{
	if (!BLayoutUtils::IsSet(fPreferredSize.w)) {
		const TextSystem& text = TextSystem::Default();
		const FontHeight fontHeight = text.Height(FontRole::Plain);

		// Haiku: rect.right + rect.left and rect.bottom + rect.top of its
		// inclusive box frame
		gfx::Rect rect(_CheckBoxFrame(fontHeight));
		float width = (rect.x1 - 1) + rect.x0;
		float height = (rect.y1 - 1) + rect.y0;

		if (!Label().empty()) {
			width += Look().DefaultLabelSpacing(text)
				+ std::ceil(text.StringWidth(Label(), FontRole::Plain));
			height = std::max(height,
				std::ceil(6.0f + fontHeight.ascent + fontHeight.descent));
		}

		// NewGUI: pixel counts
		fPreferredSize = {width + 1, height + 1};

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


int32
BCheckBox::_NextState() const
{
	switch (Value()) {
		case kControlOff:
			return kControlOn;
		case kControlPartiallyOn:
			return fPartialToOff ? kControlOff : kControlOn;
		case kControlOn:
		default:
			return kControlOff;
	}
}


// #pragma mark - accessibility


void
BCheckBox::GetAccessibleInfo(AccessibleInfo* info) const
{
	BControl::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	info->role = AccessibleRole::CheckBox;
	if (Value() == kControlOn)
		info->states |= AccessibleState::Checked;
	else if (Value() == kControlPartiallyOn)
		info->states |= AccessibleState::Mixed;
}


} // namespace ngui
