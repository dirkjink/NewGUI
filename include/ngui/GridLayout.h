/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <utility>
#include <vector>

#include <ngui/TwoDimensionalLayout.h>


namespace ngui {


/*!	Places its items in the cells of a grid (Haiku's BGridLayout); an item
	may span several columns and rows. Columns and rows have weights and may
	have a minimum and maximum size; columns and rows without items take no
	space. The grid grows with the items, up to 1024 columns and rows.

	NewGUI: ItemAt(column, row) gives the item that covers the cell, also
	for the cells after the first of a spanning item (Haiku gave a
	placeholder pointer there); weights and limits of columns and rows
	invalidate the layout when they are set. */
class BGridLayout : public BTwoDimensionalLayout {
public:
								BGridLayout(float horizontal = kUseDefaultSpacing,
									float vertical = kUseDefaultSpacing);
									/*!< A grid with these spacings between columns and
										rows. */
								~BGridLayout() override;
									/*!< Destroys the layout. */

			int32				CountColumns() const;
									/*!< The columns of the grid. */
			int32				CountRows() const;
									/*!< The rows of the grid. */

			float				HorizontalSpacing() const;
									/*!< The spacing between columns, constants resolved. */
			float				VerticalSpacing() const;
									/*!< The spacing between rows, constants resolved. */
			void				SetHorizontalSpacing(float spacing);
									/*!< Sets the spacing between columns. */
			void				SetVerticalSpacing(float spacing);
									/*!< Sets the spacing between rows. */
			void				SetSpacing(float horizontal, float vertical);
									/*!< Sets both spacings. */

			float				ColumnWeight(int32 column) const;
									/*!< Share of extra width (1 by default). */
			void				SetColumnWeight(int32 column, float weight);
									/*!< Sets the weight of \a column. */
			float				MinColumnWidth(int32 column) const;
									/*!< kSizeUnset if not set. */
			void				SetMinColumnWidth(int32 column, float width);
									/*!< Sets the minimum width of \a column. */
			float				MaxColumnWidth(int32 column) const;
									/*!< kSizeUnset if not set. */
			void				SetMaxColumnWidth(int32 column, float width);
									/*!< Sets the maximum width of \a column. */

			float				RowWeight(int32 row) const;
									/*!< Share of extra height (1 by default). */
			void				SetRowWeight(int32 row, float weight);
									/*!< Sets the weight of \a row. */
			float				MinRowHeight(int32 row) const;
									/*!< kSizeUnset if not set. */
			void				SetMinRowHeight(int32 row, float height);
									/*!< Sets the minimum height of \a row. */
			float				MaxRowHeight(int32 row) const;
									/*!< kSizeUnset if not set. */
			void				SetMaxRowHeight(int32 row, float height);
									/*!< Sets the maximum height of \a row. */

			using				BLayout::ItemAt;
			BLayoutItem*		ItemAt(int32 column, int32 row) const;
									/*!< The item covering the cell, or nullptr. */

			using				BLayout::AddView;
			using				BLayout::AddItem;

			BLayoutItem*		AddView(std::unique_ptr<BView> child, int32 column,
									int32 row, int32 columnCount = 1,
									int32 rowCount = 1);
									/*!< Adds \a child covering these cells; nullptr if a
										cell is taken or out of range. */
			BLayoutItem*		AddView(BView* child, int32 column, int32 row,
									int32 columnCount = 1, int32 rowCount = 1);
									/*!< The same for a child of the target view. */
			BLayoutItem*		AddItem(int32 index,
									std::unique_ptr<BLayoutItem> item) override;
									/*!< Adds \a item in the first free cell (row by row),
										in a new column if there is none; \a index is
										ignored, as in Haiku. */
			BLayoutItem*		AddItem(std::unique_ptr<BLayoutItem> item, int32 column,
									int32 row, int32 columnCount = 1,
									int32 rowCount = 1);
									/*!< Adds \a item covering these cells; nullptr if a
										cell is taken or out of range. */

protected:
			bool				ItemAdded(BLayoutItem* item, int32 atIndex) override;
									/*!< Gives the item one cell at (0, 0) until placed. */
			void				ItemRemoved(BLayoutItem* item, int32 fromIndex) override;
									/*!< Frees its cells and shrinks the grid. */

			bool				HasMultiColumnItems() override;
									/*!< Whether an item spans columns. */
			bool				HasMultiRowItems() override;
									/*!< Whether an item spans rows. */

			int32				InternalCountColumns() override;
									/*!< CountColumns(). */
			int32				InternalCountRows() override;
									/*!< CountRows(). */
			void				GetColumnRowConstraints(BOrientation orientation,
									int32 index,
									ColumnRowConstraints* constraints) override;
									/*!< Weight and limits of the column or row. */
			void				GetItemDimensions(BLayoutItem* item,
									Dimensions* dimensions) override;
									/*!< The cells of \a item. */

private:
			/*!	Weight and limits of a column or row. */
			struct Info {
				float			weight = 1;
				float			minSize = kSizeUnset;
				float			maxSize = kSizeUnset;
			};

			bool				_IsGridCellEmpty(int32 column, int32 row);
			bool				_AreGridCellsEmpty(int32 column, int32 row,
									int32 columnCount, int32 rowCount);
			bool				_InsertItemIntoGrid(BLayoutItem* item);
			void				_ResizeGrid(int32 columnCount, int32 rowCount);
			Dimensions*			_DimensionsOf(BLayoutItem* item);
	static	const Info*			_InfoAt(const std::vector<Info>& infos, int32 index);
	static	Info*				_InfoAt(std::vector<Info>& infos, int32 index, bool resize);

			std::vector<std::vector<BLayoutItem*>> fGrid;
									// [column][row]: the item covering the cell
			int32				fColumnCount = 0;
			int32				fRowCount = 0;
			std::vector<Info>	fRowInfos;
			std::vector<Info>	fColumnInfos;
			std::vector<std::pair<BLayoutItem*, Dimensions>> fDimensions;
			int32				fMultiColumnItems = 0;
			int32				fMultiRowItems = 0;
};


} // namespace ngui
