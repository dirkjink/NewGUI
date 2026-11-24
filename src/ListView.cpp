/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ListView.cpp (commit b1d497bb69),
 * without archiving, scripting and drag and drop. NewGUI (see ListView.h):
 * - The list owns its items (std::unique_ptr).
 * - Items get the text color as a parameter (Haiku: SetHighColor()) and
 *   the text system (Haiku: the view's font).
 * - Selection and invocation messages go through Invoke() (Haiku:
 *   InvokeNotify()); the view is redrawn whole.
 * - The click time is the Clock's, 500 ms (Haiku: get_click_speed()).
 * - Sizes are pixel counts; an item's frame is half-open, its Bottom() is
 *   Haiku's inclusive one. The vertical scroll range is all items' height
 *   less the view's (Haiku: one less), and _FixupScrollBar() does not
 *   scroll by itself (the scroll bar's range keeps its value in it).
 * - The first and last selected index are taken from the items after
 *   selecting a range and after moving, swapping and replacing items
 *   (_UpdateSelectionRange()); Haiku's bookkeeping lost track there (a
 *   disabled end of the range, selected items shifted by a move). The
 *   anchor follows a move like any index. RemoveItems() deselects first.
 *
 * Authors (Haiku):
 *		Ulrich Wimboeck
 *		Marc Flerackers (mflerackers@androme.be)
 *		Stephan Assmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent (rene@gollent.com)
 *		Stephan Aßmus, superstippi@gmx.de
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/ListView.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/ScrollBar.h>
#include <ngui/ScrollView.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "compat/InterfaceDefs.h"


namespace ngui {


using namespace std::chrono_literals;


namespace {


constexpr float kDoubleClickThreshold = 6.0f;
// Haiku's default click speed (get_click_speed())
constexpr Clock::Time kClickSpeed = 500ms;


} // namespace


BListView::BListView(gfx::Rect frame, std::string_view name, BListViewType type,
	BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitObject(type);
}


BListView::BListView(std::string_view name, BListViewType type, BViewFlags flags)
	:
	BView(name, flags)
{
	_InitObject(type);
}


BListView::~BListView()
{
}


// #pragma mark -


void
BListView::Draw(DrawContext& context)
{
	const int32 count = CountItems();
	if (count == 0)
		return;

	const gfx::Rect bounds = Bounds();
	for (int32 i = 0; i < count; i++) {
		BListItem* item = ItemAt(i);
		const gfx::Rect itemFrame = {bounds.x0, item->Top(), bounds.x1,
			item->Top() + std::ceil(item->Height())};
		if (itemFrame.y1 > bounds.y0 && itemFrame.y0 < bounds.y1)
			DrawItem(item, context, itemFrame);
	}
}


void
BListView::AttachedToWindow()
{
	BView::AttachedToWindow();
	_UpdateItems();

	if (!Messenger().IsValid())
		SetTarget(BMessenger(Window()));

	_FixupScrollBar();
}


void
BListView::FrameResized(float /*newWidth*/, float /*newHeight*/)
{
	_FixupScrollBar();
	_UpdateItems();
}


void
BListView::TargetedByScrollView(BScrollView* view)
{
	fScrollView = view;
	// TODO: We could SetFlags(Flags() | B_FRAME_EVENTS) here, but that
	// may break apps which override FrameResized() and rely on it not
	// being called
}


void
BListView::KeyDown(std::string_view bytes)
{
	if (bytes.empty())
		return;

	const bool extend = fListType == BListViewType::MultipleSelection
		&& _Modifiers().Has(BModifier::Shift);

	if (fFirstSelected == -1
		&& (bytes[0] == keys::kUpArrow || bytes[0] == keys::kDownArrow)) {
		// nothing is selected yet, select the first enabled item
		const int32 lastItem = CountItems() - 1;
		for (int32 i = 0; i <= lastItem; i++) {
			if (ItemAt(i)->IsEnabled()) {
				Select(i);
				break;
			}
		}
		return;
	}

	switch (bytes[0]) {
		case keys::kUpArrow:
		{
			if (fAnchorIndex > 0) {
				if (!extend || fAnchorIndex <= fFirstSelected) {
					for (int32 i = 1; fAnchorIndex - i >= 0; i++) {
						if (ItemAt(fAnchorIndex - i)->IsEnabled()) {
							// Select the previous enabled item
							Select(fAnchorIndex - i, extend);
							break;
						}
					}
				} else {
					Deselect(fAnchorIndex);
					do
						fAnchorIndex--;
					while (fAnchorIndex > 0 && !ItemAt(fAnchorIndex)->IsEnabled());
				}
			}

			ScrollToSelection();
			break;
		}

		case keys::kDownArrow:
		{
			const int32 lastItem = CountItems() - 1;
			if (fAnchorIndex < lastItem) {
				if (!extend || fAnchorIndex >= fLastSelected) {
					for (int32 i = 1; fAnchorIndex + i <= lastItem; i++) {
						if (ItemAt(fAnchorIndex + i)->IsEnabled()) {
							// Select the next enabled item
							Select(fAnchorIndex + i, extend);
							break;
						}
					}
				} else {
					Deselect(fAnchorIndex);
					do
						fAnchorIndex++;
					while (fAnchorIndex < lastItem && !ItemAt(fAnchorIndex)->IsEnabled());
				}
			}

			ScrollToSelection();
			break;
		}

		case keys::kHome:
			if (extend) {
				Select(0, fAnchorIndex, true);
				fAnchorIndex = 0;
			} else {
				// select the first enabled item
				const int32 lastItem = CountItems() - 1;
				for (int32 i = 0; i <= lastItem; i++) {
					if (ItemAt(i)->IsEnabled()) {
						Select(i, false);
						break;
					}
				}
			}

			ScrollToSelection();
			break;

		case keys::kEnd:
			if (extend) {
				Select(fAnchorIndex, CountItems() - 1, true);
				fAnchorIndex = CountItems() - 1;
			} else {
				// select the last enabled item
				for (int32 i = CountItems() - 1; i >= 0; i--) {
					if (ItemAt(i)->IsEnabled()) {
						Select(i, false);
						break;
					}
				}
			}

			ScrollToSelection();
			break;

		case keys::kPageUp:
		{
			gfx::Point scrollOffset{Bounds().x0, Bounds().y0};
			scrollOffset.y = std::max(0.0f, scrollOffset.y - Bounds().Height());
			ScrollTo(scrollOffset);
			break;
		}

		case keys::kPageDown:
		{
			gfx::Point scrollOffset{Bounds().x0, Bounds().y0};
			if (BListItem* item = LastItem()) {
				scrollOffset.y += Bounds().Height();
				// NewGUI: up to the end of the last item (Haiku's Bottom()
				// is inclusive)
				scrollOffset.y = std::min(item->Bottom() + 1 - Bounds().Height(),
					scrollOffset.y);
			}
			ScrollTo(scrollOffset);
			break;
		}

		case keys::kEnter:
		case keys::kSpace:
			(void)Invoke();
			break;

		default:
			BView::KeyDown(bytes);
	}
}


void
BListView::MouseDown(gfx::Point where)
{
	if (fTrack.isActive)
		return BView::MouseDown(where);

	if (!IsFocus())
		MakeFocus();

	const int32 index = IndexOf(where);
	const gfx::Point delta = {where.x - fTrack.dragStart.x, where.y - fTrack.dragStart.y};
	const Clock::Time now = Clock::Default().Now();

	// was this a double click?
	bool doubleClick = false;
	if (fTrack.lastClickTime.has_value() && now - *fTrack.lastClickTime < kClickSpeed
		&& std::fabs(delta.x) < kDoubleClickThreshold
		&& std::fabs(delta.y) < kDoubleClickThreshold
		&& fTrack.itemIndex == index) {
		doubleClick = true;
	}

	if (doubleClick && index >= fFirstSelected && index <= fLastSelected) {
		fTrack.dragStart = {1e9f, 1e9f};
		fTrack.lastClickTime.reset();
		(void)Invoke();
		return BView::MouseDown(where);
	}

	if (!doubleClick) {
		fTrack.isActive = true;
		fTrack.dragStart = where;
		fTrack.lastClickTime = now;
		fTrack.itemIndex = index;
		fTrack.wasSelected = index >= 0 ? ItemAt(index)->IsSelected() : false;
		SetMouseEventMask(BEventMask::Pointer);
	}

	if (fTrack.wasSelected)
		fTrack.selectedClickCount++;
	else
		fTrack.selectedClickCount = 0;

	_DoSelection(index);

	BView::MouseDown(where);
}


void
BListView::MouseUp(gfx::Point where)
{
	if (!fTrack.isActive)
		return BView::MouseUp(where);

	fTrack.isActive = false;

	if (fListType == BListViewType::MultipleSelection)
		return BView::MouseUp(where);

	int32 index = IndexOf(where);
	if (index == fTrack.itemIndex)
		return BView::MouseUp(where);

	if (index == -1)
		index = fTrack.itemIndex;

	if (index == -1)
		return BView::MouseUp(where);

	BListItem* item = ItemAt(index);
	if (item != nullptr) {
		item->Deselect();
		_DoSelection(index);
	}

	BView::MouseUp(where);
}


void
BListView::MouseMoved(gfx::Point where, BMouseTransit transit, const BMessage* dragMessage)
{
	if (!fTrack.isActive)
		return BView::MouseMoved(where, transit, dragMessage);

	// NewGUI: no drag and drop (Haiku's InitiateDrag())

	int32 index = IndexOf(where);
	if (index == -1) {
		// If where is above top, scroll to the first item,
		// else if where is below bottom, scroll to the last item.
		if (where.y < Bounds().y0)
			index = 0;
		else if (where.y >= Bounds().y1)
			index = CountItems() - 1;
	}

	const int32 lastIndex = fFirstSelected;
	if (index == -1)
		return BView::MouseMoved(where, transit, dragMessage);

	if (where.x < Bounds().x0 || where.x >= Bounds().x1)
		return BView::MouseMoved(where, transit, dragMessage);

	ScrollTo(index);

	if (fListType != BListViewType::MultipleSelection && lastIndex != -1
		&& index != lastIndex) {
		BListItem* last = ItemAt(lastIndex);
		BListItem* item = ItemAt(index);
		if (last != nullptr && item != nullptr && item->IsEnabled()) {
			last->Deselect();
			item->Select();

			// update selection index
			fFirstSelected = fLastSelected = index;

			// redraw items whose selection has changed
			Invalidate();
		}
	} else
		Invalidate();

	BView::MouseMoved(where, transit, dragMessage);
}


void
BListView::GetPreferredSize(float* width, float* height)
{
	const int32 count = CountItems();

	if (count > 0) {
		float maxWidth = 0.0f;
		for (int32 i = 0; i < count; i++) {
			const float itemWidth = ItemAt(i)->Width();
			if (itemWidth > maxWidth)
				maxWidth = itemWidth;
		}

		if (width != nullptr)
			*width = maxWidth;
		if (height != nullptr) {
			// NewGUI: a pixel count
			*height = ItemAt(count - 1)->Bottom() + 1;
		}
	} else
		BView::GetPreferredSize(width, height);
}


gfx::Size
BListView::MinSize()
{
	// We need a stable min size: the BView implementation uses
	// GetPreferredSize(), which by default just returns the current size.
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), {10, 10});
}


gfx::Size
BListView::PreferredSize()
{
	// We need a stable preferred size: the BView implementation uses
	// GetPreferredSize(), which by default just returns the current size.
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), {100, 50});
}


void
BListView::MakeFocus(bool focused)
{
	if (IsFocus() == focused)
		return;

	BView::MakeFocus(focused);

	if (fScrollView != nullptr)
		(void)fScrollView->SetBorderHighlighted(focused);
}


// #pragma mark - items


bool
BListView::AddItem(std::unique_ptr<BListItem> item, int32 index)
{
	if (item == nullptr || index < 0 || index > CountItems())
		return false;

	BListItem* added = item.get();
	fList.insert(fList.begin() + index, std::move(item));

	if (fFirstSelected != -1 && index <= fFirstSelected)
		fFirstSelected++;

	if (fLastSelected != -1 && index <= fLastSelected)
		fLastSelected++;

	if (fAnchorIndex != -1 && index <= fAnchorIndex)
		fAnchorIndex++;

	if (Window() != nullptr) {
		added->SetTop((index > 0) ? ItemAt(index - 1)->Bottom() + 1.0f : 0.0f);

		added->Update(this, TextSystem::Default());
		_RecalcItemTops(index + 1);

		_FixupScrollBar();
		_InvalidateFrom(index);
	}

	return true;
}


bool
BListView::AddItem(std::unique_ptr<BListItem> item)
{
	return AddItem(std::move(item), CountItems());
}


bool
BListView::AddList(std::vector<std::unique_ptr<BListItem>> items, int32 index)
{
	if (index < 0 || index > CountItems())
		return false;
	for (const std::unique_ptr<BListItem>& item : items) {
		if (item == nullptr)
			return false;
	}

	const auto count = static_cast<int32>(items.size());
	fList.insert(fList.begin() + index, std::make_move_iterator(items.begin()),
		std::make_move_iterator(items.end()));

	if (fFirstSelected != -1 && index < fFirstSelected)
		fFirstSelected += count;

	if (fLastSelected != -1 && index < fLastSelected)
		fLastSelected += count;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex += count;

	if (Window() != nullptr) {
		for (int32 i = index; i <= (index + count - 1); i++) {
			ItemAt(i)->SetTop((i > 0) ? ItemAt(i - 1)->Bottom() + 1.0f : 0.0f);
			ItemAt(i)->Update(this, TextSystem::Default());
		}

		_RecalcItemTops(index + count - 1);

		_FixupScrollBar();
		Invalidate(); // TODO
	}

	return true;
}


bool
BListView::AddList(std::vector<std::unique_ptr<BListItem>> items)
{
	return AddList(std::move(items), CountItems());
}


std::unique_ptr<BListItem>
BListView::RemoveItem(int32 index)
{
	BListItem* item = ItemAt(index);
	if (item == nullptr)
		return nullptr;

	if (item->IsSelected())
		Deselect(index);

	std::unique_ptr<BListItem> removed = std::move(fList[static_cast<std::size_t>(index)]);
	fList.erase(fList.begin() + index);

	if (fFirstSelected != -1 && index < fFirstSelected)
		fFirstSelected--;

	if (fLastSelected != -1 && index < fLastSelected)
		fLastSelected--;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex--;

	_RecalcItemTops(index);

	_InvalidateFrom(index);
	_FixupScrollBar();

	return removed;
}


std::unique_ptr<BListItem>
BListView::RemoveItem(BListItem* item)
{
	return BListView::RemoveItem(IndexOf(item));
}


bool
BListView::RemoveItems(int32 index, int32 count)
{
	if (index >= CountItems())
		index = -1;

	if (index < 0 || count < 0)
		return false;

	if (fAnchorIndex != -1 && index < fAnchorIndex)
		fAnchorIndex = index;

	// NewGUI: the selection is deselected first (Haiku kept stale indices)
	if (_DeselectAll(-1, -1))
		_NotifySelection();

	count = std::min(count, CountItems() - index);
	fList.erase(fList.begin() + index, fList.begin() + index + count);
	if (index < CountItems())
		_RecalcItemTops(index);

	Invalidate();
	_FixupScrollBar();

	return true;
}


void
BListView::SetSelectionMessage(std::unique_ptr<BMessage> message)
{
	fSelectMessage = std::move(message);
}


void
BListView::SetInvocationMessage(std::unique_ptr<BMessage> message)
{
	BInvoker::SetMessage(std::move(message));
}


BMessage*
BListView::InvocationMessage() const
{
	return BInvoker::Message();
}


uint32
BListView::InvocationCommand() const
{
	return BInvoker::Command();
}


BMessage*
BListView::SelectionMessage() const
{
	return fSelectMessage.get();
}


uint32
BListView::SelectionCommand() const
{
	if (fSelectMessage != nullptr)
		return fSelectMessage->what;

	return 0;
}


void
BListView::SetListType(BListViewType type)
{
	if (fListType == BListViewType::MultipleSelection
		&& type == BListViewType::SingleSelection) {
		Select(CurrentSelection(0));
	}

	fListType = type;
}


BListViewType
BListView::ListType() const
{
	return fListType;
}


BListItem*
BListView::ItemAt(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return nullptr;
	return fList[static_cast<std::size_t>(index)].get();
}


int32
BListView::IndexOf(const BListItem* item) const
{
	if (item == nullptr)
		return -1;
	for (std::size_t i = 0; i < fList.size(); i++) {
		if (fList[i].get() == item)
			return static_cast<int32>(i);
	}
	return -1;
}


int32
BListView::IndexOf(gfx::Point point) const
{
	int32 low = 0;
	int32 high = CountItems() - 1;

	// binary search the list
	while (high >= low) {
		const int32 mid = (low + high) / 2;
		const float frameTop = ItemAt(mid)->Top();
		// NewGUI: half-open
		const float frameBottom = ItemAt(mid)->Bottom() + 1;
		if (point.y < frameTop)
			high = mid - 1;
		else if (point.y >= frameBottom)
			low = mid + 1;
		else
			return mid;
	}

	return -1;
}


BListItem*
BListView::FirstItem() const
{
	return fList.empty() ? nullptr : fList.front().get();
}


BListItem*
BListView::LastItem() const
{
	return fList.empty() ? nullptr : fList.back().get();
}


bool
BListView::HasItem(const BListItem* item) const
{
	return IndexOf(item) != -1;
}


int32
BListView::CountItems() const
{
	return static_cast<int32>(fList.size());
}


void
BListView::MakeEmpty()
{
	if (fList.empty())
		return;

	if (_DeselectAll(-1, -1))
		_NotifySelection();

	fList.clear();
	fAnchorIndex = -1;

	if (Window() != nullptr) {
		_FixupScrollBar();
		Invalidate();
	}
}


bool
BListView::IsEmpty() const
{
	return fList.empty();
}


void
BListView::DoForEach(const std::function<bool(BListItem* item)>& function)
{
	for (const std::unique_ptr<BListItem>& item : fList) {
		if (function(item.get()))
			break;
	}
}


void
BListView::InvalidateItem(int32 /*index*/)
{
	// NewGUI: views are redrawn whole
	Invalidate();
}


void
BListView::ScrollTo(int32 index)
{
	if (index < 0)
		index = 0;
	if (index > CountItems() - 1)
		index = CountItems() - 1;
	if (index < 0)
		return;

	const gfx::Rect itemFrame = ItemFrame(index);
	const gfx::Rect bounds = Bounds();
	if (itemFrame.y0 < bounds.y0)
		BView::ScrollTo({itemFrame.x0, itemFrame.y0});
	else if (itemFrame.y1 > bounds.y1)
		BView::ScrollTo({0, itemFrame.y1 - bounds.Height()});
}


void
BListView::ScrollToSelection()
{
	const int32 selection = CurrentSelection(0);
	if (selection < 0)
		return;

	const gfx::Rect itemFrame = ItemFrame(selection);
	const gfx::Rect bounds = Bounds();
	if (itemFrame.y0 < bounds.y0 || itemFrame.Height() > bounds.Height())
		ScrollBy(0, itemFrame.y0 - bounds.y0);
	else if (itemFrame.y1 > bounds.y1)
		ScrollBy(0, itemFrame.y1 - bounds.y1);
}


void
BListView::Select(int32 index, bool extend)
{
	if (_Select(index, extend))
		_NotifySelection();
}


void
BListView::Select(int32 start, int32 finish, bool extend)
{
	if (_Select(start, finish, extend))
		_NotifySelection();
}


bool
BListView::IsItemSelected(int32 index) const
{
	BListItem* item = ItemAt(index);
	if (item != nullptr)
		return item->IsSelected();

	return false;
}


int32
BListView::CurrentSelection(int32 index) const
{
	if (fFirstSelected == -1)
		return -1;

	if (index == 0)
		return fFirstSelected;

	for (int32 i = fFirstSelected; i <= fLastSelected; i++) {
		if (ItemAt(i)->IsSelected()) {
			if (index == 0)
				return i;

			index--;
		}
	}

	return -1;
}


std::expected<void, Error>
BListView::Invoke(const BMessage* message)
{
	if (message == nullptr)
		message = Message();
	if (message == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to send");

	BMessage clone = *message;
	(void)clone.AddInt64("when", Clock::Default().Now().count());
	(void)clone.AddPointer("source", this);

	if (fListType == BListViewType::SingleSelection)
		(void)clone.AddInt32("index", fFirstSelected);
	else {
		if (fFirstSelected >= 0) {
			for (int32 i = fFirstSelected; i <= fLastSelected; i++) {
				if (ItemAt(i)->IsSelected())
					(void)clone.AddInt32("index", i);
			}
		}
	}

	return BInvoker::Invoke(&clone);
}


void
BListView::DeselectAll()
{
	if (_DeselectAll(-1, -1))
		_NotifySelection();
}


void
BListView::DeselectExcept(int32 exceptFrom, int32 exceptTo)
{
	if (exceptFrom > exceptTo || exceptFrom < 0 || exceptTo < 0)
		return;

	if (_DeselectAll(exceptFrom, exceptTo))
		_NotifySelection();
}


void
BListView::Deselect(int32 index)
{
	if (_Deselect(index))
		_NotifySelection();
}


void
BListView::SelectionChanged()
{
	// Hook method to be implemented by subclasses
}


void
BListView::SortItems(int (*compare)(const BListItem*, const BListItem*))
{
	if (_DeselectAll(-1, -1))
		_NotifySelection();

	std::stable_sort(fList.begin(), fList.end(),
		[compare](const std::unique_ptr<BListItem>& a, const std::unique_ptr<BListItem>& b) {
			return compare(a.get(), b.get()) < 0;
		});

	_RecalcItemTops(0);
	Invalidate();
}


bool
BListView::SwapItems(int32 a, int32 b)
{
	MiscData data{a, b, nullptr};
	return DoMiscellaneous(MiscCode::Swap, &data);
}


bool
BListView::MoveItem(int32 from, int32 to)
{
	MiscData data{from, to, nullptr};
	return DoMiscellaneous(MiscCode::Move, &data);
}


std::unique_ptr<BListItem>
BListView::ReplaceItem(int32 index, std::unique_ptr<BListItem> item)
{
	MiscData data{index, 0, std::move(item)};
	if (!DoMiscellaneous(MiscCode::Replace, &data))
		return nullptr;
	return std::move(data.item);
}


gfx::Rect
BListView::ItemFrame(int32 index)
{
	gfx::Rect frame = Bounds();
	if (index < 0 || index >= CountItems()) {
		frame.y0 = 0;
		frame.y1 = 0;
	} else {
		BListItem* item = ItemAt(index);
		frame.y0 = item->Top();
		frame.y1 = item->Bottom() + 1;
	}
	return frame;
}


// #pragma mark - protected


bool
BListView::DoMiscellaneous(MiscCode code, MiscData* data)
{
	switch (code) {
		case MiscCode::NoOp:
			break;

		case MiscCode::Replace:
		{
			std::unique_ptr<BListItem> old = _ReplaceItem(data->a, std::move(data->item));
			if (old == nullptr)
				return false;
			data->item = std::move(old);
			return true;
		}

		case MiscCode::Move:
			return _MoveItem(data->a, data->b);

		case MiscCode::Swap:
			return _SwapItems(data->a, data->b);
	}

	return false;
}


void
BListView::DrawItem(BListItem* item, DrawContext& context, gfx::Rect itemRect, bool complete)
{
	gfx::Color textColor;
	if (!item->IsEnabled()) {
		const compat::rgb_color text = compat::ToRgbColor(
			context.palette.Get(BColorWhich::ListItemText));
		compat::rgb_color disabledColor;
		if (text.red + text.green + text.blue > 128 * 3)
			disabledColor = compat::tint_color(text, compat::B_DARKEN_2_TINT);
		else
			disabledColor = compat::tint_color(text, compat::B_LIGHTEN_2_TINT);
		textColor = compat::ToColor(disabledColor);
	} else if (item->IsSelected())
		textColor = context.palette.Get(BColorWhich::ListSelectedItemText);
	else
		textColor = context.palette.Get(BColorWhich::ListItemText);

	item->DrawItem(this, context, itemRect, textColor, complete);
}


// #pragma mark - private


void
BListView::_InitObject(BListViewType type)
{
	fListType = type;
	fFirstSelected = -1;
	fLastSelected = -1;
	fAnchorIndex = -1;
	fScrollView = nullptr;

	fTrack = TrackData{{0, 0}, -1, 0, false, false, std::nullopt};

	SetViewColor(Palette::Default().Get(BColorWhich::ListBackground));
}


void
BListView::_FixupScrollBar()
{
	BScrollBar* vertScroller = ScrollBar(BOrientation::Vertical);
	if (vertScroller != nullptr) {
		const gfx::Rect bounds = Bounds();
		const int32 count = CountItems();

		// NewGUI: the pixel count of all items
		float itemHeight = 0.0f;
		if (count > 0)
			itemHeight = ItemAt(count - 1)->Bottom() + 1;

		if (bounds.Height() >= itemHeight) {
			// no scrolling
			vertScroller->SetRange(0.0f, 0.0f);
			vertScroller->SetValue(0.0f);
		} else {
			vertScroller->SetRange(0.0f, itemHeight - bounds.Height());
			vertScroller->SetProportion(bounds.Height() / itemHeight);
		}

		if (count != 0)
			vertScroller->SetSteps(std::ceil(FirstItem()->Height()), bounds.Height());
	}

	BScrollBar* horizontalScroller = ScrollBar(BOrientation::Horizontal);
	if (horizontalScroller != nullptr) {
		float w;
		GetPreferredSize(&w, nullptr);
		const gfx::Rect scrollBarSize = horizontalScroller->Bounds();

		if (w <= scrollBarSize.Width()) {
			// no scrolling
			horizontalScroller->SetRange(0.0f, 0.0f);
			horizontalScroller->SetValue(0.0f);
		} else {
			horizontalScroller->SetRange(0, w - scrollBarSize.Width());
			horizontalScroller->SetProportion(scrollBarSize.Width() / w);
		}
	}
}


void
BListView::_InvalidateFrom(int32 /*index*/)
{
	// NewGUI: views are redrawn whole
	Invalidate();
}


void
BListView::_UpdateItems()
{
	const TextSystem& text = TextSystem::Default();
	for (int32 i = 0; i < CountItems(); i++) {
		ItemAt(i)->SetTop((i > 0) ? ItemAt(i - 1)->Bottom() + 1.0f : 0.0f);
		ItemAt(i)->Update(this, text);
	}
}


bool
BListView::_Select(int32 index, bool extend)
{
	if (index < 0 || index >= CountItems())
		return false;

	bool changed = false;

	if (!extend && fFirstSelected != -1)
		changed = _DeselectAll(index, index);

	fAnchorIndex = index;

	BListItem* item = ItemAt(index);
	if (!item->IsEnabled() || item->IsSelected()) {
		// if the item is already selected, or can't be selected,
		// we're done here
		return changed;
	}

	// keep track of first and last selected item
	if (fFirstSelected == -1) {
		// no previous selection
		fFirstSelected = index;
		fLastSelected = index;
	} else if (index < fFirstSelected) {
		fFirstSelected = index;
	} else if (index > fLastSelected) {
		fLastSelected = index;
	}

	item->Select();
	if (Window() != nullptr)
		InvalidateItem(index);

	return true;
}


bool
BListView::_Select(int32 from, int32 to, bool extend)
{
	if (to < from)
		return false;

	// NewGUI: within the list
	from = std::max(from, 0);
	to = std::min(to, CountItems() - 1);
	if (to < from)
		return false;

	bool changed = false;

	if (fFirstSelected != -1 && !extend)
		changed = _DeselectAll(from, to);

	for (int32 i = from; i <= to; ++i) {
		BListItem* item = ItemAt(i);
		if (item != nullptr && !item->IsSelected() && item->IsEnabled()) {
			item->Select();
			if (Window() != nullptr)
				InvalidateItem(i);
			changed = true;
		}
	}

	// NewGUI: the first and last item that are selected (Haiku took from
	// and to, even when disabled)
	_UpdateSelectionRange();

	return changed;
}


bool
BListView::_Deselect(int32 index)
{
	if (index < 0 || index >= CountItems())
		return false;

	BListItem* item = ItemAt(index);

	if (item != nullptr && item->IsSelected()) {
		item->Deselect();

		if (fFirstSelected == index && fLastSelected == index) {
			fFirstSelected = -1;
			fLastSelected = -1;
		} else {
			if (fFirstSelected == index)
				fFirstSelected = _CalcFirstSelected(index);

			if (fLastSelected == index)
				fLastSelected = _CalcLastSelected(index);
		}

		if (Window() != nullptr)
			InvalidateItem(index);
	}

	return true;
}


bool
BListView::_DeselectAll(int32 exceptFrom, int32 exceptTo)
{
	if (fFirstSelected == -1)
		return false;

	bool changed = false;

	for (int32 index = fFirstSelected; index <= fLastSelected; index++) {
		// don't deselect the items we shouldn't deselect
		if (exceptFrom != -1 && exceptFrom <= index && exceptTo >= index)
			continue;

		BListItem* item = ItemAt(index);
		if (item != nullptr && item->IsSelected()) {
			item->Deselect();
			InvalidateItem(index);
			changed = true;
		}
	}

	if (!changed)
		return false;

	if (exceptFrom != -1) {
		fFirstSelected = _CalcFirstSelected(exceptFrom);
		fLastSelected = _CalcLastSelected(exceptTo);
	} else
		fFirstSelected = fLastSelected = -1;

	return true;
}


int32
BListView::_CalcFirstSelected(int32 after)
{
	if (after >= CountItems())
		return -1;

	const int32 count = CountItems();
	for (int32 i = std::max(after, 0); i < count; i++) {
		if (ItemAt(i)->IsSelected())
			return i;
	}

	return -1;
}


int32
BListView::_CalcLastSelected(int32 before)
{
	if (before < 0)
		return -1;

	before = std::min(CountItems() - 1, before);

	for (int32 i = before; i >= 0; i--) {
		if (ItemAt(i)->IsSelected())
			return i;
	}

	return -1;
}


void
BListView::_UpdateSelectionRange()
{
	fFirstSelected = _CalcFirstSelected(0);
	fLastSelected = _CalcLastSelected(CountItems() - 1);
}


void
BListView::_NotifySelection()
{
	SelectionChanged();
	if (fSelectMessage != nullptr)
		(void)Invoke(fSelectMessage.get());
}


bool
BListView::_SwapItems(int32 a, int32 b)
{
	// remember frames of items before anything happens,
	// the tricky situation is when the two items have
	// a different height
	const gfx::Rect aFrame = ItemFrame(a);
	const gfx::Rect bFrame = ItemFrame(b);

	if (ItemAt(a) == nullptr || ItemAt(b) == nullptr)
		return false;

	std::swap(fList[static_cast<std::size_t>(a)], fList[static_cast<std::size_t>(b)]);

	if (a == b) {
		// nothing to do, but success nevertheless
		return true;
	}

	// track anchor item
	if (fAnchorIndex == a)
		fAnchorIndex = b;
	else if (fAnchorIndex == b)
		fAnchorIndex = a;

	// track selection
	// NOTE: this is only important if the selection status
	// of both items is not the same
	const int32 first = std::min(a, b);
	const int32 last = std::max(a, b);
	if (ItemAt(a)->IsSelected() != ItemAt(b)->IsSelected()) {
		// NewGUI: from the items (Haiku rescanned only beyond the range)
		_UpdateSelectionRange();
		// though the actually selected items stayed the
		// same, the selection has still changed
		SelectionChanged();
	}

	ItemAt(a)->SetTop(aFrame.y0);
	ItemAt(b)->SetTop(bFrame.y0);

	// take care of invalidation
	if (aFrame.Height() != bFrame.Height())
		_RecalcItemTops(first, last);

	if (Window() != nullptr)
		Invalidate();

	return true;
}


bool
BListView::_MoveItem(int32 from, int32 to)
{
	if (ItemAt(from) == nullptr || to < 0 || to >= CountItems())
		return false;

	std::unique_ptr<BListItem> moved = std::move(fList[static_cast<std::size_t>(from)]);
	fList.erase(fList.begin() + from);
	fList.insert(fList.begin() + to, std::move(moved));

	// track anchor item
	// NewGUI: also when the items between from and to shift
	if (fAnchorIndex == from)
		fAnchorIndex = to;
	else if (from < fAnchorIndex && fAnchorIndex <= to)
		fAnchorIndex--;
	else if (to <= fAnchorIndex && fAnchorIndex < from)
		fAnchorIndex++;

	// track selection
	// NewGUI: the selected items between from and to shift as well (Haiku
	// rescanned only when the moved item was selected)
	_UpdateSelectionRange();
	if (ItemAt(to)->IsSelected()) {
		// though the actually selected items stayed the
		// same, the selection has still changed
		SelectionChanged();
	}

	_RecalcItemTops((to > from) ? from : to);

	// take care of invalidation
	if (Window() != nullptr)
		Invalidate();

	return true;
}


std::unique_ptr<BListItem>
BListView::_ReplaceItem(int32 index, std::unique_ptr<BListItem> item)
{
	if (item == nullptr)
		return nullptr;

	BListItem* old = ItemAt(index);
	if (old == nullptr)
		return nullptr;

	const gfx::Rect frame = ItemFrame(index);

	const bool selectionChanged = old->IsSelected() != item->IsSelected();

	// replace item
	std::unique_ptr<BListItem> replaced = std::move(fList[static_cast<std::size_t>(index)]);
	fList[static_cast<std::size_t>(index)] = std::move(item);
	if (Window() != nullptr)
		ItemAt(index)->Update(this, TextSystem::Default());

	// tack selection
	if (selectionChanged) {
		_UpdateSelectionRange();
		SelectionChanged();
	}
	_RecalcItemTops(index);

	const bool itemHeightChanged = frame != ItemFrame(index);

	// take care of invalidation
	if (Window() != nullptr)
		Invalidate();

	if (itemHeightChanged)
		_FixupScrollBar();

	return replaced;
}



void
BListView::_RecalcItemTops(int32 start, int32 end)
{
	int32 count = CountItems();
	if ((start < 0) || (start >= count))
		return;

	if (end >= 0)
		count = end + 1;

	float top = (start == 0) ? 0.0f : ItemAt(start - 1)->Bottom() + 1.0f;

	for (int32 i = start; i < count; i++) {
		BListItem* item = ItemAt(i);
		item->SetTop(top);
		top += std::ceil(item->Height());
	}
}


void
BListView::_DoSelection(int32 index)
{
	BListItem* item = ItemAt(index);

	// don't alter selection if invalid item clicked
	if (index < 0 || item == nullptr)
		return;

	// deselect all items if disabled item clicked
	if (!item->IsEnabled())
		return DeselectAll();

	const BModifierFlags modifiers = _Modifiers();
	if (fListType == BListViewType::MultipleSelection) {
		// multiple-selection list

		if (modifiers.Has(BModifier::Shift)) {
			if (index >= fFirstSelected && index < fLastSelected) {
				// clicked inside of selected items block, deselect all
				// except from the first selected index to item index
				DeselectExcept(fFirstSelected, index);
			} else {
				// extend or contract selection
				Select(std::min(index, fFirstSelected == -1 ? index : fFirstSelected),
					std::max(index, fLastSelected));
			}
		} else if (modifiers.Has(BModifier::Command)) {
			// toggle selection state
			if (item->IsSelected())
				Deselect(index);
			else
				Select(index, true);
		} else if (fTrack.selectedClickCount != 1)
			Select(index); // eat a click on selected for drag and drop
	} else {
		// single-selection list

		// toggle selection state
		if (modifiers.Has(BModifier::Command) && item->IsSelected())
			Deselect(index);
		else
			Select(index);
	}
}


BModifierFlags
BListView::_Modifiers() const
{
	// Haiku's modifiers(): those of the event being handled
	const BMessage* current = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	return BModifierFlags::FromRaw(static_cast<uint32>(
		current != nullptr ? current->GetInt32("modifiers", 0) : 0));
}


} // namespace ngui
