/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Control.cpp (commit b1d497bb69).
 * Without archiving, scripting, icons and notices (see Control.h). The label
 * is a std::string, empty for none (Haiku: NULL); the message is owned by
 * the BInvoker as std::unique_ptr.
 *
 * Authors (Haiku):
 *		Marc Flerackers, mflerackers@androme.be
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 */


#include <ngui/Control.h>

#include <ngui/Clock.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/Window.h>


namespace ngui {


BControl::BControl(gfx::Rect frame, std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitData();
	SetLabel(label);
	SetMessage(std::move(message));
}


BControl::BControl(std::string_view name, std::string_view label,
	std::unique_ptr<BMessage> message, BViewFlags flags)
	:
	BView(name, flags)
{
	_InitData();
	SetLabel(label);
	SetMessage(std::move(message));
}


BControl::~BControl()
{
}


void
BControl::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


void
BControl::AttachedToWindow()
{
	if (!Messenger().IsValid())
		SetTarget(Window());

	BView::AttachedToWindow();
}


void
BControl::MakeFocus(bool focus)
{
	if (focus == IsFocus())
		return;

	BView::MakeFocus(focus);

	if (Window() != nullptr) {
		fFocusChanging = true;
		Invalidate();
		fFocusChanging = false;
	}
}


void
BControl::KeyDown(std::string_view bytes)
{
	if (bytes.size() == 1 && (bytes[0] == keys::kEnter || bytes[0] == keys::kSpace)) {
		if (!fEnabled)
			return;

		SetValue(Value() ? kControlOff : kControlOn);
		(void)Invoke();
	} else
		BView::KeyDown(bytes);
}


void
BControl::SetLabel(std::string_view label)
{
	// Has the label been changed?
	if (fLabel == label)
		return;

	fLabel = label;
	InvalidateLayout();
	Invalidate();
}


const std::string&
BControl::Label() const
{
	return fLabel;
}


void
BControl::SetValue(int32 value)
{
	if (value == fValue)
		return;

	fValue = value;
	Invalidate();
}


void
BControl::SetValueNoUpdate(int32 value)
{
	fValue = value;
}


int32
BControl::Value() const
{
	return fValue;
}


void
BControl::SetEnabled(bool enabled)
{
	if (fEnabled == enabled)
		return;

	fEnabled = enabled;

	if (fEnabled && fWantsNav)
		SetFlags(Flags() | BViewFlag::Navigable);
	else if (!fEnabled && Flags().Has(BViewFlag::Navigable)) {
		fWantsNav = true;
		SetFlags(Flags() & ~BViewFlags(BViewFlag::Navigable));
	} else
		fWantsNav = false;

	if (Window())
		Invalidate();
}


bool
BControl::IsEnabled() const
{
	return fEnabled;
}


std::expected<void, Error>
BControl::Invoke(const BMessage* message)
{
	if (message == nullptr)
		message = Message();
	if (message == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to send");

	BMessage clone = *message;
	(void)clone.AddInt64("when", Clock::Default().Now().count());
	(void)clone.AddPointer("source", this);
	(void)clone.AddInt32("be:value", fValue);

	return BInvoker::Invoke(&clone);
}


bool
BControl::IsFocusChanging() const
{
	return fFocusChanging;
}


bool
BControl::IsTracking() const
{
	return fTracking;
}


void
BControl::SetTracking(bool state)
{
	fTracking = state;
}


void
BControl::_InitData()
{
	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));

	fLabel.clear();
	fValue = kControlOff;
	fEnabled = true;
	fFocusChanging = false;
	fTracking = false;
	fWantsNav = Flags().Has(BViewFlag::Navigable);
}


} // namespace ngui
