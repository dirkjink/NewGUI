/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Invoker.h>
#include <ngui/Messenger.h>
#include <ngui/Window.h>


namespace ngui {


class BBitmap;
class BButton;
class BGroupLayout;
class BTextView;


/*!	What an alert is about (Haiku's alert_type). */
enum class BAlertType : std::uint8_t {
	Empty,		/*!< B_EMPTY_ALERT */
	Info,		/*!< B_INFO_ALERT */
	Idea,		/*!< B_IDEA_ALERT */
	Warning,	/*!< B_WARNING_ALERT */
	Stop		/*!< B_STOP_ALERT */
};


/*!	How the buttons of an alert are spaced (Haiku's button_spacing). */
enum class BButtonSpacing : std::uint8_t {
	Even,		/*!< B_EVEN_SPACING */
	Offset		/*!< B_OFFSET_SPACING: the first button apart from the others. */
};


/*!	A modal window with a text and up to three buttons (Haiku's BAlert,
	module W6): the buttons sit at the bottom right, the last one is the
	default button; a key given to a button presses it. Go() shows it; it
	closes when a button is pressed, which it reports with the button's
	index ("which", 0 for the first).

	Differences to Haiku: Go() is asynchronous only, as NewGUI's
	BPopUpMenu::Go(): it shows the alert and returns at once, the result
	arrives through the invoker's message or a callback (Haiku's blocking
	Go() returned the index). It is static, as the alert is handed to its
	thread. The type icons are NewGUI's own SVG icons, drawn by the
	VectorIconRenderer (none without one) and anew for the window's scale.
	No sound, no archiving and scripting. The alert is modal to the
	application (BWindowFeel::ModalApp).
*/
class BAlert : public BWindow {
public:
								BAlert();
									/*!< An empty info alert, buttons as wide as the widest. */
								BAlert(std::string_view title, std::string_view text,
									std::string_view button1, std::string_view button2 = {},
									std::string_view button3 = {},
									BButtonWidth width = BButtonWidth::AsUsual,
									BAlertType type = BAlertType::Info);
									/*!< An alert with \a text and the buttons that have a
										label. */
								BAlert(std::string_view title, std::string_view text,
									std::string_view button1, std::string_view button2,
									std::string_view button3, BButtonWidth width,
									BButtonSpacing spacing, BAlertType type = BAlertType::Info);
									/*!< The same with the buttons' \a spacing. */
								~BAlert() override;
									/*!< Destroys it. */

			BAlertType			Type() const;
									/*!< What it is about. */
			void				SetType(BAlertType type);
									/*!< What it is about; its icon, unless SetIcon() gave
										one. */
			void				SetIcon(std::unique_ptr<BBitmap> bitmap);
									/*!< The icon left of the text instead of the type's
										(Haiku took ownership of the pointer); nullptr
										brings the type's back. */
			BBitmap*			Icon() const;
									/*!< The icon, once shown: SetIcon()'s or the type's;
										nullptr if none (no type icon without a
										VectorIconRenderer). */
			void				SetText(std::string_view text);
									/*!< Its text. */
			void				SetButtonSpacing(BButtonSpacing spacing);
									/*!< How its buttons are spaced (before Go()). */
			void				SetButtonWidth(BButtonWidth width);
									/*!< How wide its buttons are (before Go()). */
			void				SetShortcut(int32 buttonIndex, char key);
									/*!< \a key presses button \a buttonIndex, e.g.
										keys::kEscape for "Cancel". */
			char				Shortcut(int32 buttonIndex) const;
									/*!< The key of a button, 0 if none. */

	static	std::expected<BMessenger, Error> Go(std::unique_ptr<BAlert> alert,
									std::unique_ptr<BInvoker> invoker = nullptr);
									/*!< Shows \a alert (in front of the window whose thread
										calls) and returns at once; when a button is
										pressed, \a invoker's message goes out with its
										index in "which". InvalidArgument without buttons. */
	static	std::expected<BMessenger, Error> Go(std::unique_ptr<BAlert> alert,
									std::function<void(int32 which)> done);
									/*!< The same with a callback; \a done runs on the
										alert's thread, so send a message to reach a
										window. */

			void				MessageReceived(BMessage* message) override;
									/*!< A button pressed: reports it and quits. */

			void				AddButton(std::string_view label, char key = 0);
									/*!< Adds a button, which becomes the default button. */
			int32				CountButtons() const;
									/*!< The number of buttons. */
			BButton*			ButtonAt(int32 index) const;
									/*!< Button \a index, or nullptr. */
			BTextView*			TextView() const;
									/*!< The view that shows the text. */

	static	gfx::Point			AlertPosition(float width, float height);
									/*!< Where an alert of that size goes on the screen:
										centered, a quarter down. */

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Alert, its text as description. */

private:
	class KeyFilter;

			void				_Init(std::string_view text, std::string_view button1,
									std::string_view button2, std::string_view button3,
									BButtonWidth width, BButtonSpacing spacing,
									BAlertType type);
			BButton*			_CreateButton(int32 which, std::string_view label);
			void				_CreateTypeIcon();
			void				_Prepare(const gfx::Rect* parentFrame);
			gfx::Point			_AlertPosition(const gfx::Rect& frame) const;
	static	std::expected<BMessenger, Error> _Go(std::unique_ptr<BAlert> alert);

			BView*				fIconView = nullptr;
			BTextView*			fTextView = nullptr;
			BGroupLayout*		fButtonLayout = nullptr;
			std::vector<BButton*> fButtons;
			std::vector<char>	fKeys;
			BAlertType			fType = BAlertType::Info;
			BButtonSpacing		fButtonSpacing = BButtonSpacing::Even;
			BButtonWidth		fButtonWidth = BButtonWidth::AsUsual;
			std::unique_ptr<BInvoker> fInvoker;
			std::function<void(int32)> fDone;
			bool				fReported = false;
};


} // namespace ngui
