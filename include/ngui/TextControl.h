/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>

#include <ngui/Control.h>
#include <ngui/ControlLook.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/LayoutItem.h>


namespace ngui {


class BTextView;
class TextInput;


/*!	A label and a one-line text field (Haiku's BTextControl): the field is a
	BTextView without line breaks (typed or pasted line breaks become
	spaces). Enter, or leaving the field with changed text, sends the
	control's message; every change sends the modification message. Tab
	moves the focus on; a click on the label focuses the field.

	In a grid, label and field can take a column each
	(CreateLabelLayoutItem(), CreateTextViewLayoutItem(), or
	BLayoutBuilder::Grid::AddTextControl()).

	Differences to Haiku: the layout items are handed out once while they
	live (see BMenuField::CreateLabelLayoutItem()); no archiving and
	scripting; the text's color is set with BTextView::SetTextColor().
*/
class BTextControl : public BControl {
public:
								BTextControl(gfx::Rect frame, std::string_view name,
									std::string_view label, std::string_view text,
									std::unique_ptr<BMessage> message,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A text control at \a frame, as high as it needs. */
								BTextControl(std::string_view name, std::string_view label,
									std::string_view text, std::unique_ptr<BMessage> message,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A text control for a layout. */
								BTextControl(std::string_view label, std::string_view text,
									std::unique_ptr<BMessage> message);
									/*!< An unnamed text control for a layout. */
								~BTextControl() override;
									/*!< Destroys the control and its text view. */

	virtual	void				SetText(std::string_view text);
									/*!< Replaces the text; while focused, it is selected. */
			std::string_view	Text() const;
									/*!< The text. */
			int32				TextLength() const;
									/*!< Its length in bytes. */
			void				MarkAsInvalid(bool invalid);
									/*!< Shows the field in the failure color. */

			BTextView*			TextView() const;
									/*!< The text view of the field. */

	virtual	void				SetModificationMessage(std::unique_ptr<BMessage> message);
									/*!< Sent on every change of the text. */
			BMessage*			ModificationMessage() const;
									/*!< The modification message, or nullptr. */

	virtual	void				SetAlignment(BHorizontalAlignment label,
									BHorizontalAlignment text);
									/*!< How the label and the text are aligned. */
			void				GetAlignment(BHorizontalAlignment* label,
									BHorizontalAlignment* text) const;
									/*!< The alignments; pointers may be nullptr. */
	virtual	void				SetDivider(float position);
									/*!< Where the field begins (without layout). */
			float				Divider() const;
									/*!< Where the field begins. */

			void				Draw(DrawContext& context) override;
									/*!< The label and the field's frame. */
			void				MouseDown(gfx::Point where) override;
									/*!< Focuses the field. */
			void				AttachedToWindow() override;
			void				MakeFocus(bool focus = true) override;
									/*!< Focuses the field and selects its text. */
			void				SetEnabled(bool enable) override;
									/*!< Enables or disables the field too. */
			void				WindowActivated(bool active) override;

			void				GetPreferredSize(float* width, float* height) override;
			void				ResizeToPreferred() override;
			gfx::Size			MinSize() override;
			gfx::Size			MaxSize() override;
									/*!< Unlimited width. */
			gfx::Size			PreferredSize() override;
			BAlignment			LayoutAlignment() override;
									/*!< Left, vertically centered. */

			std::unique_ptr<BLayoutItem> CreateLabelLayoutItem();
									/*!< An item for the label, beside
										CreateTextViewLayoutItem(); created once while it
										lives, else nullptr. */
			std::unique_ptr<BLayoutItem> CreateTextViewLayoutItem();
									/*!< An item for the field, unlimited in width. */

protected:
			void				LayoutInvalidated(bool descendants) override;
			void				DoLayout() override;

private:
	friend class TextInput;

			class LabelLayoutItem;
			class TextViewLayoutItem;

	struct LayoutData {
		LabelLayoutItem*	labelLayoutItem = nullptr;
		TextViewLayoutItem*	textViewLayoutItem = nullptr;
		float				labelWidth = 0;
		float				labelHeight = 0;
		gfx::Size			min = {0, 0};
		gfx::Size			textViewMin = {0, 0};
		bool				valid = false;
	};

			void				_UpdateTextViewColors(bool enable);
			void				_InitData(std::string_view label);
			void				_InitText(std::string_view initialText);
			void				_ValidateLayout();
			void				_LayoutTextView();
			void				_UpdateFrame();
			void				_ValidateLayoutData();

			TextInput*			fText = nullptr;
			std::unique_ptr<BMessage> fModificationMessage;
			BHorizontalAlignment fLabelAlign = BHorizontalAlignment::Left;
			float				fDivider = 0;
			LayoutData			fLayoutData;
			ControlStateFlags	fLook;
};


} // namespace ngui
