/*
 * Copyright 2007-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/Messenger.h>
#include <ngui/SupportDefs.h>
#include <ngui/View.h>


namespace ngui {


class BMenuItem;


/*!	How a menu arranges its items (Haiku's menu_layout). */
enum class BMenuLayout : std::uint8_t {
	InRow,		/*!< B_ITEMS_IN_ROW: side by side, as in a menu bar. */
	InColumn,	/*!< B_ITEMS_IN_COLUMN: one below the other. */
	InMatrix	/*!< B_ITEMS_IN_MATRIX: where they are put. */
};


/*!	A list of menu items (Haiku's BMenu): in a column, in a row or at
	given frames; with check marks (one of them in radio mode), shortcuts,
	triggers and submenus. It sizes itself to its items.

	This is the first part of the menus (W3a): items, their layout and
	drawing. Opening a menu in a window of its own and tracking the mouse
	and keyboard come with BMenuBar (W3b).

	Differences to Haiku: the menu owns its items (AddItem() takes them as
	std::unique_ptr, RemoveItem() gives them back); no scripting. Sizes are
	pixel counts, one more than Haiku's.
*/
class BMenu : public BView {
public:
								BMenu(std::string_view name,
									BMenuLayout layout = BMenuLayout::InColumn);
									/*!< A menu named \a name (the label of its superitem). */
								BMenu(std::string_view name, float width, float height);
									/*!< A matrix menu of \a width × \a height pixels. */
								~BMenu() override;
									/*!< Destroys the menu and its items. */

			void				AttachedToWindow() override;
									/*!< Installs the items in the window and lays them
										out. */
			void				DetachedFromWindow() override;
									/*!< Uninstalls the items. */
			void				Draw(DrawContext& context) override;
									/*!< Background and items. */

			gfx::Size			MinSize() override;
									/*!< The size of its items. */
			gfx::Size			MaxSize() override;
									/*!< The size of its items. */
			gfx::Size			PreferredSize() override;
									/*!< The size of its items. */
			void				GetPreferredSize(float* width, float* height) override;
									/*!< The size of its items. */

			bool				AddItem(std::unique_ptr<BMenuItem> item);
									/*!< Adds \a item at the end (not in a matrix). */
			bool				AddItem(std::unique_ptr<BMenuItem> item, int32 index);
									/*!< Adds \a item at \a index (not in a matrix). */
			bool				AddItem(std::unique_ptr<BMenuItem> item, gfx::Rect frame);
									/*!< Adds \a item at \a frame (only in a matrix). */
			bool				AddItem(std::unique_ptr<BMenu> submenu);
									/*!< Adds an item that opens \a submenu. */
			bool				AddItem(std::unique_ptr<BMenu> submenu, int32 index);
									/*!< Adds an item that opens \a submenu at \a index. */
			bool				AddItem(std::unique_ptr<BMenu> submenu, gfx::Rect frame);
									/*!< Adds an item that opens \a submenu at \a frame. */
			bool				AddSeparatorItem();
									/*!< Adds a separator at the end. */
			std::unique_ptr<BMenuItem> RemoveItem(BMenuItem* item);
									/*!< Takes \a item out; nullptr if it is not here. */
			std::unique_ptr<BMenuItem> RemoveItem(int32 index);
									/*!< Takes item \a index out. */
			bool				RemoveItems(int32 index, int32 count);
									/*!< Removes and destroys \a count items from
										\a index. */
			std::unique_ptr<BMenuItem> RemoveItem(BMenu* submenu);
									/*!< Takes the item of \a submenu out. */

			int32				CountItems() const;
									/*!< The number of items. */
			BMenuItem*			ItemAt(int32 index) const;
									/*!< Item \a index, or nullptr. */
			BMenu*				SubmenuAt(int32 index) const;
									/*!< The submenu of item \a index, or nullptr. */
			int32				IndexOf(const BMenuItem* item) const;
									/*!< The index of \a item, or -1. */
			int32				IndexOf(const BMenu* submenu) const;
									/*!< The index of the item of \a submenu, or -1. */
			BMenuItem*			FindItem(std::string_view label) const;
									/*!< The item labelled \a label, also in submenus. */
			BMenuItem*			FindItem(uint32 command) const;
									/*!< The item whose message is \a command, also in
										submenus. */

			void				SetTargetForItems(const BHandler* handler);
									/*!< Sends the messages of all items to \a handler. */
			void				SetTargetForItems(BMessenger messenger);
									/*!< Sends the messages of all items to \a messenger. */

	virtual	void				SetEnabled(bool enable);
									/*!< Enables or disables the menu and its superitem. */
	virtual	void				SetRadioMode(bool on);
									/*!< At most one item is marked. */
	virtual	void				SetTriggersEnabled(bool enable);
									/*!< Whether the triggers are underlined. */
	virtual	void				SetMaxContentWidth(float width);
									/*!< Labels wider than this are shortened; 0 for no
										limit. */

			void				SetLabelFromMarked(bool on);
									/*!< The superitem shows the marked item's label (radio
										mode). */
			bool				IsLabelFromMarked() const;
									/*!< Whether the superitem shows the marked label. */
			bool				IsEnabled() const;
									/*!< Enabled, and so is its supermenu. */
			bool				IsRadioMode() const;
									/*!< Whether at most one item is marked. */
			bool				AreTriggersEnabled() const;
									/*!< Whether the triggers are underlined. */
			float				MaxContentWidth() const;
									/*!< The widest label shown, or 0. */

			BMenuItem*			FindMarked() const;
									/*!< The first marked item, or nullptr. */
			int32				FindMarkedIndex() const;
									/*!< The index of the first marked item, or -1. */

			BMenu*				Supermenu() const;
									/*!< The menu of its superitem, or nullptr. */
			BMenuItem*			Superitem() const;
									/*!< The item that opens it, or nullptr. */

			void				SetItemMargins(float left, float top, float right,
									float bottom);
									/*!< The space around each item's content. */
			void				GetItemMargins(float* left, float* top, float* right,
									float* bottom) const;
									/*!< The space around each item's content. */

			using				BView::Layout;
			BMenuLayout			Layout() const;
									/*!< How the items are arranged. */

	virtual	void				DrawBackground(DrawContext& context);
									/*!< The menu background. */
			void				DrawItems(DrawContext& context);
									/*!< All items. */

			void				SortItems(int (*compare)(const BMenuItem*, const BMenuItem*));
									/*!< Sorts the items, keeping equal ones in order. */
			bool				SwapItems(int32 indexA, int32 indexB);
									/*!< Swaps two items. */
			bool				MoveItem(int32 indexFrom, int32 indexTo);
									/*!< Moves an item to another index. */

protected:
								BMenu(gfx::Rect frame, std::string_view name,
									BResizingMode resizingMode, BViewFlags flags,
									BMenuLayout layout, bool resizeToFit);
									/*!< For BMenuBar: a menu at \a frame. */

			void				LayoutInvalidated(bool descendants) override;
									/*!< Lays the items out anew before they are drawn. */
			void				DoLayout() override;
									/*!< Lays the items out if needed. */

private:
	friend class BMenuItem;

			void				_InitData();
			bool				_AddItem(std::unique_ptr<BMenuItem> item, int32 index);
			std::unique_ptr<BMenuItem> _RemoveItemAt(int32 index);
			bool				_RelayoutIfNeeded();
			void				_LayoutItems(int32 index);
			gfx::Size			_ValidatePreferredSize();
			void				_ComputeLayout(int32 index, bool bestFit, bool moveItems,
									float* width, float* height);
			void				_ComputeColumnLayout(int32 index, bool bestFit,
									bool moveItems, gfx::Rect& frame);
			void				_ComputeRowLayout(int32 index, bool bestFit, bool moveItems,
									gfx::Rect& frame);
			void				_ComputeMatrixLayout(gfx::Rect& frame);
			void				_CacheFontInfo();
			void				_ItemMarked(BMenuItem* item);
			void				_Install(BWindow* target);
			void				_Uninstall();
			void				_CalcTriggers();

			std::vector<std::unique_ptr<BMenuItem>> fItems;
			BMenuItem*			fSelected = nullptr;
			BMenu*				fSuper = nullptr;
			BMenuItem*			fSuperitem = nullptr;
			gfx::Rect			fPad;
									// left, top, right, bottom margins (not a rectangle)
			float				fAscent = -1.0f;
			float				fDescent = -1.0f;
			float				fFontHeight = -1.0f;
			BMenuLayout			fLayout;
			float				fMaxContentWidth = 0.0f;
			gfx::Size			fPreferred = {kSizeUnset, kSizeUnset};
									// Haiku's size (pixels - 1)
			BResizingMode		fLastResizingMode;
			bool				fResizeToFit = true;
			bool				fUseCachedMenuLayout = false;
			bool				fEnabled = true;
			bool				fDynamicName = false;
			bool				fRadioMode = false;
			bool				fTriggerEnabled = false;
			bool				fHasSubmenus = false;
};


} // namespace ngui
