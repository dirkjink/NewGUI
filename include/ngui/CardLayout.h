/*
 * Copyright 2006-2009, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/AbstractLayout.h>


namespace ngui {


/*!	Shows one of its items at a time, in the whole layout area (Haiku's
	BCardLayout): the pages of a wizard or of tabs. Its sizes are those of
	all items together, so that switching pages does not resize the
	window. The first item added is shown. */
class BCardLayout : public BAbstractLayout {
public:
								BCardLayout();
								~BCardLayout() override;
									/*!< Destroys the layout. */

			BLayoutItem*		VisibleItem() const;
									/*!< The item shown, or nullptr. */
			int32				VisibleIndex() const;
									/*!< Its index, or -1. */
			void				SetVisibleItem(int32 index);
									/*!< Shows item \a index (none if out of range). */
			void				SetVisibleItem(BLayoutItem* item);
									/*!< Shows \a item, which must be an item of the
										layout, or none for nullptr. */

			gfx::Size			BaseMinSize() override;
									/*!< The largest minimum of the items. */
			gfx::Size			BaseMaxSize() override;
									/*!< The smallest maximum, at least the minimum. */
			gfx::Size			BasePreferredSize() override;
									/*!< The largest preferred size, within the limits. */
			BAlignment			BaseAlignment() override;
									/*!< Full width and height. */

			bool				HasHeightForWidth() override;
									/*!< Whether an item has height for width. */
			void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred) override;
									/*!< The heights of all items for \a width. */

protected:
			void				LayoutInvalidated(bool children) override;
									/*!< Drops the computed sizes. */
			void				DoLayout() override;
									/*!< Places the visible item. */

			bool				ItemAdded(BLayoutItem* item, int32 atIndex) override;
									/*!< Shows the first item, hides the others. */
			void				ItemRemoved(BLayoutItem* item, int32 fromIndex) override;
									/*!< Shows none if the visible one is removed. */

private:
			void				_ValidateMinMax();

			gfx::Size			fMin;
			gfx::Size			fMax;
			gfx::Size			fPreferred;
			BLayoutItem*		fVisibleItem;
			bool				fMinMaxValid;
};


} // namespace ngui
