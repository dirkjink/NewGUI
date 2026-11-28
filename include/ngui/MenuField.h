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

#include <ngui/InterfaceDefs.h>
#include <ngui/LayoutItem.h>
#include <ngui/View.h>


namespace ngui {


class BMenu;
class BMenuBar;
class BMenuItem;


/*!	A label and a menu, shown as its chosen item (Haiku's BMenuField):
	clicking the field, or Space, Down or Right while it has the focus,
	opens the menu, placed so that the marked item of a BPopUpMenu lies
	over the field. Internally a menu bar with one item whose submenu is
	the menu.

	In a grid, label and menu bar can take a column each
	(CreateLabelLayoutItem(), CreateMenuBarLayoutItem(), or
	BLayoutBuilder::Grid::AddMenuField()), so that the fields of a form
	line up.

	Differences to Haiku: the field owns its menu (taken as
	std::unique_ptr, through its item in the menu bar); the bar's item
	copies only the label of the item it starts with (Haiku: an archived
	copy, with its message and shortcut). A disabled field does not open
	its menu with the mouse either. The layout items are handed over once
	(see CreateLabelLayoutItem()). No archiving and scripting.
*/
class BMenuField : public BView {
public:
								BMenuField(gfx::Rect frame, std::string_view name,
									std::string_view label, std::unique_ptr<BMenu> menu,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A field at \a frame whose menu bar grows with its
										item. */
								BMenuField(gfx::Rect frame, std::string_view name,
									std::string_view label, std::unique_ptr<BMenu> menu,
									bool fixedSize, BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A field at \a frame; with \a fixedSize the menu bar
										reaches to the right edge. */
								BMenuField(std::string_view name, std::string_view label,
									std::unique_ptr<BMenu> menu,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A field for a layout. */
								BMenuField(std::string_view name, std::string_view label,
									std::unique_ptr<BMenu> menu, bool fixedSize,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< A field for a layout; without \a fixedSize the menu
										bar keeps to the left. */
								BMenuField(std::string_view label, std::unique_ptr<BMenu> menu,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::Navigable);
									/*!< An unnamed field for a layout. */
								~BMenuField() override;
									/*!< Destroys the field, its menu bar and menu. */

			void				Draw(DrawContext& context) override;
			void				AttachedToWindow() override;
									/*!< Takes its parent's view color. */
			void				AllAttached() override;
									/*!< Without layout: fits its height to the menu bar. */
			void				MessageReceived(BMessage* message) override;
									/*!< NewGUI: when the look changes, without layout the
										menu bar and the field take the heights of the new
										look's font (specification 18.7). */
			void				MouseDown(gfx::Point where) override;
									/*!< Opens the menu. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Space, Down or Right opens the menu. */
			void				MakeFocus(bool focused = true) override;
			void				WindowActivated(bool active) override;
			void				FrameResized(float newWidth, float newHeight) override;

			BMenu*				Menu() const;
									/*!< The menu. */
			BMenuBar*			MenuBar() const;
									/*!< The menu bar inside the field. */
			BMenuItem*			MenuItem() const;
									/*!< The bar's item, which shows the chosen label. */

			void				SetLabel(std::string_view label);
									/*!< The label left of the menu. */
			const std::string&	Label() const;
									/*!< The label. */

			void				SetEnabled(bool on);
									/*!< Enables or disables the field and its menu bar. */
			bool				IsEnabled() const;
									/*!< Whether it is enabled. */

			void				SetAlignment(BHorizontalAlignment label);
									/*!< How the label is aligned left of the divider. */
			BHorizontalAlignment Alignment() const;
									/*!< The label's alignment. */
			void				SetDivider(float position);
									/*!< Where the menu bar begins (without layout). */
			float				Divider() const;
									/*!< Where the menu bar begins. */

			void				ShowPopUpMarker();
									/*!< Shows the arrow right in the menu bar. */
			void				HidePopUpMarker();
									/*!< Hides the arrow. */

			void				ResizeToPreferred() override;
			void				GetPreferredSize(float* width, float* height) override;

			gfx::Size			MinSize() override;
			gfx::Size			MaxSize() override;
									/*!< Unlimited width. */
			gfx::Size			PreferredSize() override;

			std::unique_ptr<BLayoutItem> CreateLabelLayoutItem();
									/*!< An item for the label, to put into a layout
										beside CreateMenuBarLayoutItem(); the field then
										spans both. NewGUI: the layout owns it, so it is
										created once, and nullptr comes back while it
										lives (Haiku gave the same item again). A field
										without parent must come into the layout with one
										of its items (BLayoutItem::TakeView()). */
			std::unique_ptr<BLayoutItem> CreateMenuBarLayoutItem();
									/*!< An item for the menu bar, as
										CreateLabelLayoutItem(); unlimited in width. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role ComboBox: its label as name, the marked item
										as value; Click. */
			std::expected<void, Error> PerformAccessibleAction(AccessibleAction action,
									const BMessage& data) override;
									/*!< Click opens its menu, as Space does. */

protected:
			void				LayoutInvalidated(bool descendants) override;
			void				DoLayout() override;

private:
	friend class MenuFieldBar;

			// Without layout: the menu bar's preferred height, the field fit
			// to it (the constructor and AllAttached()).
			void				_FitWithoutLayout();

			class LabelLayoutItem;
			class MenuBarLayoutItem;

	struct LayoutData {
		LabelLayoutItem*	labelLayoutItem = nullptr;
		MenuBarLayoutItem*	menuBarLayoutItem = nullptr;
		float				previousHeight = -1;
		float				labelWidth = 0;
		float				labelHeight = 0;
		gfx::Size			min = {0, 0};
		gfx::Size			menuBarMin = {0, 0};
		bool				valid = false;
	};

			void				_InitObject(std::string_view label);
			void				_InitObject2();
			void				_DrawLabel(DrawContext& context);
			void				_DrawMenuBar(DrawContext& context);
			void				_InitMenuBar(std::unique_ptr<BMenu> menu, gfx::Rect frame,
									bool fixedSize);
			void				_AddMenu(std::unique_ptr<BMenu> menu);
			void				_ValidateLayoutData();
			float				_MenuBarOffset() const;
			float				_MenuBarWidth() const;
			void				_UpdateFrame();

			std::string			fLabel;
			BMenu*				fMenu = nullptr;
			BMenuBar*			fMenuBar = nullptr;
			BHorizontalAlignment fAlign = BHorizontalAlignment::Left;
			float				fDivider = 0;
			bool				fEnabled = true;
			bool				fFixedSizeMB = false;
			LayoutData			fLayoutData;
};


} // namespace ngui
