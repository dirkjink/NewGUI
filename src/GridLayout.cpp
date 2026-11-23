/*
 * Copyright 2010-2011 Haiku, Inc. All rights reserved.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/GridLayout.cpp (commit b1d497bb69),
 * without archiving. NewGUI:
 * - The grid is a std::vector of columns; every cell an item covers holds
 *   the item (Haiku: the first one, the others a placeholder 0x1), so that
 *   ItemAt(column, row) never gives a pointer that is no item.
 * - The cells of the items and the infos of columns and rows are kept in
 *   std::vector (Haiku: layout data per item, BList).
 * - Items must cover at least one cell and stay within 1024 columns and
 *   rows (Haiku limited only the infos, and grew the grid to any size).
 * - Setting a weight or limit of a column or row invalidates the layout.
 * - The spacings are resolved when they are used.
 */


#include <ngui/GridLayout.h>

#include <algorithm>
#include <utility>

#include <ngui/LayoutUtils.h>
#include <ngui/View.h>


namespace ngui {


namespace {


constexpr int32 MAX_COLUMN_ROW_COUNT = 1024;


} // namespace


BGridLayout::BGridLayout(float horizontal, float vertical)
{
	fHSpacing = horizontal;
	fVSpacing = vertical;
}


BGridLayout::~BGridLayout()
{
}


int32
BGridLayout::CountColumns() const
{
	return fColumnCount;
}


int32
BGridLayout::CountRows() const
{
	return fRowCount;
}


float
BGridLayout::HorizontalSpacing() const
{
	return BLayoutUtils::ComposeSpacing(fHSpacing);
}


float
BGridLayout::VerticalSpacing() const
{
	return BLayoutUtils::ComposeSpacing(fVSpacing);
}


void
BGridLayout::SetHorizontalSpacing(float spacing)
{
	if (spacing != fHSpacing) {
		fHSpacing = spacing;

		InvalidateLayout();
	}
}


void
BGridLayout::SetVerticalSpacing(float spacing)
{
	if (spacing != fVSpacing) {
		fVSpacing = spacing;

		InvalidateLayout();
	}
}


void
BGridLayout::SetSpacing(float horizontal, float vertical)
{
	if (horizontal != fHSpacing || vertical != fVSpacing) {
		fHSpacing = horizontal;
		fVSpacing = vertical;

		InvalidateLayout();
	}
}


float
BGridLayout::ColumnWeight(int32 column) const
{
	const Info* info = _InfoAt(fColumnInfos, column);
	return info ? info->weight : 1;
}


void
BGridLayout::SetColumnWeight(int32 column, float weight)
{
	if (Info* info = _InfoAt(fColumnInfos, column, true)) {
		info->weight = weight;
		InvalidateLayout();
	}
}


float
BGridLayout::MinColumnWidth(int32 column) const
{
	const Info* info = _InfoAt(fColumnInfos, column);
	return info ? info->minSize : kSizeUnset;
}


void
BGridLayout::SetMinColumnWidth(int32 column, float width)
{
	if (Info* info = _InfoAt(fColumnInfos, column, true)) {
		info->minSize = width;
		InvalidateLayout();
	}
}


float
BGridLayout::MaxColumnWidth(int32 column) const
{
	const Info* info = _InfoAt(fColumnInfos, column);
	return info ? info->maxSize : kSizeUnset;
}


void
BGridLayout::SetMaxColumnWidth(int32 column, float width)
{
	if (Info* info = _InfoAt(fColumnInfos, column, true)) {
		info->maxSize = width;
		InvalidateLayout();
	}
}


float
BGridLayout::RowWeight(int32 row) const
{
	const Info* info = _InfoAt(fRowInfos, row);
	return info ? info->weight : 1;
}


void
BGridLayout::SetRowWeight(int32 row, float weight)
{
	if (Info* info = _InfoAt(fRowInfos, row, true)) {
		info->weight = weight;
		InvalidateLayout();
	}
}


float
BGridLayout::MinRowHeight(int32 row) const
{
	const Info* info = _InfoAt(fRowInfos, row);
	return info ? info->minSize : kSizeUnset;
}


void
BGridLayout::SetMinRowHeight(int32 row, float height)
{
	if (Info* info = _InfoAt(fRowInfos, row, true)) {
		info->minSize = height;
		InvalidateLayout();
	}
}


float
BGridLayout::MaxRowHeight(int32 row) const
{
	const Info* info = _InfoAt(fRowInfos, row);
	return info ? info->maxSize : kSizeUnset;
}


void
BGridLayout::SetMaxRowHeight(int32 row, float height)
{
	if (Info* info = _InfoAt(fRowInfos, row, true)) {
		info->maxSize = height;
		InvalidateLayout();
	}
}


BLayoutItem*
BGridLayout::ItemAt(int32 column, int32 row) const
{
	if (column < 0 || column >= CountColumns()
		|| row < 0 || row >= CountRows())
		return nullptr;

	return fGrid[(std::size_t)column][(std::size_t)row];
}


BLayoutItem*
BGridLayout::AddView(std::unique_ptr<BView> child, int32 column, int32 row,
	int32 columnCount, int32 rowCount)
{
	if (!child)
		return nullptr;

	return AddItem(ItemForView(std::move(child)), column, row, columnCount,
		rowCount);
}


BLayoutItem*
BGridLayout::AddView(BView* child, int32 column, int32 row, int32 columnCount,
	int32 rowCount)
{
	if (!child)
		return nullptr;

	return AddItem(ItemForView(child), column, row, columnCount, rowCount);
}


BLayoutItem*
BGridLayout::AddItem(int32 /*index*/, std::unique_ptr<BLayoutItem> item)
{
	// find a free spot
	for (int32 row = 0; row < fRowCount; row++) {
		for (int32 column = 0; column < fColumnCount; column++) {
			if (_IsGridCellEmpty(column, row))
				return AddItem(std::move(item), column, row, 1, 1);
		}
	}

	// no free spot, start a new column
	return AddItem(std::move(item), fColumnCount, 0, 1, 1);
}


BLayoutItem*
BGridLayout::AddItem(std::unique_ptr<BLayoutItem> item, int32 column, int32 row,
	int32 columnCount, int32 rowCount)
{
	if (columnCount < 1 || rowCount < 1
		|| column > MAX_COLUMN_ROW_COUNT - columnCount
		|| row > MAX_COLUMN_ROW_COUNT - rowCount) {
		return nullptr;
	}
	if (!_AreGridCellsEmpty(column, row, columnCount, rowCount))
		return nullptr;

	BLayoutItem* added = BTwoDimensionalLayout::AddItem(-1, std::move(item));
	if (!added)
		return nullptr;

	// set item dimensions
	if (Dimensions* dimensions = _DimensionsOf(added)) {
		dimensions->x = column;
		dimensions->y = row;
		dimensions->width = columnCount;
		dimensions->height = rowCount;
	}

	if (!_InsertItemIntoGrid(added)) {
		(void)RemoveItem(added);
		return nullptr;
	}

	if (columnCount > 1)
		fMultiColumnItems++;
	if (rowCount > 1)
		fMultiRowItems++;

	// NewGUI: the dimensions are set after BLayout::AddItem() invalidated
	InvalidateLayout();

	return added;
}


bool
BGridLayout::ItemAdded(BLayoutItem* item, int32 /*atIndex*/)
{
	fDimensions.emplace_back(item, Dimensions());
	return true;
}


void
BGridLayout::ItemRemoved(BLayoutItem* item, int32 /*fromIndex*/)
{
	Dimensions itemDimensions;
	if (Dimensions* dimensions = _DimensionsOf(item))
		itemDimensions = *dimensions;
	std::erase_if(fDimensions, [item](const auto& entry) { return entry.first == item; });

	// NewGUI: only an item that was entered in the grid frees cells
	if (ItemAt(itemDimensions.x, itemDimensions.y) != item)
		return;

	if (itemDimensions.width > 1)
		fMultiColumnItems--;
	if (itemDimensions.height > 1)
		fMultiRowItems--;

	// remove the item from the grid
	for (int x = 0; x < itemDimensions.width; x++) {
		for (int y = 0; y < itemDimensions.height; y++) {
			fGrid[(std::size_t)(itemDimensions.x + x)]
				[(std::size_t)(itemDimensions.y + y)] = nullptr;
		}
	}

	// check whether we can shrink the grid
	if (itemDimensions.x + itemDimensions.width == fColumnCount
		|| itemDimensions.y + itemDimensions.height == fRowCount) {
		int32 columnCount = fColumnCount;
		int32 rowCount = fRowCount;

		// check for empty columns
		bool empty = true;
		for (; columnCount > 0; columnCount--) {
			for (int32 row = 0; empty && row < rowCount; row++)
				empty &= (fGrid[(std::size_t)columnCount - 1][(std::size_t)row] == nullptr);

			if (!empty)
				break;
		}

		// check for empty rows
		empty = true;
		for (; rowCount > 0; rowCount--) {
			for (int32 column = 0; empty && column < columnCount; column++)
				empty &= (fGrid[(std::size_t)column][(std::size_t)rowCount - 1] == nullptr);

			if (!empty)
				break;
		}

		// resize the grid
		if (columnCount != fColumnCount || rowCount != fRowCount)
			_ResizeGrid(columnCount, rowCount);
	}
}


bool
BGridLayout::HasMultiColumnItems()
{
	return fMultiColumnItems > 0;
}


bool
BGridLayout::HasMultiRowItems()
{
	return fMultiRowItems > 0;
}


int32
BGridLayout::InternalCountColumns()
{
	return fColumnCount;
}


int32
BGridLayout::InternalCountRows()
{
	return fRowCount;
}


void
BGridLayout::GetColumnRowConstraints(BOrientation orientation, int32 index,
	ColumnRowConstraints* constraints)
{
	if (orientation == BOrientation::Horizontal) {
		constraints->min = MinColumnWidth(index);
		constraints->max = MaxColumnWidth(index);
		constraints->weight = ColumnWeight(index);
	} else {
		constraints->min = MinRowHeight(index);
		constraints->max = MaxRowHeight(index);
		constraints->weight = RowWeight(index);
	}
}


void
BGridLayout::GetItemDimensions(BLayoutItem* item, Dimensions* dimensions)
{
	if (Dimensions* itemDimensions = _DimensionsOf(item))
		*dimensions = *itemDimensions;
}


bool
BGridLayout::_IsGridCellEmpty(int32 column, int32 row)
{
	if (column < 0 || row < 0)
		return false;
	if (column >= fColumnCount || row >= fRowCount)
		return true;

	return fGrid[(std::size_t)column][(std::size_t)row] == nullptr;
}


bool
BGridLayout::_AreGridCellsEmpty(int32 column, int32 row, int32 columnCount,
	int32 rowCount)
{
	if (column < 0 || row < 0)
		return false;
	int32 toColumn = std::min(column + columnCount, fColumnCount);
	int32 toRow = std::min(row + rowCount, fRowCount);
	for (int32 x = column; x < toColumn; x++) {
		for (int32 y = row; y < toRow; y++) {
			if (fGrid[(std::size_t)x][(std::size_t)y] != nullptr)
				return false;
		}
	}

	return true;
}


bool
BGridLayout::_InsertItemIntoGrid(BLayoutItem* item)
{
	Dimensions* dimensions = _DimensionsOf(item);
	if (dimensions == nullptr)
		return false;
	int32 column = dimensions->x;
	int32 columnCount = dimensions->width;
	int32 row = dimensions->y;
	int32 rowCount = dimensions->height;

	// resize the grid, if necessary
	int32 newColumnCount = std::max(fColumnCount, column + columnCount);
	int32 newRowCount = std::max(fRowCount, row + rowCount);
	if (newColumnCount > fColumnCount || newRowCount > fRowCount)
		_ResizeGrid(newColumnCount, newRowCount);

	// enter the item in the grid
	for (int32 x = 0; x < columnCount; x++) {
		for (int32 y = 0; y < rowCount; y++)
			fGrid[(std::size_t)(column + x)][(std::size_t)(row + y)] = item;
	}

	return true;
}


void
BGridLayout::_ResizeGrid(int32 columnCount, int32 rowCount)
{
	fGrid.resize((std::size_t)columnCount);
	for (std::vector<BLayoutItem*>& column : fGrid)
		column.resize((std::size_t)rowCount, nullptr);
	fColumnCount = columnCount;
	fRowCount = rowCount;
}


BTwoDimensionalLayout::Dimensions*
BGridLayout::_DimensionsOf(BLayoutItem* item)
{
	for (auto& entry : fDimensions) {
		if (entry.first == item)
			return &entry.second;
	}
	return nullptr;
}


const BGridLayout::Info*
BGridLayout::_InfoAt(const std::vector<Info>& infos, int32 index)
{
	if (index < 0 || index >= (int32)infos.size())
		return nullptr;
	return &infos[(std::size_t)index];
}


BGridLayout::Info*
BGridLayout::_InfoAt(std::vector<Info>& infos, int32 index, bool resize)
{
	if (index < 0 || index >= MAX_COLUMN_ROW_COUNT)
		return nullptr;

	// resize, if necessary and desired
	if (index >= (int32)infos.size()) {
		if (!resize)
			return nullptr;
		infos.resize((std::size_t)index + 1);
	}

	return &infos[(std::size_t)index];
}


} // namespace ngui
