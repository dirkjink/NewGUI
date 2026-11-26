/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>

#include <ngui/Invoker.h>
#include <ngui/View.h>


namespace ngui {


inline constexpr int32 kControlOff = 0;			/*!< B_CONTROL_OFF */
inline constexpr int32 kControlOn = 1;			/*!< B_CONTROL_ON */
inline constexpr int32 kControlPartiallyOn = 2;	/*!< B_CONTROL_PARTIALLY_ON */


/*!	The base of the controls (module W2), as Haiku's BControl: a view with a
	label, a value and an enabled state that sends its message when the
	user changes it (BInvoker). Its target is its window unless set.

	Not ported (yet): icons (they come with BBitmap, W7), the scripting
	properties, the notices to watchers, and the field "be:sender" of the
	invocation message (BMessage has no messenger fields).
*/
class BControl : public BView, public BInvoker {
public:
								BControl(gfx::Rect frame, std::string_view name,
									std::string_view label,
									std::unique_ptr<BMessage> message,
									BResizingMode resizingMode, BViewFlags flags);
									/*!< A control at \a frame that sends \a message. */
								BControl(std::string_view name, std::string_view label,
									std::unique_ptr<BMessage> message, BViewFlags flags);
									/*!< A control for a layout. */
								~BControl() override;
									/*!< Destroys the control and its message. */

			void				WindowActivated(bool active) override;
									/*!< Draws the focus anew. */
			void				AttachedToWindow() override;
									/*!< Makes the window the target if none is set. */

			void				MakeFocus(bool focus = true) override;
									/*!< Draws the focus anew. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Enter and space toggle the value and invoke. */

	virtual	void				SetLabel(std::string_view label);
									/*!< Sets the label; empty for none. */
			const std::string&	Label() const;
									/*!< The label; empty if there is none. */

	virtual	void				SetValue(int32 value);
									/*!< Sets the value (kControlOff, kControlOn ...). */
			int32				Value() const;
									/*!< The value. */

	virtual	void				SetEnabled(bool enabled);
									/*!< Enables or disables it; a disabled control is not
										Navigable. */
			bool				IsEnabled() const;
									/*!< Whether it is enabled. */

			std::expected<void, Error> Invoke(const BMessage* message = nullptr) override;
									/*!< Sends a copy of \a message (Message() by default)
										with "when", "source" and "be:value". */
			void				MessageReceived(BMessage* message) override;
									/*!< Scripting: gets and sets "Enabled", "Label" and
										"Value"; otherwise as BView's. */
			BHandler*			ResolveSpecifier(BMessage* message, std::int32_t index,
									const BMessage& specifier, std::uint32_t form,
									std::string_view property) override;
									/*!< Its scripting properties; otherwise as BView's. */
			std::expected<void, Error> GetSupportedSuites(BMessage* data) override;
									/*!< "suite/vnd.Be-control" and BView's. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Its label as name, Disabled, Click while enabled. */
			std::expected<void, Error> PerformAccessibleAction(AccessibleAction action,
									const BMessage& data) override;
									/*!< Click as Space does (KeyDown()), while enabled. */

protected:
			bool				IsFocusChanging() const;
									/*!< Inside MakeFocus(). */
			bool				IsTracking() const;
									/*!< Whether it follows the mouse. */
			void				SetTracking(bool state);
									/*!< Sets whether it follows the mouse. */
			void				SetValueNoUpdate(int32 value);
									/*!< Sets the value without drawing. */

private:
			void				_InitData();

			std::string			fLabel;
			int32				fValue = kControlOff;
			bool				fEnabled = true;
			bool				fFocusChanging = false;
			bool				fTracking = false;
			bool				fWantsNav = false;
};


} // namespace ngui
