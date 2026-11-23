/*
 * Copyright 2006-2010, Haiku Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/private/interface/SplitLayout.h (commit
 * b1d497bb69). Internal, as in Haiku: BSplitView is its interface.
 */
#pragma once


#include <memory>
#include <unordered_map>
#include <vector>

#include <ngui/AbstractLayout.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Point.h>

#include "layout/Layouter.h"


namespace ngui {


// Items in a row or a column with a splitter between each two that the user
// can drag, collapsing items that become too small.
class BSplitLayout : public BAbstractLayout {
public:
								BSplitLayout(BOrientation orientation,
									float spacing = 0.0f);
								~BSplitLayout() override;

			void				SetInsets(float left, float top, float right,
									float bottom);
			void				GetInsets(float* left, float* top, float* right,
									float* bottom) const;

			float				Spacing() const;
			void				SetSpacing(float spacing);

			BOrientation		Orientation() const;
			void				SetOrientation(BOrientation orientation);

			float				SplitterSize() const;
			void				SetSplitterSize(float size);

			using				BLayout::AddView;
			using				BLayout::AddItem;
			BLayoutItem*		AddView(std::unique_ptr<BView> child, float weight);
			BLayoutItem*		AddView(int32 index, std::unique_ptr<BView> child,
									float weight);
			BLayoutItem*		AddItem(std::unique_ptr<BLayoutItem> item, float weight);
			BLayoutItem*		AddItem(int32 index, std::unique_ptr<BLayoutItem> item,
									float weight);

			float				ItemWeight(int32 index) const;
			float				ItemWeight(BLayoutItem* item) const;
			void				SetItemWeight(int32 index, float weight,
									bool invalidateLayout);
			void				SetItemWeight(BLayoutItem* item, float weight);

			bool				IsCollapsible(int32 index) const;
			void				SetCollapsible(bool collapsible);
			void				SetCollapsible(int32 index, bool collapsible);
			void				SetCollapsible(int32 first, int32 last,
									bool collapsible);

			bool				IsItemCollapsed(int32 index) const;
			void				SetItemCollapsed(int32 index, bool collapsed);

			gfx::Size			BaseMinSize() override;
			gfx::Size			BaseMaxSize() override;
			gfx::Size			BasePreferredSize() override;
			BAlignment			BaseAlignment() override;

			bool				HasHeightForWidth() override;
			void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred) override;

			void				LayoutInvalidated(bool children) override;
			void				DoLayout() override;

	// interface for BSplitView
			gfx::Rect			SplitterItemFrame(int32 index) const;
			bool				IsAboveSplitter(gfx::Point point) const;

			bool				StartDraggingSplitter(gfx::Point point);
			bool				DragSplitter(gfx::Point point);
			bool				StopDraggingSplitter();
			int32				DraggedSplitter() const;

protected:
			bool				ItemAdded(BLayoutItem* item, int32 atIndex) override;
			void				ItemRemoved(BLayoutItem* item, int32 fromIndex) override;

private:
			class ItemLayoutInfo;
			class ValueRange;
			class SplitterItem;

			void				_InvalidateCachedHeightForWidth();

			SplitterItem*		_SplitterItemAt(gfx::Point point,
									int32* index = nullptr) const;
			SplitterItem*		_SplitterItemAt(int32 index) const;

			void				_GetSplitterValueRange(int32 index,
									ValueRange& range);
			int32				_SplitterValue(int32 index) const;

			void				_LayoutItem(BLayoutItem* item, gfx::Rect frame,
									bool visible);
			void				_LayoutItem(BLayoutItem* item,
									ItemLayoutInfo* info);

			bool				_SetSplitterValue(int32 index, int32 value);

			ItemLayoutInfo*		_ItemLayoutInfo(const BLayoutItem* item) const;

			void				_UpdateSplitterWeights();

			void				_ValidateMinMax();

			void				_InternalGetHeightForWidth(float width,
									bool realLayout, float* minHeight,
									float* maxHeight, float* preferredHeight);

			float				_SplitterSpace() const;

			float				_LeftInset() const;
			float				_TopInset() const;
			float				_RightInset() const;
			float				_BottomInset() const;
			gfx::Size			_AddInsets(gfx::Size size);
			void				_AddInsets(float* minHeight, float* maxHeight,
									float* preferredHeight);
			gfx::Size			_SubtractInsets(gfx::Size size);

private:
			BOrientation		fOrientation;
			float				fLeftInset = 0;
			float				fRightInset = 0;
			float				fTopInset = 0;
			float				fBottomInset = 0;
			float				fSplitterSize = 6;
			float				fSpacing;

			std::vector<std::unique_ptr<SplitterItem>> fSplitterItems;
			std::vector<BLayoutItem*> fVisibleItems;
			std::unordered_map<const BLayoutItem*, std::unique_ptr<ItemLayoutInfo>>
									fItemInfos;

			gfx::Size			fMin;
			gfx::Size			fMax;
			gfx::Size			fPreferred;

			std::unique_ptr<layout::Layouter> fHorizontalLayouter;
			std::unique_ptr<layout::Layouter> fVerticalLayouter;

			std::unique_ptr<layout::LayoutInfo> fHorizontalLayoutInfo;
			std::unique_ptr<layout::LayoutInfo> fVerticalLayoutInfo;

			std::vector<BLayoutItem*> fHeightForWidthItems;
			// Incorporates the children's height for width constraints for a
			// concrete width. Cloned lazily from fVerticalLayout when needed.
			std::unique_ptr<layout::Layouter> fHeightForWidthVerticalLayouter;
			std::unique_ptr<layout::LayoutInfo> fHeightForWidthHorizontalLayoutInfo;
				// for computing height for width info

			bool				fLayoutValid = false;

			float				fCachedHeightForWidthWidth = -2;
			float				fHeightForWidthVerticalLayouterWidth = -2;
			float				fCachedMinHeightForWidth = -1;
			float				fCachedMaxHeightForWidth = -1;
			float				fCachedPreferredHeightForWidth = -1;

			gfx::Point			fDraggingStartPoint;
			int32				fDraggingStartValue = 0;
			int32				fDraggingCurrentValue = 0;
			int32				fDraggingSplitterIndex = -1;
};


} // namespace ngui
