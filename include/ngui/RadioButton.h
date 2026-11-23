/*
 * Copyright 2001-2008, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/Control.h>


namespace ngui {


/*!	One choice of several (Haiku's BRadioButton): turned on, it turns off
	the radio buttons among its siblings. A click, Enter or space turn it
	on; it cannot be turned off by the user.

	Differences to Haiku: the mouse is always followed asynchronously; it
	is centered vertically in a layout (Haiku: vertically unset, which
	moves it above its cell when the cell is higher); no icons (W7); the
	special cases of BBox come with BBox (W1). Sizes are pixel counts, one
	more than Haiku's.
*/
class BRadioButton : public BControl {
public:
								BRadioButton(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A radio button at \a frame, at least as high as
										it needs. */
								BRadioButton(std::string_view name, std::string_view label,
									std::unique_ptr<BMessage> message,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A radio button for a layout. */
								BRadioButton(std::string_view label,
									std::unique_ptr<BMessage> message = nullptr);
									/*!< A radio button for a layout, without a name. */
								~BRadioButton() override;
									/*!< Destroys the radio button. */

			void				Draw(DrawContext& context) override;
									/*!< The knob and the label by the look. */

			void				MouseDown(gfx::Point where) override;
									/*!< Outlines the knob and follows the mouse. */
			void				MouseUp(gfx::Point where) override;
									/*!< Turns it on if released inside. */
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Outlined while the mouse is inside. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Enter and space turn it on. */
			void				FrameResized(float newWidth, float newHeight) override;
									/*!< Draws it anew. */

			void				SetValue(int32 value) override;
									/*!< Sets the value; on turns off its siblings. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The knob, the label spacing and the label. */
			gfx::Size			MaxSize() override;
									/*!< The preferred size unless set explicitly. */
			BAlignment			LayoutAlignment() override;
									/*!< Left, vertically centered. */

private:
			gfx::Rect			_KnobFrame(const FontHeight& fontHeight) const;
			gfx::Rect			_KnobFrame() const;

			bool				fOutlined = false;
};


} // namespace ngui
