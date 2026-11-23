/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/Control.h>


namespace ngui {


/*!	A box that is checked or not, or partially (Haiku's BCheckBox). A click,
	Enter or space moves to the next state: off to on, on to off, partially
	on to on (or to off, see SetPartialStateToOff()).

	Differences to Haiku: the mouse is always followed asynchronously; no
	icons (W7); sizes are pixel counts, one more than Haiku's.
*/
class BCheckBox : public BControl {
public:
								BCheckBox(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A check box at \a frame, at least as high as the
										font needs. */
								BCheckBox(std::string_view name, std::string_view label,
									std::unique_ptr<BMessage> message,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A check box for a layout. */
								BCheckBox(std::string_view label,
									std::unique_ptr<BMessage> message = nullptr);
									/*!< A check box for a layout, without a name. */
								~BCheckBox() override;
									/*!< Destroys the check box. */

			void				Draw(DrawContext& context) override;
									/*!< The box and the label by the look. */

			void				KeyDown(std::string_view bytes) override;
									/*!< Enter and space move to the next state. */
			void				MouseDown(gfx::Point where) override;
									/*!< Outlines the box and follows the mouse. */
			void				MouseUp(gfx::Point where) override;
									/*!< Moves to the next state if released inside. */
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Outlined while the mouse is inside. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The box, the label spacing and the label. */
			gfx::Size			MinSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			MaxSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			PreferredSize() override;
									/*!< The box, the label spacing and the label. */
			BAlignment			LayoutAlignment() override;
									/*!< Left, vertically centered. */

			void				SetValue(int32 value) override;
									/*!< kControlOff, kControlOn or kControlPartiallyOn;
										other values are on. */

			bool				IsPartialStateToOff() const;
									/*!< Whether partially on goes to off (not on). */
			void				SetPartialStateToOff(bool partialToOff);
									/*!< Sets where partially on goes. */

protected:
			void				LayoutInvalidated(bool descendants = false) override;
									/*!< Drops the cached preferred size. */

private:
			gfx::Rect			_CheckBoxFrame(const FontHeight& fontHeight) const;
			gfx::Rect			_CheckBoxFrame() const;
			gfx::Size			_ValidatePreferredSize();
			int32				_NextState() const;

			gfx::Size			fPreferredSize = kUnsetSize;
			bool				fOutlined = false;
			bool				fPartialToOff = false;
};


} // namespace ngui
