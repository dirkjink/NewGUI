/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <optional>
#include <string_view>

#include <ngui/Control.h>


namespace ngui {


/*!	A push button (Haiku's BButton): sends its message when it is clicked,
	or when Enter or space is pressed while it has the focus; the default
	button of a window hears Enter as well. As a toggle button it keeps its
	value between clicks.

	Differences to Haiku: the mouse is always followed asynchronously, as
	with B_ASYNCHRONOUS_CONTROLS (the window thread never waits in a loop);
	the button does not pause (snooze()) before it sends its message; icons
	come with BBitmap (W7). Sizes are pixel counts, one more than Haiku's
	sizes.
*/
class BButton : public BControl {
public:
	/*!	How the button behaves. */
	enum class BBehavior : std::uint8_t {
		Button,		/*!< B_BUTTON_BEHAVIOR: on while pressed. */
		Toggle,		/*!< B_TOGGLE_BEHAVIOR: each click switches the value. */
		PopUp		/*!< B_POP_UP_BEHAVIOR: with a pop-up part at the right. */
	};

								BButton(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable
										| BViewFlag::FullUpdateOnResize);
									/*!< A button at \a frame, at least as high as the font
										needs. */
								BButton(std::string_view name, std::string_view label,
									std::unique_ptr<BMessage> message,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable
										| BViewFlag::FullUpdateOnResize);
									/*!< A button for a layout. */
								BButton(std::string_view label,
									std::unique_ptr<BMessage> message = nullptr);
									/*!< A button for a layout, without a name. */
								~BButton() override;
									/*!< Destroys the button. */

			void				Draw(DrawContext& context) override;
									/*!< Frame, background and label by the look. */
			void				MouseDown(gfx::Point where) override;
									/*!< Presses it and follows the mouse. */
			void				AttachedToWindow() override;
									/*!< A default button tells its window. */
			void				MessageReceived(BMessage* message) override;
									/*!< NewGUI: when the look changes, a button made with a
										frame takes the height its constructor gives it
										with the new look's font (specification 18.7). */
			void				KeyDown(std::string_view bytes) override;
									/*!< Enter and space press and invoke it. */

	virtual	void				MakeDefault(bool flag);
									/*!< Makes it the default button of its window (or not);
										it gets larger by the look's default frame. */
			void				SetLabel(std::string_view label) override;
									/*!< Sets the label. */

			bool				IsDefault() const;
									/*!< Whether it is the default button. */
			bool				IsFlat() const;
									/*!< Whether it is drawn flat (as in a tool bar). */
			void				SetFlat(bool flat);
									/*!< Draws it flat unless pressed. */

			BBehavior			Behavior() const;
									/*!< How it behaves. */
			void				SetBehavior(BBehavior behavior);
									/*!< Sets how it behaves. */

			BMessage*			PopUpMessage() const;
									/*!< The message of the pop-up part, or nullptr. */
			void				SetPopUpMessage(std::unique_ptr<BMessage> message);
									/*!< Sets the message of the pop-up part. */

			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Hover, and pressed while the mouse is inside. */
			void				MouseUp(gfx::Point where) override;
									/*!< Invokes if released inside. */

			void				SetValue(int32 value) override;
									/*!< Sets the value. */
			void				GetPreferredSize(float* width, float* height) override;
									/*!< Room for the label and the look's frame. */
			std::expected<void, Error> Invoke(const BMessage* message = nullptr) override;
									/*!< Sends the message; a push button is off again. */

			gfx::Size			MinSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			MaxSize() override;
									/*!< The preferred size unless set explicitly. */
			gfx::Size			PreferredSize() override;
									/*!< Room for the label and the look's frame. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Button, Default if it is the default button. */

protected:
			void				LayoutInvalidated(bool descendants = false) override;
									/*!< Drops the cached preferred size. */

private:
			gfx::Size			_ValidatePreferredSize();
			gfx::Rect			_PopUpRect() const;

			bool				_Flag(std::uint32_t flag) const;
			bool				_SetFlag(std::uint32_t flag, bool set);

			gfx::Size			fPreferredSize = {-1, -1};
			// the height the frame constructor was given
			std::optional<float> fFrameHeight;
			std::uint32_t		fFlags = 0;
			BBehavior			fBehavior = BBehavior::Button;
			std::unique_ptr<BMessage> fPopUpMessage;
};


} // namespace ngui
