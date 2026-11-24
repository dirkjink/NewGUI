/*
 * Copyright 2001-2020 Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/TextInput.cpp (commit
 * b1d497bb69), _BTextInput_, without archiving. NewGUI: MakeResizable(true)
 * is SetWordWrap(false) (the text view does not resize itself, as in Haiku
 * without a container view); the modification message is sent with
 * Invoke() (Haiku: InvokeNotify(..., B_CONTROL_MODIFIED)).
 *
 * Authors (Haiku):
 *		Frans van Nispen (xlr8@tref.nl)
 *		Marc Flerackers (mflerackers@androme.be)
 */


#include "TextInput.h"

#include <cmath>

#include <ngui/AppDefs.h>
#include <ngui/LayoutUtils.h>
#include <ngui/TextControl.h>


namespace ngui {


TextInput::TextInput(gfx::Rect frame, gfx::Rect textRect, BResizingMode resizingMode,
	BViewFlags flags)
	:
	BTextView(frame, "_input_", textRect, resizingMode, flags)
{
	SetWordWrap(false);
}


void
TextInput::MouseDown(gfx::Point where)
{
	fInMouseDown = true;
	BTextView::MouseDown(where);
	fInMouseDown = false;
}


void
TextInput::KeyDown(std::string_view bytes)
{
	switch (bytes.empty() ? '\0' : bytes[0]) {
		case keys::kEnter:
		{
			BTextControl* control = _TextControl();
			if (control == nullptr || !control->IsEnabled())
				break;

			if (!fPreviousText.has_value() || Text() != *fPreviousText) {
				(void)control->Invoke();
				fPreviousText = std::string(Text());
			}

			SelectAll();
			break;
		}

		case keys::kTab:
			BView::KeyDown(bytes);
			break;

		default:
			BTextView::KeyDown(bytes);
			break;
	}
}


void
TextInput::MakeFocus(bool state)
{
	if (state == IsFocus())
		return;

	BTextView::MakeFocus(state);

	if (state) {
		SetInitialText();
		if (!fInMouseDown)
			SelectAll();
	} else {
		BTextControl* control = _TextControl();
		if (control != nullptr && (!fPreviousText.has_value() || Text() != *fPreviousText))
			(void)control->Invoke();

		fPreviousText.reset();
	}

	if (Window() != nullptr) {
		// Invalidate parent to draw or remove the focus mark
		if (BTextControl* parent = _TextControl())
			parent->Invalidate();
	}
}


gfx::Size
TextInput::MinSize()
{
	gfx::Size min;
	min.h = std::ceil(LineHeight(0) + 2.0f);
	// we always add at least one pixel vertical inset top/bottom for
	// the text rect.
	min.w = min.h * 3;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), min);
}


void
TextInput::SetInitialText()
{
	fPreviousText = std::string(Text());
}


void
TextInput::Paste(BClipboard* clipboard)
{
	BTextView::Paste(clipboard);
	Invalidate();
}


void
TextInput::InsertText(std::string_view text, int32 offset, const BTextRunArray* runs)
{
	// Filter all line breaks
	std::string filtered(text);
	for (char& c : filtered) {
		if (c == '\n' || c == '\r')
			c = ' ';
	}
	BTextView::InsertText(filtered, offset, runs);

	if (BTextControl* control = _TextControl(); control != nullptr
		&& control->ModificationMessage() != nullptr) {
		(void)control->Invoke(control->ModificationMessage());
	}
}


void
TextInput::DeleteText(int32 fromOffset, int32 toOffset)
{
	BTextView::DeleteText(fromOffset, toOffset);

	if (BTextControl* control = _TextControl(); control != nullptr
		&& control->ModificationMessage() != nullptr) {
		(void)control->Invoke(control->ModificationMessage());
	}
}


BTextControl*
TextInput::_TextControl()
{
	// NewGUI: nullptr while not yet a child (Haiku: debugger())
	return dynamic_cast<BTextControl*>(Parent());
}


} // namespace ngui
