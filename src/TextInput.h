/*
 * Copyright 2001-2015, Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The text view inside a BTextControl (Haiku's _BTextInput_). Internal.


#include <optional>
#include <string>
#include <string_view>

#include <ngui/TextView.h>


namespace ngui {


class BTextControl;


class TextInput : public BTextView {
public:
								TextInput(gfx::Rect frame, gfx::Rect textRect,
									BResizingMode resizingMode, BViewFlags flags);

			void				MouseDown(gfx::Point where) override;
			void				KeyDown(std::string_view bytes) override;
			void				MakeFocus(bool focus = true) override;
			gfx::Size			MinSize() override;

			void				SetInitialText();

			void				Paste(BClipboard* clipboard) override;

protected:
			void				InsertText(std::string_view text, int32 offset) override;
			void				DeleteText(int32 fromOffset, int32 toOffset) override;

private:
			BTextControl*		_TextControl();

			std::optional<std::string> fPreviousText;
			bool				fInMouseDown = false;
};


} // namespace ngui
