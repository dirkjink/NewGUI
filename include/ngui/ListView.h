/*
 * Copyright 2002-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <ngui/Clock.h>
#include <ngui/Invoker.h>
#include <ngui/ListItem.h>
#include <ngui/View.h>


namespace ngui {


class BScrollView;


/*!	Whether a list view selects one item or several (Haiku's
	list_view_type). */
enum class BListViewType : std::uint8_t {
	SingleSelection,	/*!< B_SINGLE_SELECTION_LIST */
	MultipleSelection	/*!< B_MULTIPLE_SELECTION_LIST */
};


/*!	A list of items, one below the other (Haiku's BListView): selected
	with the mouse (Shift extends, Command toggles in a multiple selection
	list) and the arrow keys, Home and End; a double click, Enter or Space
	invokes it. Each change of the selection sends the selection message;
	invoking sends the invocation message, both with the selected indices
	("index"). In a BScrollView it sets up the scroll bars.

	Differences to Haiku: the list owns its items (AddItem() takes them as
	std::unique_ptr, RemoveItem() and ReplaceItem() give them back,
	MakeEmpty() destroys them); no drag and drop of items (InitiateDrag()
	is not called), no archiving and scripting; the scroll range is the
	pixel count (Haiku's is one less). The selection stays right where
	Haiku's lost track: after selecting a range with a disabled end and
	after moving, swapping or replacing items.
*/
class BListView : public BView, public BInvoker {
public:
								BListView(gfx::Rect frame, std::string_view name,
									BListViewType type = BListViewType::SingleSelection,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< A list view at \a frame. */
	explicit					BListView(std::string_view name,
									BListViewType type = BListViewType::SingleSelection,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents | BViewFlag::Navigable);
									/*!< A list view for a layout. */
								~BListView() override;
									/*!< Destroys the view and its items. */

			void				Draw(DrawContext& context) override;
									/*!< Its items, one below the other. */
			void				AttachedToWindow() override;
									/*!< Measures the items; the window is the target if
										none is set. */
			void				FrameResized(float newWidth, float newHeight) override;
			void				TargetedByScrollView(BScrollView* view) override;
			void				KeyDown(std::string_view bytes) override;
									/*!< Up, Down, Home, End select; Page Up and Down
										scroll; Enter and Space invoke. */
			void				MouseDown(gfx::Point where) override;
									/*!< Selects; a double click invokes. */
			void				MouseUp(gfx::Point where) override;
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< In a single selection list, the selection follows
										the pressed mouse. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< As wide as the widest item, as high as all. */
			gfx::Size			MinSize() override;
									/*!< 10 × 10. */
			gfx::Size			PreferredSize() override;
									/*!< 100 × 50. */
			void				MakeFocus(bool focused = true) override;
									/*!< Highlights the border of its scroll view. */

	virtual	bool				AddItem(std::unique_ptr<BListItem> item);
									/*!< Appends \a item. */
	virtual	bool				AddItem(std::unique_ptr<BListItem> item, int32 atIndex);
									/*!< Inserts \a item at \a atIndex. */
	virtual	bool				AddList(std::vector<std::unique_ptr<BListItem>> items,
									int32 atIndex);
									/*!< Inserts \a items at \a atIndex. */
	virtual	bool				AddList(std::vector<std::unique_ptr<BListItem>> items);
									/*!< Appends \a items. */
	virtual	std::unique_ptr<BListItem> RemoveItem(int32 index);
									/*!< Takes item \a index out and gives it back. */
	virtual	std::unique_ptr<BListItem> RemoveItem(BListItem* item);
									/*!< Takes \a item out and gives it back. */
	virtual	bool				RemoveItems(int32 index, int32 count);
									/*!< Takes \a count items out and destroys them. */

	virtual	void				SetSelectionMessage(std::unique_ptr<BMessage> message);
									/*!< Sent when the selection changes. */
	virtual	void				SetInvocationMessage(std::unique_ptr<BMessage> message);
									/*!< Sent when it is invoked (BInvoker's message). */
			BMessage*			SelectionMessage() const;
									/*!< The selection message, or nullptr. */
			uint32				SelectionCommand() const;
									/*!< Its what, or 0. */
			BMessage*			InvocationMessage() const;
									/*!< The invocation message, or nullptr. */
			uint32				InvocationCommand() const;
									/*!< Its what, or 0. */

	virtual	void				SetListType(BListViewType type);
									/*!< Single or multiple selection. */
			BListViewType		ListType() const;
									/*!< Single or multiple selection. */

			BListItem*			ItemAt(int32 index) const;
									/*!< Item \a index, or nullptr. */
			int32				IndexOf(gfx::Point point) const;
									/*!< The item at \a point, or -1. */
			int32				IndexOf(const BListItem* item) const;
									/*!< The index of \a item, or -1. */
			BListItem*			FirstItem() const;
									/*!< The first item, or nullptr. */
			BListItem*			LastItem() const;
									/*!< The last item, or nullptr. */
			bool				HasItem(const BListItem* item) const;
									/*!< Whether \a item is in the list. */
			int32				CountItems() const;
									/*!< The number of items. */
	virtual	void				MakeEmpty();
									/*!< Destroys all items. */
			bool				IsEmpty() const;
									/*!< Whether it has no items. */
			void				DoForEach(const std::function<bool(BListItem* item)>& function);
									/*!< Calls \a function with each item until it returns
										true. */

			void				InvalidateItem(int32 index);
									/*!< Redraws item \a index. */
			using				BView::ScrollTo;
			void				ScrollTo(int32 index);
									/*!< Scrolls so that item \a index is visible. */
			void				ScrollToSelection();
									/*!< Scrolls to the first selected item. */

			void				Select(int32 index, bool extend = false);
									/*!< Selects item \a index; without \a extend the others
										lose their selection. */
			void				Select(int32 from, int32 to, bool extend = false);
									/*!< Selects the items \a from to \a to. */
			bool				IsItemSelected(int32 index) const;
									/*!< Whether item \a index is selected. */
			int32				CurrentSelection(int32 index = 0) const;
									/*!< The \a index-th selected item, or -1. */
			std::expected<void, Error> Invoke(const BMessage* message = nullptr) override;
									/*!< Sends \a message (or the invocation message) with
										"when", "source" and the selected "index"es. */
			void				MessageReceived(BMessage* message) override;
									/*!< Scripting: counts "Item" and invokes items by
										index or range; counts, gets, sets and invokes the
										"Selection" (Haiku's properties); otherwise as
										BView's. */
			BHandler*			ResolveSpecifier(BMessage* message, std::int32_t index,
									const BMessage& specifier, std::uint32_t form,
									std::string_view property) override;
									/*!< Its scripting properties; otherwise as BView's. */
			std::expected<void, Error> GetSupportedSuites(BMessage* data) override;
									/*!< "suite/vnd.Be-list-view" and BView's. */

			void				DeselectAll();
									/*!< Deselects all items. */
			void				DeselectExcept(int32 exceptFrom, int32 exceptTo);
									/*!< Deselects all items but these. */
			void				Deselect(int32 index);
									/*!< Deselects item \a index. */
	virtual	void				SelectionChanged();
									/*!< Hook: the selection changed. */

	virtual	void				SortItems(int (*compare)(const BListItem*, const BListItem*));
									/*!< Sorts the items by \a compare (less than 0: the
										first comes first); deselects all. */
			bool				SwapItems(int32 a, int32 b);
									/*!< Swaps two items. */
			bool				MoveItem(int32 from, int32 to);
									/*!< Moves an item. */
			std::unique_ptr<BListItem> ReplaceItem(int32 index,
									std::unique_ptr<BListItem> item);
									/*!< Puts \a item in place of item \a index and gives
										that back; nullptr if \a index is invalid. */
			gfx::Rect			ItemFrame(int32 index);
									/*!< Where item \a index is drawn. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role List; Focus. */
			void				GetAccessibleItems(std::vector<AccessibleItem>* items)
									const override;
									/*!< Its items: ListItem, the text of a BStringItem as name,
										Selected, Disabled; Click. */
			std::expected<void, Error> PerformAccessibleItemAction(int32 index,
									AccessibleAction action, const BMessage& data) override;
									/*!< Click selects the item (alone), as a click does. */

protected:
	/*!	What DoMiscellaneous() is asked to do. */
	enum class MiscCode : std::uint8_t {
		NoOp,		/*!< B_NO_OP */
		Replace,	/*!< B_REPLACE_OP */
		Move,		/*!< B_MOVE_OP */
		Swap		/*!< B_SWAP_OP */
	};

	/*!	Its arguments (Haiku's MiscData, without the union). */
	struct MiscData {
		int32				a;		/*!< Index (Replace), from (Move), a (Swap). */
		int32				b;		/*!< to (Move), b (Swap). */
		std::unique_ptr<BListItem> item;
									/*!< The new item (Replace); the old one afterwards. */
	};

	virtual	bool				DoMiscellaneous(MiscCode code, MiscData* data);
									/*!< Replaces, moves or swaps items; BOutlineListView
										changes what that means. */
	virtual	void				DrawItem(BListItem* item, DrawContext& context,
									gfx::Rect itemRect, bool complete = false);
									/*!< Draws \a item in its text color. */

private:
	friend class BOutlineListView;

	// What MouseDown() starts (Haiku's track_data).
	struct TrackData {
		gfx::Point			dragStart;
		int32				itemIndex;
		uint32				selectedClickCount;
		bool				isActive;
		bool				wasSelected;
		std::optional<Clock::Time> lastClickTime;
	};

			void				_InitObject(BListViewType type);
			void				_FixupScrollBar();
			void				_InvalidateFrom(int32 index);
			void				_UpdateItems();
			bool				_Select(int32 index, bool extend);
			bool				_Select(int32 from, int32 to, bool extend);
			bool				_Deselect(int32 index);
			bool				_DeselectAll(int32 exceptFrom, int32 exceptTo);
			int32				_CalcFirstSelected(int32 after);
			int32				_CalcLastSelected(int32 before);
			void				_UpdateSelectionRange();
			void				_NotifySelection();
			bool				_SwapItems(int32 a, int32 b);
			bool				_MoveItem(int32 from, int32 to);
			std::unique_ptr<BListItem> _ReplaceItem(int32 index,
									std::unique_ptr<BListItem> item);
			void				_RecalcItemTops(int32 start, int32 end = -1);
			void				_DoSelection(int32 index);
			BModifierFlags		_Modifiers() const;

			std::vector<std::unique_ptr<BListItem>> fList;
			BListViewType		fListType = BListViewType::SingleSelection;
			int32				fFirstSelected = -1;
			int32				fLastSelected = -1;
			int32				fAnchorIndex = -1;
			std::unique_ptr<BMessage> fSelectMessage;
			BScrollView*		fScrollView = nullptr;
			TrackData			fTrack;
};


} // namespace ngui
