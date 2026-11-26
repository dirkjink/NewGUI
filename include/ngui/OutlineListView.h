/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <ngui/ListView.h>


namespace ngui {


/*!	A list view whose items form a tree (Haiku's BOutlineListView): each
	item has an outline level, the items below it with a deeper level are
	its subitems. An item with subitems shows a latch left of it; clicking
	it, or Right and Left on the selected item, expands and collapses it.
	Collapsing hides the subitems; a hidden selected item passes its
	selection to the collapsed one.

	The inherited methods work on the visible items (ItemAt(),
	CurrentSelection(), Select() ...); the FullList methods on all items.
	AddItem(), RemoveItem() and AddList() take indices into the full list.

	Differences to Haiku:
	- The outline view owns its items: the visible ones in the list, the
	  hidden ones itself.
	- RemoveItem() removes an item with its subitems also when it is
	  hidden (Haiku: only a visible one) and destroys the subitems.
	- An item inserted before deeper items adopts them: their visibility
	  and the latches are brought up to date (Haiku left them).
	- ReplaceItem() puts the new item in the full list too, with the old
	  one's level and expansion; MoveItem() is refused (Haiku moved the
	  item only among the visible ones); SortItems() sorts the tree like
	  FullListSortItems() (Haiku: only the visible items, tearing the
	  tree apart).
	- EachItemUnder() and FullListDoForEach() take a std::function.
*/
class BOutlineListView : public BListView {
public:
								BOutlineListView(gfx::Rect frame, std::string_view name,
									BListViewType type = BListViewType::SingleSelection,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< An outline list view at \a frame. */
	explicit					BOutlineListView(std::string_view name,
									BListViewType type = BListViewType::SingleSelection,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< An outline list view for a layout. */
								~BOutlineListView() override;
									/*!< Destroys the view and all its items. */

			void				MouseDown(gfx::Point where) override;
									/*!< A click on a latch expands or collapses. */
			void				KeyDown(std::string_view bytes) override;
									/*!< Right expands the selected item or selects its
										first subitem; Left collapses it or selects its
										superitem. */

	virtual	bool				AddUnder(std::unique_ptr<BListItem> item,
									BListItem* superItem);
									/*!< Adds \a item as the first subitem of \a superItem
										(at the top level without one). */
			bool				AddItem(std::unique_ptr<BListItem> item) override;
									/*!< Appends \a item to the full list. */
			bool				AddItem(std::unique_ptr<BListItem> item,
									int32 fullListIndex) override;
									/*!< Inserts \a item at \a fullListIndex; its level
										makes it a subitem of the item before. */
			bool				AddList(std::vector<std::unique_ptr<BListItem>> items) override;
									/*!< Appends \a items. */
			bool				AddList(std::vector<std::unique_ptr<BListItem>> items,
									int32 fullListIndex) override;
									/*!< Inserts \a items at \a fullListIndex. */
			std::unique_ptr<BListItem> RemoveItem(BListItem* item) override;
									/*!< Takes \a item out and gives it back; its subitems
										are destroyed. */
			std::unique_ptr<BListItem> RemoveItem(int32 fullListIndex) override;
									/*!< Takes item \a fullListIndex out with its
										subitems. */
			bool				RemoveItems(int32 fullListIndex, int32 count) override;
									/*!< Removes \a count items (with their subitems) at
										\a fullListIndex. */

			BListItem*			FullListItemAt(int32 fullListIndex) const;
									/*!< Item \a fullListIndex, or nullptr. */
			int32				FullListIndexOf(gfx::Point where) const;
									/*!< The full list index of the item at \a where. */
			int32				FullListIndexOf(const BListItem* item) const;
									/*!< The full list index of \a item, or -1. */
			BListItem*			FullListFirstItem() const;
									/*!< The first item, or nullptr. */
			BListItem*			FullListLastItem() const;
									/*!< The last item, or nullptr. */
			bool				FullListHasItem(const BListItem* item) const;
									/*!< Whether \a item is in the outline. */
			int32				FullListCountItems() const;
									/*!< The number of all items. */
			int32				FullListCurrentSelection(int32 index = 0) const;
									/*!< The full list index of the \a index-th selected
										item, or -1. */
			void				MakeEmpty() override;
									/*!< Destroys all items. */
			bool				FullListIsEmpty() const;
									/*!< Whether it has no items. */
			void				FullListDoForEach(
									const std::function<bool(BListItem* item)>& function);
									/*!< Calls \a function with each item until it returns
										true. */

			BListItem*			Superitem(const BListItem* item);
									/*!< The item \a item is a subitem of, or nullptr. */
			void				Expand(BListItem* item);
									/*!< Shows the subitems of \a item. */
			void				Collapse(BListItem* item);
									/*!< Hides the subitems of \a item. */
			bool				IsExpanded(int32 fullListIndex);
									/*!< Whether item \a fullListIndex is expanded. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< The widest item with its indentation and latch,
										as high as the visible items. */

			void				SortItems(int (*compare)(const BListItem*,
									const BListItem*)) override;
									/*!< Sorts the tree, as FullListSortItems(). */
			void				FullListSortItems(int (*compare)(const BListItem* first,
									const BListItem* second));
									/*!< Sorts the items of each level among themselves,
										each with its subitems. */
			void				SortItemsUnder(BListItem* superItem, bool oneLevelOnly,
									int (*compare)(const BListItem* first,
										const BListItem* second));
									/*!< Sorts the subitems of \a superItem (all items
										without one), only its direct subitems if
										\a oneLevelOnly. */
			int32				CountItemsUnder(BListItem* superItem,
									bool oneLevelOnly) const;
									/*!< The number of subitems of \a superItem. */
			BListItem*			EachItemUnder(BListItem* superItem, bool oneLevelOnly,
									const std::function<BListItem*(BListItem* item)>& function);
									/*!< Calls \a function with each subitem until it
										returns an item, and returns that. */
			BListItem*			ItemUnderAt(BListItem* superItem, bool oneLevelOnly,
									int32 index) const;
									/*!< The \a index-th subitem of \a superItem. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Tree. */

protected:
			bool				DoMiscellaneous(MiscCode code, MiscData* data) override;
									/*!< Swaps items with their subitems, replaces in the
										full list too; refuses to move. */

	virtual	void				ExpandOrCollapse(BListItem* superItem, bool expand);
									/*!< Shows or hides the subitems of \a superItem. */
	virtual	gfx::Rect			LatchRect(gfx::Rect itemRect, int32 level) const;
									/*!< Where the latch of an item at \a level in
										\a itemRect is. */
	virtual	void				DrawLatch(DrawContext& context, gfx::Rect itemRect,
									int32 level, bool collapsed, bool highlighted,
									bool misTracked);
									/*!< An arrow pointing right (collapsed) or down. */
			void				DrawItem(BListItem* item, DrawContext& context,
									gfx::Rect itemRect, bool complete = false) override;
									/*!< The latch, then the item right of it. */

private:
			int32				_FullListIndex(int32 index) const;
			bool				_SwapItems(int32 first, int32 second);
			int32				_SubtreeEnd(int32 fullListIndex) const;
			void				_SortRange(int32 begin, int32 end, bool oneLevelOnly,
									int (*compare)(const BListItem*, const BListItem*));
			void				_Restructure();
			void				_OrderVisibleItems();
			std::unique_ptr<BListItem> _RemoveItem(BListItem* item, int32 fullListIndex);
			BListItem*			_SuperitemForIndex(int32 fullListIndex, uint32 level,
									int32* superIndex = nullptr) const;
			int32				_FindPreviousVisibleIndex(int32 fullListIndex) const;
			bool				_ItemsUnderSetup(BListItem* superItem, int32& startIndex,
									uint32& baseLevel) const;
			std::unique_ptr<BListItem> _TakeHidden(BListItem* item);

			std::vector<BListItem*> fFullList;
			std::unordered_map<BListItem*, std::unique_ptr<BListItem>> fHidden;
};


} // namespace ngui
