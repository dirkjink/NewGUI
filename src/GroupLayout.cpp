/*
 * Copyright 2010 Haiku, Inc. All rights reserved.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/GroupLayout.cpp (commit b1d497bb69),
 * without archiving. NewGUI: the weights are kept in a list of the layout
 * instead of layout data allocated per item; the spacing is resolved when it
 * is used; the column and row constraints start with defaults, so that a
 * group whose items are all hidden reports defined values (Haiku left them
 * uninitialized); the minimum of a cell is 0 pixels (Haiku: -1).
 */


#include <ngui/GroupLayout.h>

#include <algorithm>
#include <utility>

#include <ngui/LayoutUtils.h>
#include <ngui/View.h>


namespace ngui {


BGroupLayout::BGroupLayout(BOrientation orientation, float spacing)
	:
	BTwoDimensionalLayout(),
	fOrientation(orientation)
{
	fHSpacing = spacing;
	fVSpacing = spacing;
}


BGroupLayout::~BGroupLayout()
{
}


float
BGroupLayout::Spacing() const
{
	return BLayoutUtils::ComposeSpacing(fHSpacing);
}


void
BGroupLayout::SetSpacing(float spacing)
{
	if (spacing != fHSpacing) {
		fHSpacing = spacing;
		fVSpacing = spacing;
		InvalidateLayout();
	}
}


BOrientation
BGroupLayout::Orientation() const
{
	return fOrientation;
}


void
BGroupLayout::SetOrientation(BOrientation orientation)
{
	if (orientation != fOrientation) {
		fOrientation = orientation;

		InvalidateLayout();
	}
}


float
BGroupLayout::ItemWeight(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return 0;

	return _Weight(ItemAt(index));
}


void
BGroupLayout::SetItemWeight(int32 index, float weight)
{
	if (index < 0 || index >= CountItems())
		return;

	BLayoutItem* item = ItemAt(index);
	for (auto& entry : fWeights) {
		if (entry.first == item)
			entry.second = weight;
	}

	InvalidateLayout();
}


BLayoutItem*
BGroupLayout::AddView(std::unique_ptr<BView> child, float weight)
{
	return AddView(-1, std::move(child), weight);
}


BLayoutItem*
BGroupLayout::AddView(int32 index, std::unique_ptr<BView> child, float weight)
{
	BLayoutItem* item = AddView(index, std::move(child));
	if (item != nullptr)
		SetItemWeight(IndexOfItem(item), weight);

	return item;
}


BLayoutItem*
BGroupLayout::AddItem(std::unique_ptr<BLayoutItem> item, float weight)
{
	return AddItem(-1, std::move(item), weight);
}


BLayoutItem*
BGroupLayout::AddItem(int32 index, std::unique_ptr<BLayoutItem> item, float weight)
{
	BLayoutItem* added = AddItem(index, std::move(item));
	if (added != nullptr)
		SetItemWeight(IndexOfItem(added), weight);

	return added;
}


bool
BGroupLayout::ItemAdded(BLayoutItem* item, int32 /*atIndex*/)
{
	fWeights.emplace_back(item, 1.0f);
	return true;
}


void
BGroupLayout::ItemRemoved(BLayoutItem* item, int32 /*fromIndex*/)
{
	std::erase_if(fWeights, [item](const auto& entry) { return entry.first == item; });
}


void
BGroupLayout::PrepareItems(BOrientation /*orientation*/)
{
	// filter the visible items
	fVisibleItems.clear();
	int32 itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible())
			fVisibleItems.push_back(item);
	}
}


int32
BGroupLayout::InternalCountColumns()
{
	return (fOrientation == BOrientation::Horizontal
		? (int32)fVisibleItems.size() : 1);
}


int32
BGroupLayout::InternalCountRows()
{
	return (fOrientation == BOrientation::Vertical
		? (int32)fVisibleItems.size() : 1);
}


void
BGroupLayout::GetColumnRowConstraints(BOrientation /*orientation*/, int32 index,
	ColumnRowConstraints* constraints)
{
	constraints->min = 0;
	constraints->max = kSizeUnlimited;
	constraints->weight = 1;
	if (index >= 0 && index < (int32)fVisibleItems.size())
		constraints->weight = _Weight(fVisibleItems[(std::size_t)index]);
}


void
BGroupLayout::GetItemDimensions(BLayoutItem* item, Dimensions* dimensions)
{
	const auto found = std::find(fVisibleItems.begin(), fVisibleItems.end(), item);
	if (found == fVisibleItems.end())
		return;
	int32 index = (int32)(found - fVisibleItems.begin());

	if (fOrientation == BOrientation::Horizontal) {
		dimensions->x = index;
		dimensions->y = 0;
		dimensions->width = 1;
		dimensions->height = 1;
	} else {
		dimensions->x = 0;
		dimensions->y = index;
		dimensions->width = 1;
		dimensions->height = 1;
	}
}


float
BGroupLayout::_Weight(BLayoutItem* item) const
{
	for (const auto& entry : fWeights) {
		if (entry.first == item)
			return entry.second;
	}
	return 0;
}


} // namespace ngui
