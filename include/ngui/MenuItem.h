/*
 * Copyright 2006-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>

#include <ngui/InterfaceDefs.h>
#include <ngui/Invoker.h>
#include <ngui/SupportDefs.h>
#include <ngui/View.h>


namespace ngui {


class BMenu;
class BWindow;


/*!	An item of a BMenu (Haiku's BMenuItem): a label that sends its message
	when chosen, or opens a submenu; it may be marked, disabled, have a
	keyboard shortcut and a trigger character.

	Differences to Haiku: the item owns its submenu and its message
	(std::unique_ptr); the menu owns its items; the drawing hooks get the
	DrawContext. The trigger index is a byte offset into the UTF-8 label.
	Frame() is half-open; internally the item keeps Haiku's inclusive
	rectangle, so the layout arithmetic is Haiku's.
*/
class BMenuItem : public BInvoker {
public:
								BMenuItem(std::string_view label,
									std::unique_ptr<BMessage> message,
									uint32 shortcut = 0, BModifierFlags modifiers = {});
									/*!< An item with \a label that sends \a message;
										\a shortcut is a character typed with
										\a modifiers. */
	explicit					BMenuItem(std::unique_ptr<BMenu> menu,
									std::unique_ptr<BMessage> message = nullptr);
									/*!< An item that opens \a menu; its label is the
										menu's name (or the marked item's label). */
	virtual						~BMenuItem();
									/*!< Destroys the item and its submenu. */

								BMenuItem(const BMenuItem&) = delete;
								BMenuItem& operator=(const BMenuItem&) = delete;

	virtual	void				SetLabel(std::string_view label);
									/*!< Sets the label. */
	virtual	void				SetEnabled(bool enable);
									/*!< Enables or disables the item (and its submenu). */
	virtual	void				SetMarked(bool mark);
									/*!< Marks it with a check mark; in a radio menu the
										others lose theirs. */
	virtual	void				SetTrigger(char trigger);
									/*!< The character that chooses it from the keyboard;
										0 for one chosen by the menu. */
			void				SetShortcut(uint32 shortcut, BModifierFlags modifiers);
									/*!< The keyboard shortcut: \a shortcut with
										\a modifiers, always with Command unless
										BModifier::NoCommand is given; installed in
										the window of the item's menu (see
										BWindow::AddShortcut()). */

			const std::string&	Label() const;
									/*!< The label. */
			bool				IsEnabled() const;
									/*!< Enabled, and so are its submenu and its menu. */
			bool				IsMarked() const;
									/*!< Whether it is marked. */
			char				Trigger() const;
									/*!< The trigger set with SetTrigger(), or 0. */
			uint32				Shortcut(BModifierFlags* modifiers = nullptr) const;
									/*!< The shortcut character; its modifiers in
										\a modifiers. */
			BMenu*				Submenu() const;
									/*!< The submenu, or nullptr. */
			BMenu*				Menu() const;
									/*!< The menu it is in, or nullptr. */
			gfx::Rect			Frame() const;
									/*!< Where it is in its menu. */

			std::expected<void, Error> Invoke(const BMessage* message = nullptr) override;
									/*!< Sends the message with "index", "when" and
										"source"; marks the item in a radio menu. Fails
										for a disabled item or without a message. */

protected:
	virtual	void				GetContentSize(float* width, float* height);
									/*!< The size of the label (Haiku's measure). */
	virtual	std::string			TruncateLabel(float maxWidth);
									/*!< The label shortened in the middle to \a maxWidth. */
	virtual	void				DrawContent(DrawContext& context);
									/*!< The label at ContentLocation(), and its trigger. */
	virtual	void				Draw(DrawContext& context);
									/*!< Background when selected, content, mark, shortcut
										and submenu arrow. */
	virtual	void				Highlight(bool highlight);
									/*!< Draws it anew when it is selected or deselected. */
			bool				IsSelected() const;
									/*!< Whether it is the selected item of its menu. */
			gfx::Point			ContentLocation() const;
									/*!< Where the content starts: the frame's left top
										corner plus the menu's item margins. */

			gfx::Color			LowColor(const DrawContext& context) const;
									/*!< The background color: selected or not. */
			gfx::Color			HighColor(const DrawContext& context) const;
									/*!< The text color: selected, enabled or disabled. */

private:
	friend class BMenu;
	friend class BMenuBar;
	friend class MenuTracker;

			void				Install(BWindow* window);
			void				Uninstall();
			void				SetSuper(BMenu* superMenu);
			void				Select(bool select);
			void				SetAutomaticTrigger(int32 index, uint32 trigger);

			void				_InitMenuData(std::unique_ptr<BMenu> menu);
			bool				_IsActivated() const;
			void				_DrawMarkSymbol(DrawContext& context);
			void				_DrawShortcutSymbol(DrawContext& context, bool submenus);
			void				_DrawSubmenuSymbol(DrawContext& context);

			std::string			fLabel;
			BMenu*				fSubmenu = nullptr;
			std::unique_ptr<BMenu> fOwnedSubmenu;
									// null while the submenu is shown (its popup has it)
			BWindow*			fWindow = nullptr;
			BMenu*				fSuper = nullptr;
			gfx::Rect			fBounds = {0, 0, -1, -1};
									// Haiku's inclusive rectangle
			BModifierFlags		fModifiers;
			float				fCachedWidth = 0;
			int32				fTriggerIndex = -1;
			char				fUserTrigger = 0;
			bool				fMark = false;
			bool				fEnabled = true;
			bool				fSelected = false;
			uint32				fTrigger = 0;
			uint32				fShortcut = 0;
};


} // namespace ngui
