/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <vector>

#include <ngui/TwoDimensionalLayout.h>


namespace ngui {


/*!	Places its items in a row or a column (Haiku's BGroupLayout), each with
	a weight for the space beyond the preferred sizes. The spacing between
	items is the look's item spacing by default.

	NewGUI: weights are kept by the layout (Haiku: in the items' layout
	data); AddView() and AddItem() take ownership as in BLayout. */
class BGroupLayout : public BTwoDimensionalLayout {
public:
								BGroupLayout(BOrientation orientation,
									float spacing = kUseDefaultSpacing);
									/*!< A row (Horizontal) or a column (Vertical). */
								~BGroupLayout() override;
									/*!< Destroys the layout. */

			float				Spacing() const;
									/*!< The spacing between items, constants resolved. */
			void				SetSpacing(float spacing);
									/*!< Sets the spacing (pixels or a spacing constant). */

			BOrientation		Orientation() const;
									/*!< Row or column. */
			void				SetOrientation(BOrientation orientation);
									/*!< Makes it a row or a column. */

			float				ItemWeight(int32 index) const;
									/*!< The weight of item \a index; 0 if there is none. */
			void				SetItemWeight(int32 index, float weight);
									/*!< Sets the weight of item \a index. */

			using				BLayout::AddView;
			using				BLayout::AddItem;

			BLayoutItem*		AddView(std::unique_ptr<BView> child, float weight);
									/*!< AddView() with \a weight. */
			BLayoutItem*		AddView(int32 index, std::unique_ptr<BView> child,
									float weight);
									/*!< AddView() at \a index with \a weight. */
			BLayoutItem*		AddItem(std::unique_ptr<BLayoutItem> item, float weight);
									/*!< AddItem() with \a weight. */
			BLayoutItem*		AddItem(int32 index, std::unique_ptr<BLayoutItem> item,
									float weight);
									/*!< AddItem() at \a index with \a weight. */

protected:
			bool				ItemAdded(BLayoutItem* item, int32 atIndex) override;
									/*!< Gives the item the weight 1. */
			void				ItemRemoved(BLayoutItem* item, int32 fromIndex) override;
									/*!< Drops the item's weight. */

			void				PrepareItems(BOrientation orientation) override;
									/*!< Collects the visible items. */

			int32				InternalCountColumns() override;
									/*!< The visible items in a row, else 1. */
			int32				InternalCountRows() override;
									/*!< The visible items in a column, else 1. */
			void				GetColumnRowConstraints(BOrientation orientation,
									int32 index,
									ColumnRowConstraints* constraints) override;
									/*!< The weight of the visible item \a index, no
										limits. */
			void				GetItemDimensions(BLayoutItem* item,
									Dimensions* dimensions) override;
									/*!< The cell of a visible item. */

private:
			float				_Weight(BLayoutItem* item) const;

			BOrientation		fOrientation;
			std::vector<std::pair<BLayoutItem*, float>> fWeights;
			std::vector<BLayoutItem*> fVisibleItems;
};


} // namespace ngui
