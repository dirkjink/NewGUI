/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/AbstractLayout.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/LayoutUtils.h>


namespace ngui {


/*!	The base of layouts that place their items on a grid, each item covering
	one or more cells (Haiku's BTwoDimensionalLayout): BGroupLayout and
	BGridLayout. Subclasses report the items' cells and the constraints of
	columns and rows; this class lays out both directions, height for width
	included.

	Insets and spacings may be spacing constants (kUseDefaultSpacing ...);
	they are kept as given and resolved whenever sizes are computed
	(BLayoutUtils::ComposeSpacing()). Sizes are pixel counts.
*/
class BTwoDimensionalLayout : public BAbstractLayout {
public:
								BTwoDimensionalLayout();
								~BTwoDimensionalLayout() override;
									/*!< Leaves the layouts it is aligned with. */

			void				SetInsets(float left, float top, float right,
									float bottom);
									/*!< Space between the layout area and the items. */
			void				SetInsets(float horizontal, float vertical);
									/*!< The same left and right, top and bottom. */
			void				SetInsets(float insets);
									/*!< The same on all sides. */
			void				GetInsets(float* left, float* top, float* right,
									float* bottom) const;
									/*!< The insets, spacing constants resolved; each
										pointer may be nullptr. */

			void				AlignLayoutWith(BTwoDimensionalLayout* other,
									BOrientation orientation);
									/*!< Lays out its columns (Horizontal) or rows
										(Vertical) together with \a other's, so that they
										line up, also across views. */

			gfx::Size			BaseMinSize() override;
									/*!< The items' minimums, with spacing and insets. */
			gfx::Size			BaseMaxSize() override;
									/*!< The items' maximums, with spacing and insets. */
			gfx::Size			BasePreferredSize() override;
									/*!< The items' preferred sizes, with spacing and
										insets. */
			BAlignment			BaseAlignment() override;
									/*!< Full width and height. */
			bool				HasHeightForWidth() override;
									/*!< Whether an item has height for width. */
			void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred) override;
									/*!< The heights for \a width. */
			void				SetFrame(gfx::Rect frame) override;
									/*!< Places the layout. */

protected:
			/*!	Constraints of a column or row; kSizeUnset for none. */
			struct ColumnRowConstraints {
				float	weight = 1;				/*!< Share of extra space. */
				float	min = kSizeUnset;		/*!< Smallest size. */
				float	max = kSizeUnset;		/*!< Largest size. */
			};

			/*!	The cells an item covers. */
			struct Dimensions {
				int32	x = 0;			/*!< First column. */
				int32	y = 0;			/*!< First row. */
				int32	width = 1;		/*!< Number of columns. */
				int32	height = 1;		/*!< Number of rows. */
			};

			void				LayoutInvalidated(bool children = false) override;
									/*!< Drops the computed constraints. */
			void				DoLayout() override;
									/*!< Lays out columns and rows and places the items. */

			gfx::Size			AddInsets(gfx::Size size);
									/*!< \a size with the insets. */
			void				AddInsets(float* minHeight, float* maxHeight,
									float* preferredHeight);
									/*!< The heights with the top and bottom insets. */
			gfx::Size			SubtractInsets(gfx::Size size);
									/*!< \a size without the insets. */

	virtual	void				PrepareItems(BOrientation orientation);
									/*!< Before the constraints of \a orientation are
										asked for. */
	virtual	bool				HasMultiColumnItems();
									/*!< Whether an item spans columns. */
	virtual	bool				HasMultiRowItems();
									/*!< Whether an item spans rows. */

	virtual	int32				InternalCountColumns() = 0;
									/*!< The number of columns. */
	virtual	int32				InternalCountRows() = 0;
									/*!< The number of rows. */
	virtual	void				GetColumnRowConstraints(BOrientation orientation,
									int32 index, ColumnRowConstraints* constraints) = 0;
									/*!< The constraints of column or row \a index. */
	virtual	void				GetItemDimensions(BLayoutItem* item,
									Dimensions* dimensions) = 0;
									/*!< The cells \a item covers. */

			float				fLeftInset = 0;		/*!< As given, maybe a constant. */
			float				fRightInset = 0;	/*!< As given, maybe a constant. */
			float				fTopInset = 0;		/*!< As given, maybe a constant. */
			float				fBottomInset = 0;	/*!< As given, maybe a constant. */
			float				fHSpacing = 0;		/*!< As given, maybe a constant. */
			float				fVSpacing = 0;		/*!< As given, maybe a constant. */

private:
			class CompoundLayouter;
			class LocalLayouter;
			class VerticalCompoundLayouter;

			friend class LocalLayouter;

			void				_ValidateMinMax();

			std::unique_ptr<LocalLayouter> fLocalLayouter;
};


} // namespace ngui
