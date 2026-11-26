/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/OutlineListView.cpp (commit
 * b1d497bb69), without archiving and scripting. NewGUI (see
 * OutlineListView.h):
 * - The visible items are owned by the BListView, the hidden ones by
 *   fHidden; expanding and collapsing moves them between both.
 * - The first and last selected index are taken from the items
 *   (BListView::_UpdateSelectionRange()) after expanding, collapsing,
 *   swapping and sorting.
 * - Expanding a hidden item only marks it expanded (Haiku inserted its
 *   subitems at the top of the visible list).
 * - Sorting reorders the full list block by block (Haiku: a temporary
 *   tree of BLists) and the visible items by their place in it.
 * - The latch is Haiku's size: the plain font's size plus one pixel; the
 *   item is drawn from its inclusive right edge on, as in Haiku.
 *
 * Authors (Haiku):
 *		Axel Dörfler, axeld@pinc-software.de
 *		Rene Gollent (rene@gollent.com)
 *		Philippe Saint-Pierre, stpere@gmail.com
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/OutlineListView.h>

#include <algorithm>
#include <cstddef>
#include <utility>

#include <ngui/ControlLook.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>

#include "compat/InterfaceDefs.h"


namespace ngui {


namespace {


// Turns A M B, the ranges [a, aEnd), [aEnd, b) and [b, bEnd), into B M A.
template<typename T>
void
SwapBlocks(std::vector<T>& list, std::size_t a, std::size_t aEnd, std::size_t b,
	std::size_t bEnd)
{
	const auto begin = list.begin();
	const auto aSize = static_cast<std::ptrdiff_t>(aEnd - a);
	const auto bSize = static_cast<std::ptrdiff_t>(bEnd - b);
	const auto first = begin + static_cast<std::ptrdiff_t>(a);
	// A M B -> B A M
	std::rotate(first, begin + static_cast<std::ptrdiff_t>(b),
		begin + static_cast<std::ptrdiff_t>(bEnd));
	// B A M -> B M A
	std::rotate(first + bSize, first + bSize + aSize, begin + static_cast<std::ptrdiff_t>(bEnd));
}


} // namespace


BOutlineListView::BOutlineListView(gfx::Rect frame, std::string_view name, BListViewType type,
	BResizingMode resizingMode, BViewFlags flags)
	:
	BListView(frame, name, type, resizingMode, flags)
{
}


BOutlineListView::BOutlineListView(std::string_view name, BListViewType type,
	BViewFlags flags)
	:
	BListView(name, type, flags)
{
}


BOutlineListView::~BOutlineListView()
{
}


void
BOutlineListView::MouseDown(gfx::Point where)
{
	MakeFocus();

	const int32 index = IndexOf(where);
	if (index != -1) {
		BListItem* item = ItemAt(index);

		if (item->fHasSubitems
			&& LatchRect(ItemFrame(index), static_cast<int32>(item->fLevel)).Contains(where)) {
			if (item->IsExpanded())
				Collapse(item);
			else
				Expand(item);
		} else
			BListView::MouseDown(where);
	}
}


void
BOutlineListView::KeyDown(std::string_view bytes)
{
	if (bytes.size() == 1) {
		const int32 currentSel = CurrentSelection();
		switch (bytes[0]) {
			case keys::kRightArrow:
			{
				BListItem* item = ItemAt(currentSel);
				if (item != nullptr && item->fHasSubitems) {
					if (!item->IsExpanded())
						Expand(item);
					else {
						Select(currentSel + 1);
						ScrollToSelection();
					}
				}
				return;
			}

			case keys::kLeftArrow:
			{
				BListItem* item = ItemAt(currentSel);
				if (item != nullptr) {
					if (item->fHasSubitems && item->IsExpanded())
						Collapse(item);
					else {
						item = Superitem(item);
						if (item != nullptr) {
							Select(IndexOf(item));
							ScrollToSelection();
						}
					}
				}
				return;
			}
		}
	}

	BListView::KeyDown(bytes);
}


bool
BOutlineListView::AddUnder(std::unique_ptr<BListItem> item, BListItem* superItem)
{
	if (superItem == nullptr)
		return AddItem(std::move(item));
	if (item == nullptr)
		return false;

	const int32 superIndex = FullListIndexOf(superItem);
	if (superIndex < 0)
		return false;

	BListItem* added = item.get();
	fFullList.insert(fFullList.begin() + superIndex + 1, added);

	added->fLevel = superItem->OutlineLevel() + 1;
	superItem->fHasSubitems = true;

	// NewGUI: as the first subitem it becomes the superitem of deeper
	// subitems that follow
	const BListItem* next = FullListItemAt(superIndex + 2);
	if (next != nullptr && next->fLevel > added->fLevel) {
		added->SetItemVisible(false);
		fHidden.emplace(added, std::move(item));
		_Restructure();
		return true;
	}

	if (superItem->IsItemVisible() && superItem->IsExpanded()) {
		added->SetItemVisible(true);

		const int32 index = BListView::IndexOf(superItem);

		(void)BListView::AddItem(std::move(item), index + 1);
		Invalidate();
	} else {
		added->SetItemVisible(false);
		fHidden.emplace(added, std::move(item));
	}

	return true;
}


bool
BOutlineListView::AddItem(std::unique_ptr<BListItem> item)
{
	return AddItem(std::move(item), FullListCountItems());
}


bool
BOutlineListView::AddItem(std::unique_ptr<BListItem> item, int32 fullListIndex)
{
	if (item == nullptr)
		return false;

	if (fullListIndex < 0)
		fullListIndex = 0;
	else if (fullListIndex > FullListCountItems())
		fullListIndex = FullListCountItems();

	BListItem* added = item.get();
	fFullList.insert(fFullList.begin() + fullListIndex, added);

	// NewGUI: inserted before deeper items, it becomes their superitem;
	// what is visible and which items have subitems is worked out anew
	const BListItem* next = FullListItemAt(fullListIndex + 1);
	if (next != nullptr && next->fLevel > added->fLevel) {
		added->SetItemVisible(false);
		fHidden.emplace(added, std::move(item));
		_Restructure();
		return true;
	}

	if (added->fLevel > 0) {
		BListItem* super = _SuperitemForIndex(fullListIndex, added->fLevel);
		if (super != nullptr) {
			const bool hadSubitems = super->fHasSubitems;
			super->fHasSubitems = true;

			if (!super->IsItemVisible() || !super->IsExpanded()) {
				added->SetItemVisible(false);
				fHidden.emplace(added, std::move(item));
				return true;
			}

			if (!hadSubitems)
				Invalidate();
		}
	}

	added->SetItemVisible(true);

	const int32 listIndex = _FindPreviousVisibleIndex(fullListIndex);
	const int32 index = listIndex < 0 ? 0 : IndexOf(FullListItemAt(listIndex)) + 1;
	if (!BListView::AddItem(std::move(item), index)) {
		fFullList.erase(fFullList.begin() + fullListIndex);
		return false;
	}

	return true;
}


bool
BOutlineListView::AddList(std::vector<std::unique_ptr<BListItem>> items)
{
	return AddList(std::move(items), FullListCountItems());
}


bool
BOutlineListView::AddList(std::vector<std::unique_ptr<BListItem>> items, int32 fullListIndex)
{
	if (items.empty())
		return false;

	for (std::size_t i = 0; i < items.size(); i++)
		(void)AddItem(std::move(items[i]), fullListIndex + static_cast<int32>(i));

	return true;
}


std::unique_ptr<BListItem>
BOutlineListView::RemoveItem(BListItem* item)
{
	return _RemoveItem(item, FullListIndexOf(item));
}


std::unique_ptr<BListItem>
BOutlineListView::RemoveItem(int32 fullListIndex)
{
	return _RemoveItem(FullListItemAt(fullListIndex), fullListIndex);
}


bool
BOutlineListView::RemoveItems(int32 fullListIndex, int32 count)
{
	if (fullListIndex >= FullListCountItems())
		fullListIndex = -1;
	if (fullListIndex < 0)
		return false;

	// TODO: very bad for performance!!
	while (count-- > 0 && fullListIndex < FullListCountItems())
		(void)BOutlineListView::RemoveItem(fullListIndex);

	return true;
}


BListItem*
BOutlineListView::FullListItemAt(int32 fullListIndex) const
{
	if (fullListIndex < 0 || fullListIndex >= FullListCountItems())
		return nullptr;
	return fFullList[static_cast<std::size_t>(fullListIndex)];
}


int32
BOutlineListView::FullListIndexOf(gfx::Point where) const
{
	const int32 index = BListView::IndexOf(where);
	if (index >= 0)
		return _FullListIndex(index);
	return index;
}


int32
BOutlineListView::FullListIndexOf(const BListItem* item) const
{
	const auto found = std::find(fFullList.begin(), fFullList.end(), item);
	if (item == nullptr || found == fFullList.end())
		return -1;
	return static_cast<int32>(found - fFullList.begin());
}


BListItem*
BOutlineListView::FullListFirstItem() const
{
	return fFullList.empty() ? nullptr : fFullList.front();
}


BListItem*
BOutlineListView::FullListLastItem() const
{
	return fFullList.empty() ? nullptr : fFullList.back();
}


bool
BOutlineListView::FullListHasItem(const BListItem* item) const
{
	return FullListIndexOf(item) != -1;
}


int32
BOutlineListView::FullListCountItems() const
{
	return static_cast<int32>(fFullList.size());
}


int32
BOutlineListView::FullListCurrentSelection(int32 index) const
{
	const int32 i = BListView::CurrentSelection(index);

	BListItem* item = BListView::ItemAt(i);
	if (item != nullptr)
		return FullListIndexOf(item);

	return -1;
}


void
BOutlineListView::MakeEmpty()
{
	fFullList.clear();
	fHidden.clear();
	BListView::MakeEmpty();
}


bool
BOutlineListView::FullListIsEmpty() const
{
	return fFullList.empty();
}


void
BOutlineListView::FullListDoForEach(const std::function<bool(BListItem* item)>& function)
{
	for (BListItem* item : fFullList) {
		if (function(item))
			break;
	}
}


BListItem*
BOutlineListView::Superitem(const BListItem* item)
{
	const int32 index = FullListIndexOf(item);
	if (index == -1)
		return nullptr;

	return _SuperitemForIndex(index, item->OutlineLevel());
}


void
BOutlineListView::Expand(BListItem* item)
{
	ExpandOrCollapse(item, true);
}


void
BOutlineListView::Collapse(BListItem* item)
{
	ExpandOrCollapse(item, false);
}


bool
BOutlineListView::IsExpanded(int32 fullListIndex)
{
	BListItem* item = FullListItemAt(fullListIndex);
	if (item == nullptr)
		return false;

	return item->IsExpanded();
}


void
BOutlineListView::GetPreferredSize(float* width, float* height)
{
	const int32 count = CountItems();

	if (count > 0) {
		const TextSystem& text = TextSystem::Default();
		float maxWidth = 0.0f;
		for (int32 i = 0; i < count; i++) {
			// The item itself does not take his OutlineLevel into account, so
			// we must make up for that. Also add space for the latch.
			const float itemWidth = ItemAt(i)->Width() + text.FontSize(FontRole::Plain)
				+ static_cast<float>(ItemAt(i)->OutlineLevel() + 1)
					* Look().DefaultItemSpacing(text);
			if (itemWidth > maxWidth)
				maxWidth = itemWidth;
		}

		if (width != nullptr)
			*width = maxWidth;
		if (height != nullptr)
			*height = ItemAt(count - 1)->Bottom() + 1;
	} else
		BView::GetPreferredSize(width, height);
}


void
BOutlineListView::SortItems(int (*compare)(const BListItem*, const BListItem*))
{
	FullListSortItems(compare);
}


void
BOutlineListView::FullListSortItems(int (*compare)(const BListItem* first,
	const BListItem* second))
{
	SortItemsUnder(nullptr, false, compare);
}


void
BOutlineListView::SortItemsUnder(BListItem* superItem, bool oneLevelOnly,
	int (*compare)(const BListItem* first, const BListItem* second))
{
	int32 begin = 0;
	int32 end = FullListCountItems();
	if (superItem != nullptr) {
		const int32 superIndex = FullListIndexOf(superItem);
		if (superIndex < 0)
			return;
		begin = superIndex + 1;
		end = _SubtreeEnd(superIndex);
	}

	_SortRange(begin, end, oneLevelOnly, compare);

	// NewGUI: with levels that skip, an item can end up below a collapsed
	// one, so the structure is worked out anew
	_Restructure();
}


int32
BOutlineListView::CountItemsUnder(BListItem* superItem, bool oneLevelOnly) const
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (!_ItemsUnderSetup(superItem, i, baseLevel))
		return 0;

	int32 count = 0;
	for (; i < FullListCountItems(); i++) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return count
		if (item->fLevel < baseLevel)
			return count;

		// If we are in the right level, increment count
		if (!oneLevelOnly || item->fLevel == baseLevel)
			count++;
	}

	return count;
}


BListItem*
BOutlineListView::EachItemUnder(BListItem* superItem, bool oneLevelOnly,
	const std::function<BListItem*(BListItem* item)>& function)
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (!_ItemsUnderSetup(superItem, i, baseLevel))
		return nullptr;

	while (i < FullListCountItems()) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return nullptr
		if (item->fLevel < baseLevel)
			return nullptr;

		// If we are in the right level, call the function
		if (!oneLevelOnly || item->fLevel == baseLevel) {
			item = function(item);
			if (item != nullptr)
				return item;
		}

		i++;
	}

	return nullptr;
}


BListItem*
BOutlineListView::ItemUnderAt(BListItem* superItem, bool oneLevelOnly, int32 index) const
{
	int32 i = 0;
	uint32 baseLevel = 0;
	if (!_ItemsUnderSetup(superItem, i, baseLevel))
		return nullptr;

	while (i < FullListCountItems()) {
		BListItem* item = FullListItemAt(i);

		// If we jump out of the subtree, return nullptr
		if (item->fLevel < baseLevel)
			return nullptr;

		// If we are in the right level, check the index
		if (!oneLevelOnly || item->fLevel == baseLevel) {
			if (index == 0)
				return item;

			index--;
		}

		i++;
	}

	return nullptr;
}


// #pragma mark - protected


bool
BOutlineListView::DoMiscellaneous(MiscCode code, MiscData* data)
{
	switch (code) {
		case MiscCode::Swap:
			return _SwapItems(data->a, data->b);

		case MiscCode::Move:
			// NewGUI: moving among the visible items would tear the tree
			return false;

		case MiscCode::Replace:
		{
			// NewGUI: the new item takes the old one's place in the tree
			BListItem* old = ItemAt(data->a);
			BListItem* replacement = data->item.get();
			if (old == nullptr || replacement == nullptr)
				return false;

			const int32 fullListIndex = FullListIndexOf(old);
			replacement->fLevel = old->fLevel;
			replacement->fHasSubitems = old->fHasSubitems;
			replacement->fExpanded = old->fExpanded;
			replacement->SetItemVisible(true);
			if (!BListView::DoMiscellaneous(code, data))
				return false;

			fFullList[static_cast<std::size_t>(fullListIndex)] = replacement;
			old->fHasSubitems = false;
			return true;
		}

		case MiscCode::NoOp:
			break;
	}

	return BListView::DoMiscellaneous(code, data);
}


void
BOutlineListView::ExpandOrCollapse(BListItem* item, bool expand)
{
	if (item == nullptr || item->IsExpanded() == expand || !FullListHasItem(item))
		return;

	item->fExpanded = expand;

	// NewGUI: the subitems of a hidden item stay hidden
	if (!item->IsItemVisible())
		return;

	const uint32 level = item->fLevel;
	const int32 fullListIndex = FullListIndexOf(item);
	const int32 end = _SubtreeEnd(fullListIndex);

	if (expand) {
		const int32 startIndex = IndexOf(item) + 1;
		int32 index = startIndex;
		const TextSystem& text = TextSystem::Default();

		int32 i = fullListIndex + 1;
		while (i < end) {
			BListItem* subItem = FullListItemAt(i);

			if (!subItem->IsItemVisible()) {
				fList.insert(fList.begin() + index++, _TakeHidden(subItem));
				subItem->SetItemVisible(true);
				if (Window() != nullptr)
					subItem->Update(this, text);
			}

			// skip the subitems of a collapsed subitem
			if (subItem->HasSubitems() && !subItem->IsExpanded())
				i = _SubtreeEnd(i);
			else
				i++;
		}

		const int32 count = index - startIndex;
		if (fAnchorIndex >= startIndex)
			fAnchorIndex += count;

		_RecalcItemTops(startIndex);
		_UpdateSelectionRange();
	} else {
		const int32 index = IndexOf(item);
		int32 count = 0;
		bool selectionChanged = false;

		// the visible subitems follow the item
		while (index + 1 < CountItems() && ItemAt(index + 1)->fLevel > level) {
			BListItem* subItem = ItemAt(index + 1);
			std::unique_ptr<BListItem> owned
				= std::move(fList[static_cast<std::size_t>(index + 1)]);
			fList.erase(fList.begin() + index + 1);
			subItem->SetItemVisible(false);
			if (subItem->IsSelected()) {
				selectionChanged = true;
				subItem->Deselect();
			}
			fHidden.emplace(subItem, std::move(owned));
			count++;
		}

		if (fAnchorIndex > index + count)
			fAnchorIndex -= count;
		else if (fAnchorIndex > index)
			fAnchorIndex = index;

		_RecalcItemTops(index);
		_UpdateSelectionRange();

		// A hidden selected subitem passes its selection to the item.
		if (selectionChanged) {
			(void)_Select(index, true);
			_NotifySelection();
		}
	}

	_FixupScrollBar();
	Invalidate();
}


gfx::Rect
BOutlineListView::LatchRect(gfx::Rect itemRect, int32 level) const
{
	const TextSystem& text = TextSystem::Default();
	const float latchWidth = text.FontSize(FontRole::Plain);
	const float latchHeight = text.FontSize(FontRole::Plain);
	const float indentOffset = static_cast<float>(level) * Look().DefaultItemSpacing(text);
	// Haiku's inclusive height
	const float heightOffset = (itemRect.Height() - 1) / 2 - latchHeight / 2;

	const float left = itemRect.x0 + indentOffset;
	const float top = itemRect.y0 + heightOffset;
	// Haiku's BRect(0, 0, latchWidth, latchHeight) covers one pixel more
	return {left, top, left + latchWidth + 1, top + latchHeight + 1};
}


void
BOutlineListView::DrawLatch(DrawContext& context, gfx::Rect itemRect, int32 level,
	bool collapsed, bool /*highlighted*/, bool /*misTracked*/)
{
	gfx::Rect latchRect = LatchRect(itemRect, level);

	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);
	const ArrowDirection arrowDirection = collapsed ? ArrowDirection::Right
		: ArrowDirection::Down;

	float tintColor = kDarken4Tint;
	const compat::rgb_color rgb = compat::ToRgbColor(base);
	if (rgb.red + rgb.green + rgb.blue <= 128 * 3) {
		// dark backgrounds need lighter arrow
		tintColor = kLighten2Tint;
	}

	Look().DrawArrowShape(context, latchRect, base, arrowDirection, {}, tintColor);
}


void
BOutlineListView::DrawItem(BListItem* item, DrawContext& context, gfx::Rect itemRect,
	bool complete)
{
	const auto level = static_cast<int32>(item->fLevel);
	if (item->fHasSubitems) {
		DrawLatch(context, itemRect, level, !item->IsExpanded(),
			item->IsSelected() || complete, false);
	}

	// Haiku moved the left edge to the latch's inclusive right edge
	itemRect.x0 = LatchRect(itemRect, level).x1 - 1;
	BListView::DrawItem(item, context, itemRect, complete);
}


// #pragma mark - private


int32
BOutlineListView::_FullListIndex(int32 index) const
{
	BListItem* item = ItemAt(index);

	if (item == nullptr)
		return -1;

	return FullListIndexOf(item);
}


bool
BOutlineListView::_SwapItems(int32 first, int32 second)
{
	if (first == second)
		return true;

	if (first < 0 || first >= CountItems())
		return false;
	if (second < 0 || second >= CountItems())
		return false;

	const int32 firstIndex = std::min(first, second);
	const int32 secondIndex = std::max(first, second);
	BListItem* firstItem = ItemAt(firstIndex);
	BListItem* secondItem = ItemAt(secondIndex);

	// NewGUI: siblings only, of the same level
	if (Superitem(firstItem) != Superitem(secondItem) || firstItem->fLevel != secondItem->fLevel)
		return false;

	// the full list: each item with all its subitems
	const int32 fullFirst = FullListIndexOf(firstItem);
	const int32 fullSecond = FullListIndexOf(secondItem);
	SwapBlocks(fFullList, static_cast<std::size_t>(fullFirst),
		static_cast<std::size_t>(_SubtreeEnd(fullFirst)), static_cast<std::size_t>(fullSecond),
		static_cast<std::size_t>(_SubtreeEnd(fullSecond)));

	// the visible list: each item with its visible subitems
	const auto visibleEnd = [this](int32 index) {
		const uint32 level = ItemAt(index)->fLevel;
		int32 end = index + 1;
		while (end < CountItems() && ItemAt(end)->fLevel > level)
			end++;
		return static_cast<std::size_t>(end);
	};
	const std::size_t firstEnd = visibleEnd(firstIndex);
	const std::size_t secondEnd = visibleEnd(secondIndex);
	SwapBlocks(fList, static_cast<std::size_t>(firstIndex), firstEnd,
		static_cast<std::size_t>(secondIndex), secondEnd);

	_RecalcItemTops(firstIndex);
	_UpdateSelectionRange();
	Invalidate();
	return true;
}


int32
BOutlineListView::_SubtreeEnd(int32 fullListIndex) const
{
	const uint32 level = fFullList[static_cast<std::size_t>(fullListIndex)]->fLevel;
	int32 end = fullListIndex + 1;
	while (end < FullListCountItems() && FullListItemAt(end)->fLevel > level)
		end++;
	return end;
}


void
BOutlineListView::_SortRange(int32 begin, int32 end, bool oneLevelOnly,
	int (*compare)(const BListItem*, const BListItem*))
{
	// the blocks of the range: an item with its subitems
	struct Block {
		int32 begin;
		int32 end;
	};
	std::vector<Block> blocks;
	for (int32 i = begin; i < end;) {
		const int32 blockEnd = std::min(_SubtreeEnd(i), end);
		blocks.push_back({i, blockEnd});
		i = blockEnd;
	}

	std::stable_sort(blocks.begin(), blocks.end(), [this, compare](const Block& a,
		const Block& b) {
		return compare(FullListItemAt(a.begin), FullListItemAt(b.begin)) < 0;
	});

	std::vector<BListItem*> sorted;
	sorted.reserve(static_cast<std::size_t>(end - begin));
	for (const Block& block : blocks) {
		sorted.insert(sorted.end(), fFullList.begin() + block.begin,
			fFullList.begin() + block.end);
	}
	std::copy(sorted.begin(), sorted.end(), fFullList.begin() + begin);

	if (oneLevelOnly)
		return;

	// the subitems of each block
	int32 i = begin;
	for (const Block& block : blocks) {
		const int32 size = block.end - block.begin;
		if (size > 1)
			_SortRange(i + 1, i + size, false, compare);
		i += size;
	}
}


void
BOutlineListView::_Restructure()
{
	// which items have subitems
	for (std::size_t i = 0; i < fFullList.size(); i++) {
		fFullList[i]->fHasSubitems = i + 1 < fFullList.size()
			&& fFullList[i + 1]->fLevel > fFullList[i]->fLevel;
	}

	// which items are visible: those whose superitems are all expanded
	struct Ancestor {
		uint32 level;
		bool showsSubitems;
	};
	std::vector<Ancestor> ancestors;
	std::vector<BListItem*> shown;
	bool selectionLost = false;
	for (BListItem* item : fFullList) {
		while (!ancestors.empty() && ancestors.back().level >= item->fLevel)
			ancestors.pop_back();
		const bool visible = ancestors.empty() || ancestors.back().showsSubitems;
		ancestors.push_back({item->fLevel, visible && item->IsExpanded()});

		if (visible == item->IsItemVisible())
			continue;

		if (visible) {
			fList.push_back(_TakeHidden(item));
			item->SetItemVisible(true);
			shown.push_back(item);
		} else {
			const auto index = static_cast<std::size_t>(IndexOf(item));
			std::unique_ptr<BListItem> owned = std::move(fList[index]);
			fList.erase(fList.begin() + static_cast<std::ptrdiff_t>(index));
			item->SetItemVisible(false);
			if (item->IsSelected()) {
				item->Deselect();
				selectionLost = true;
			}
			fHidden.emplace(item, std::move(owned));
		}
	}

	_OrderVisibleItems();
	if (Window() != nullptr) {
		const TextSystem& text = TextSystem::Default();
		for (BListItem* item : shown)
			item->Update(this, text);
	}

	_RecalcItemTops(0);
	_UpdateSelectionRange();
	fAnchorIndex = fFirstSelected;
	if (selectionLost)
		_NotifySelection();

	_FixupScrollBar();
	Invalidate();
}


void
BOutlineListView::_OrderVisibleItems()
{
	std::unordered_map<const BListItem*, std::size_t> place;
	for (std::size_t i = 0; i < fFullList.size(); i++)
		place.emplace(fFullList[i], i);

	std::stable_sort(fList.begin(), fList.end(), [&place](const std::unique_ptr<BListItem>& a,
		const std::unique_ptr<BListItem>& b) {
		return place.at(a.get()) < place.at(b.get());
	});
}


std::unique_ptr<BListItem>
BOutlineListView::_RemoveItem(BListItem* item, int32 fullListIndex)
{
	if (item == nullptr || fullListIndex < 0 || fullListIndex >= FullListCountItems())
		return nullptr;

	const uint32 level = item->OutlineLevel();
	int32 superIndex = -1;
	BListItem* super = _SuperitemForIndex(fullListIndex, level, &superIndex);

	// NewGUI: the subitems go also when the item is hidden
	const int32 end = _SubtreeEnd(fullListIndex);
	for (int32 i = end - 1; i > fullListIndex; i--) {
		BListItem* subItem = FullListItemAt(i);
		if (subItem->IsItemVisible())
			(void)BListView::RemoveItem(BListView::IndexOf(subItem));
		else
			fHidden.erase(subItem);
	}

	std::unique_ptr<BListItem> removed;
	if (item->IsItemVisible())
		removed = BListView::RemoveItem(BListView::IndexOf(item));
	else
		removed = _TakeHidden(item);

	fFullList.erase(fFullList.begin() + fullListIndex, fFullList.begin() + end);

	if (super != nullptr) {
		// we might need to change the fHasSubitems field of the parent
		BListItem* child = FullListItemAt(superIndex + 1);
		if (child == nullptr || child->OutlineLevel() <= super->OutlineLevel()) {
			super->fHasSubitems = false;
			Invalidate();
		}
	}

	removed->fHasSubitems = false;
	removed->SetItemVisible(true);
	return removed;
}


BListItem*
BOutlineListView::_SuperitemForIndex(int32 fullListIndex, uint32 level,
	int32* superIndex) const
{
	fullListIndex--;

	while (fullListIndex >= 0) {
		BListItem* item = FullListItemAt(fullListIndex);
		if (item->OutlineLevel() < level) {
			if (superIndex != nullptr)
				*superIndex = fullListIndex;
			return item;
		}

		fullListIndex--;
	}

	return nullptr;
}


int32
BOutlineListView::_FindPreviousVisibleIndex(int32 fullListIndex) const
{
	fullListIndex--;

	while (fullListIndex >= 0) {
		if (FullListItemAt(fullListIndex)->fVisible)
			return fullListIndex;

		fullListIndex--;
	}

	return -1;
}


bool
BOutlineListView::_ItemsUnderSetup(BListItem* superItem, int32& startIndex,
	uint32& baseLevel) const
{
	if (superItem != nullptr) {
		startIndex = FullListIndexOf(superItem) + 1;
		if (startIndex == 0)
			return false;
		baseLevel = superItem->OutlineLevel() + 1;
	} else {
		startIndex = 0;
		baseLevel = 0;
	}

	return true;
}


std::unique_ptr<BListItem>
BOutlineListView::_TakeHidden(BListItem* item)
{
	auto node = fHidden.extract(item);
	return node.empty() ? nullptr : std::move(node.mapped());
}


// #pragma mark - accessibility


void
BOutlineListView::GetAccessibleInfo(AccessibleInfo* info) const
{
	BListView::GetAccessibleInfo(info);
	if (info != nullptr)
		info->role = AccessibleRole::Tree;
}


void
BOutlineListView::GetAccessibleItems(std::vector<AccessibleItem>* items) const
{
	if (items == nullptr)
		return;
	const std::size_t first = items->size();
	BListView::GetAccessibleItems(items);
	for (std::size_t i = first; i < items->size(); i++) {
		AccessibleItem& entry = (*items)[i];
		const BListItem* item = ItemAt(static_cast<int32>(i - first));
		entry.info.role = AccessibleRole::TreeItem;
		entry.info.level = static_cast<std::int32_t>(item->OutlineLevel()) + 1;
		if (item->HasSubitems()) {
			if (item->IsExpanded()) {
				entry.info.states |= AccessibleState::Expanded;
				entry.info.actions |= AccessibleAction::Collapse;
			} else {
				entry.info.states |= AccessibleState::Collapsed;
				entry.info.actions |= AccessibleAction::Expand;
			}
		}
	}
}


std::expected<void, Error>
BOutlineListView::PerformAccessibleItemAction(int32 index, AccessibleAction action,
	const BMessage& data)
{
	if (action != AccessibleAction::Expand && action != AccessibleAction::Collapse)
		return BListView::PerformAccessibleItemAction(index, action, data);
	BListItem* item = ItemAt(index);
	if (item == nullptr)
		return Failure(ErrorCode::NotFound, "no such item");
	if (!item->HasSubitems())
		return Failure(ErrorCode::NotSupported, "the item has no subitems");
	if (action == AccessibleAction::Expand)
		Expand(item);
	else
		Collapse(item);
	return {};
}

} // namespace ngui
