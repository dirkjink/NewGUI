/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <optional>
#include <string>
#include <string_view>

#include <ngui/TextSystem.h>
#include <ngui/View.h>


namespace ngui {


/*!	A text that cannot be edited (Haiku's BStringView): one line or several
	separated by '\\n', aligned left, right or centered, truncated with "…"
	if asked to and too wide. It takes its parent's background color.

	Sizes are pixel counts, one more than Haiku's; the text is measured and
	drawn with the plain font of the text system.
*/
class BStringView : public BView {
public:
								BStringView(gfx::Rect frame, std::string_view name,
									std::string_view text,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A text at \a frame. */
								BStringView(std::string_view name, std::string_view text,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A text for a layout. */
								~BStringView() override;
									/*!< Destroys the view. */

			void				SetText(std::string_view text);
									/*!< Sets the text. */
			const std::string&	Text() const;
									/*!< The text. */
			void				SetAlignment(BHorizontalAlignment alignment);
									/*!< Left, right or centered. */
			BHorizontalAlignment Alignment() const;
									/*!< The alignment (left by default). */
			void				SetTruncation(std::optional<TruncationMode> truncation);
									/*!< How too wide lines are shortened; none by default
										(B_NO_TRUNCATION). */
			std::optional<TruncationMode> Truncation() const;
									/*!< How too wide lines are shortened. */

			void				AttachedToWindow() override;
									/*!< Takes the parent's background color. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The widest line and the height of all lines. */
			gfx::Size			MinSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			MaxSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			PreferredSize() override;
									/*!< The widest line and the height of all lines. */
			void				ResizeToPreferred() override;
									/*!< Keeps a larger width unless aligned left. */
			BAlignment			LayoutAlignment() override;
									/*!< Its alignment, vertically centered. */

			void				Draw(DrawContext& context) override;
									/*!< Draws the lines in the panel text color. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Label, its text as name. */

protected:
			void				LayoutInvalidated(bool descendants = false) override;
									/*!< Drops the cached height. */

private:
			gfx::Size			_ValidatePreferredSize();
			float				_StringWidth(std::string_view text);

			std::string			fText;
			std::optional<TruncationMode> fTruncation;
			BHorizontalAlignment fAlign = BHorizontalAlignment::Left;
			gfx::Size			fPreferredSize;
};


} // namespace ngui
