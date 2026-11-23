/*
 * Copyright 2006-2009, Ingo Weinhold <ingo_weinhold@gmx.de>.
 * Copyright 2015, Rene Gollent, rene@gollent.com.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/SplitLayout.cpp (commit
 * b1d497bb69), without archiving. NewGUI:
 * - Sizes are pixel counts: the "+ 1" and "- 1" of Haiku's distances are
 *   gone; frames are half-open (an empty frame for a collapsed item).
 * - The item infos are kept by the layout (Haiku: the items' layout data);
 *   splitter items and layouters are owned by unique_ptr.
 * - Spacing and insets are kept as given and resolved by
 *   BLayoutUtils::ComposeSpacing() when they are used.
 * - DoLayout() places the items in LayoutArea() including its origin;
 *   Haiku started at the insets, which misplaced a split layout nested in
 *   another layout.
 * - _SetSplitterValue() adds the maxima of both items in 64 bits; two
 *   unlimited maxima overflowed int32 in Haiku (undefined behaviour).
 */


#include "SplitLayout.h"

#include <algorithm>
#include <cstdint>

#include <ngui/LayoutItem.h>
#include <ngui/LayoutUtils.h>
#include <ngui/View.h>

#include "layout/OneElementLayouter.h"
#include "layout/SimpleLayouter.h"


namespace ngui {


using layout::Layouter;
using layout::LayoutInfo;
using layout::OneElementLayouter;
using layout::SimpleLayouter;


class BSplitLayout::ItemLayoutInfo {
public:
	float		weight = 1.0f;
	gfx::Rect	layoutFrame = {0, 0, 0, 0};
	gfx::Size	min;
	gfx::Size	max;
	bool		isVisible = true;
	bool		isCollapsible = true;
};


class BSplitLayout::ValueRange {
public:
	int32 sumValue;	// including spacing
	int32 previousMin;
	int32 previousMax;
	int32 previousSize;
	int32 nextMin;
	int32 nextMax;
	int32 nextSize;
};


class BSplitLayout::SplitterItem : public BLayoutItem {
public:
	SplitterItem(BSplitLayout* layout)
		:
		fLayout(layout),
		fFrame()
	{
	}

	gfx::Size MinSize() override
	{
		// NewGUI: pixel counts
		if (fLayout->Orientation() == BOrientation::Horizontal)
			return {fLayout->SplitterSize(), 0};
		else
			return {0, fLayout->SplitterSize()};
	}

	gfx::Size MaxSize() override
	{
		if (fLayout->Orientation() == BOrientation::Horizontal)
			return {fLayout->SplitterSize(), kSizeUnlimited};
		else
			return {kSizeUnlimited, fLayout->SplitterSize()};
	}

	gfx::Size PreferredSize() override
	{
		return MinSize();
	}

	BAlignment Alignment() override
	{
		return {BHorizontalAlignment::Center, BVerticalAlignment::Middle};
	}

	void SetExplicitMinSize(gfx::Size /*size*/) override
	{
		// not allowed
	}

	void SetExplicitMaxSize(gfx::Size /*size*/) override
	{
		// not allowed
	}

	void SetExplicitPreferredSize(gfx::Size /*size*/) override
	{
		// not allowed
	}

	void SetExplicitAlignment(BAlignment /*alignment*/) override
	{
		// not allowed
	}

	bool IsVisible() override
	{
		return true;
	}

	void SetVisible(bool /*visible*/) override
	{
		// not allowed
	}


	gfx::Rect Frame() override
	{
		return fFrame;
	}

	void SetFrame(gfx::Rect frame) override
	{
		fFrame = frame;
	}

private:
	BSplitLayout*	fLayout;
	gfx::Rect		fFrame;
};


// #pragma mark -


BSplitLayout::BSplitLayout(BOrientation orientation, float spacing)
	:
	fOrientation(orientation),
	fSpacing(spacing)
{
}


BSplitLayout::~BSplitLayout()
{
}


void
BSplitLayout::SetInsets(float left, float top, float right, float bottom)
{
	fLeftInset = left;
	fTopInset = top;
	fRightInset = right;
	fBottomInset = bottom;

	InvalidateLayout();
}


void
BSplitLayout::GetInsets(float* left, float* top, float* right, float* bottom) const
{
	if (left)
		*left = _LeftInset();
	if (top)
		*top = _TopInset();
	if (right)
		*right = _RightInset();
	if (bottom)
		*bottom = _BottomInset();
}


float
BSplitLayout::Spacing() const
{
	return BLayoutUtils::ComposeSpacing(fSpacing);
}


void
BSplitLayout::SetSpacing(float spacing)
{
	if (spacing != fSpacing) {
		fSpacing = spacing;

		InvalidateLayout();
	}
}


BOrientation
BSplitLayout::Orientation() const
{
	return fOrientation;
}


void
BSplitLayout::SetOrientation(BOrientation orientation)
{
	if (orientation != fOrientation) {
		fOrientation = orientation;

		InvalidateLayout();
	}
}


float
BSplitLayout::SplitterSize() const
{
	return fSplitterSize;
}


void
BSplitLayout::SetSplitterSize(float size)
{
	if (size != fSplitterSize) {
		fSplitterSize = size;

		InvalidateLayout();
	}
}


BLayoutItem*
BSplitLayout::AddView(std::unique_ptr<BView> child, float weight)
{
	return AddView(-1, std::move(child), weight);
}


BLayoutItem*
BSplitLayout::AddView(int32 index, std::unique_ptr<BView> child, float weight)
{
	BLayoutItem* item = AddView(index, std::move(child));
	if (item)
		SetItemWeight(item, weight);

	return item;
}


BLayoutItem*
BSplitLayout::AddItem(std::unique_ptr<BLayoutItem> item, float weight)
{
	return AddItem(-1, std::move(item), weight);
}


BLayoutItem*
BSplitLayout::AddItem(int32 index, std::unique_ptr<BLayoutItem> item, float weight)
{
	BLayoutItem* added = AddItem(index, std::move(item));
	if (added)
		SetItemWeight(added, weight);

	return added;
}


float
BSplitLayout::ItemWeight(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return 0;

	return ItemWeight(ItemAt(index));
}


float
BSplitLayout::ItemWeight(BLayoutItem* item) const
{
	if (ItemLayoutInfo* info = _ItemLayoutInfo(item))
		return info->weight;
	return 0;
}


void
BSplitLayout::SetItemWeight(int32 index, float weight, bool invalidateLayout)
{
	if (index < 0 || index >= CountItems())
		return;

	BLayoutItem* item = ItemAt(index);
	SetItemWeight(item, weight);

	if (fHorizontalLayouter) {
		const auto found = std::find(fVisibleItems.begin(), fVisibleItems.end(), item);
		if (found != fVisibleItems.end()) {
			const auto visibleIndex = static_cast<int32>(found - fVisibleItems.begin());
			if (fOrientation == BOrientation::Horizontal)
				fHorizontalLayouter->SetWeight(visibleIndex, weight);
			else
				fVerticalLayouter->SetWeight(visibleIndex, weight);
		}
	}

	if (invalidateLayout)
		InvalidateLayout();
}


void
BSplitLayout::SetItemWeight(BLayoutItem* item, float weight)
{
	if (ItemLayoutInfo* info = _ItemLayoutInfo(item))
		info->weight = weight;
}


bool
BSplitLayout::IsCollapsible(int32 index) const
{
	// NewGUI: false for an invalid index (Haiku crashed)
	ItemLayoutInfo* info = _ItemLayoutInfo(ItemAt(index));
	return info != nullptr && info->isCollapsible;
}


void
BSplitLayout::SetCollapsible(bool collapsible)
{
	SetCollapsible(0, CountItems() - 1, collapsible);
}


void
BSplitLayout::SetCollapsible(int32 index, bool collapsible)
{
	SetCollapsible(index, index, collapsible);
}


void
BSplitLayout::SetCollapsible(int32 first, int32 last, bool collapsible)
{
	for (int32 i = first; i <= last; i++) {
		if (ItemLayoutInfo* info = _ItemLayoutInfo(ItemAt(i)))
			info->isCollapsible = collapsible;
	}
}


bool
BSplitLayout::IsItemCollapsed(int32 index) const
{
	ItemLayoutInfo* info = _ItemLayoutInfo(ItemAt(index));
	return info != nullptr && !info->isVisible;
}


void
BSplitLayout::SetItemCollapsed(int32 index, bool collapsed)
{
	if (BLayoutItem* item = ItemAt(index)) {
		item->SetVisible(!collapsed);

		InvalidateLayout(true);
	}
}


gfx::Size
BSplitLayout::BaseMinSize()
{
	_ValidateMinMax();

	return _AddInsets(fMin);
}


gfx::Size
BSplitLayout::BaseMaxSize()
{
	_ValidateMinMax();

	return _AddInsets(fMax);
}


gfx::Size
BSplitLayout::BasePreferredSize()
{
	_ValidateMinMax();

	return _AddInsets(fPreferred);
}


BAlignment
BSplitLayout::BaseAlignment()
{
	return BAbstractLayout::BaseAlignment();
}


bool
BSplitLayout::HasHeightForWidth()
{
	_ValidateMinMax();

	return !fHeightForWidthItems.empty();
}


void
BSplitLayout::GetHeightForWidth(float width, float* min, float* max, float* preferred)
{
	if (!HasHeightForWidth())
		return;

	float innerWidth = _SubtractInsets({width, 0}).w;
	_InternalGetHeightForWidth(innerWidth, false, min, max, preferred);
	_AddInsets(min, max, preferred);
}


void
BSplitLayout::LayoutInvalidated(bool /*children*/)
{
	fHorizontalLayouter.reset();
	fVerticalLayouter.reset();
	fHorizontalLayoutInfo.reset();
	fVerticalLayoutInfo.reset();

	_InvalidateCachedHeightForWidth();

	fLayoutValid = false;
}


void
BSplitLayout::DoLayout()
{
	_ValidateMinMax();

	// layout the elements
	const gfx::Rect area = LayoutArea();
	gfx::Size size = _SubtractInsets({area.Width(), area.Height()});
	fHorizontalLayouter->Layout(*fHorizontalLayoutInfo, size.w);

	Layouter* verticalLayouter;
	if (HasHeightForWidth()) {
		float minHeight, maxHeight, preferredHeight;
		_InternalGetHeightForWidth(size.w, true, &minHeight, &maxHeight,
			&preferredHeight);
		size.h = std::max(size.h, minHeight);
		verticalLayouter = fHeightForWidthVerticalLayouter.get();
	} else
		verticalLayouter = fVerticalLayouter.get();

	verticalLayouter->Layout(*fVerticalLayoutInfo, size.h);

	// NewGUI: from the origin of the layout area
	float xOffset = area.x0 + _LeftInset();
	float yOffset = area.y0 + _TopInset();
	float splitterWidth = 0;	// pixel counts, no distances
	float splitterHeight = 0;	//
	float xSpacing = 0;
	float ySpacing = 0;
	const float spacing = Spacing();
	if (fOrientation == BOrientation::Horizontal) {
		splitterWidth = fSplitterSize;
		splitterHeight = size.h;
		xSpacing = spacing;
	} else {
		splitterWidth = size.w;
		splitterHeight = fSplitterSize;
		ySpacing = spacing;
	}

	int itemCount = CountItems();
	for (int i = 0; i < itemCount; i++) {
		// layout the splitter
		if (i > 0) {
			SplitterItem* splitterItem = _SplitterItemAt(i - 1);

			_LayoutItem(splitterItem, {xOffset, yOffset,
				xOffset + splitterWidth, yOffset + splitterHeight}, true);

			if (fOrientation == BOrientation::Horizontal)
				xOffset += splitterWidth + xSpacing;
			else
				yOffset += splitterHeight + ySpacing;
		}

		// layout the item
		BLayoutItem* item = ItemAt(i);
		const auto found = std::find(fVisibleItems.begin(), fVisibleItems.end(), item);
		if (found == fVisibleItems.end()) {
			_LayoutItem(item, gfx::Rect{}, false);
			continue;
		}
		const auto visibleIndex = static_cast<int32>(found - fVisibleItems.begin());

		// get the dimensions of the item
		float width = fHorizontalLayoutInfo->ElementSize(visibleIndex);
		float height = fVerticalLayoutInfo->ElementSize(visibleIndex);

		// place the component
		_LayoutItem(item, {xOffset, yOffset, xOffset + width, yOffset + height}, true);

		if (fOrientation == BOrientation::Horizontal)
			xOffset += width + xSpacing;
		else
			yOffset += height + ySpacing;
	}

	fLayoutValid = true;
}


gfx::Rect
BSplitLayout::SplitterItemFrame(int32 index) const
{
	if (SplitterItem* item = _SplitterItemAt(index))
		return item->Frame();
	return {};
}


bool
BSplitLayout::IsAboveSplitter(gfx::Point point) const
{
	return _SplitterItemAt(point) != nullptr;
}


bool
BSplitLayout::StartDraggingSplitter(gfx::Point point)
{
	StopDraggingSplitter();

	// Layout must be valid. Bail out, if it isn't.
	if (!fLayoutValid)
		return false;

	// Things shouldn't be draggable, if we have a >= max layout.
	const gfx::Rect area = LayoutArea();
	gfx::Size size = _SubtractInsets({area.Width(), area.Height()});
	if ((fOrientation == BOrientation::Horizontal && size.w >= fMax.w)
		|| (fOrientation == BOrientation::Vertical && size.h >= fMax.h)) {
		return false;
	}

	int32 index = -1;
	if (_SplitterItemAt(point, &index) != nullptr) {
		fDraggingStartPoint = Owner()->ConvertToScreen(point);
		fDraggingStartValue = _SplitterValue(index);
		fDraggingCurrentValue = fDraggingStartValue;
		fDraggingSplitterIndex = index;

		return true;
	}

	return false;
}


bool
BSplitLayout::DragSplitter(gfx::Point point)
{
	if (fDraggingSplitterIndex < 0)
		return false;

	point = Owner()->ConvertToScreen(point);

	int32 valueDiff;
	if (fOrientation == BOrientation::Horizontal)
		valueDiff = int32(point.x - fDraggingStartPoint.x);
	else
		valueDiff = int32(point.y - fDraggingStartPoint.y);

	return _SetSplitterValue(fDraggingSplitterIndex,
		fDraggingStartValue + valueDiff);
}


bool
BSplitLayout::StopDraggingSplitter()
{
	if (fDraggingSplitterIndex < 0)
		return false;

	// update the item weights
	_UpdateSplitterWeights();

	fDraggingSplitterIndex = -1;

	return true;
}


int32
BSplitLayout::DraggedSplitter() const
{
	return fDraggingSplitterIndex;
}


bool
BSplitLayout::ItemAdded(BLayoutItem* item, int32 /*atIndex*/)
{
	if (CountItems() > 1) {
		auto splitter = std::make_unique<SplitterItem>(this);
		auto splitterInfo = std::make_unique<ItemLayoutInfo>();
		splitterInfo->weight = 0;
		fItemInfos[splitter.get()] = std::move(splitterInfo);
		fSplitterItems.push_back(std::move(splitter));
	}

	fItemInfos[item] = std::make_unique<ItemLayoutInfo>();
	SetItemWeight(item, 1);

	return true;
}


void
BSplitLayout::ItemRemoved(BLayoutItem* item, int32 /*fromIndex*/)
{
	if (!fSplitterItems.empty()) {
		fItemInfos.erase(fSplitterItems.back().get());
		fSplitterItems.pop_back();
	}

	fItemInfos.erase(item);
}


// #pragma mark - private


void
BSplitLayout::_InvalidateCachedHeightForWidth()
{
	fHeightForWidthVerticalLayouter.reset();
	fHeightForWidthHorizontalLayoutInfo.reset();

	fCachedHeightForWidthWidth = -2;
	fHeightForWidthVerticalLayouterWidth = -2;
}


BSplitLayout::SplitterItem*
BSplitLayout::_SplitterItemAt(gfx::Point point, int32* index) const
{
	const auto splitterCount = static_cast<int32>(fSplitterItems.size());
	for (int32 i = 0; i < splitterCount; i++) {
		SplitterItem* splitItem = _SplitterItemAt(i);
		gfx::Rect frame = splitItem->Frame();
		if (frame.Contains(point)) {
			if (index != nullptr)
				*index = i;
			return splitItem;
		}
	}
	return nullptr;
}


BSplitLayout::SplitterItem*
BSplitLayout::_SplitterItemAt(int32 index) const
{
	if (index < 0 || index >= static_cast<int32>(fSplitterItems.size()))
		return nullptr;
	return fSplitterItems[static_cast<std::size_t>(index)].get();
}


void
BSplitLayout::_GetSplitterValueRange(int32 index, ValueRange& range)
{
	ItemLayoutInfo* previousInfo = _ItemLayoutInfo(ItemAt(index));
	ItemLayoutInfo* nextInfo = _ItemLayoutInfo(ItemAt(index + 1));

	// NewGUI: pixel counts (Haiku: distances + 1)
	if (fOrientation == BOrientation::Horizontal) {
		range.previousMin = (int32)previousInfo->min.w;
		range.previousMax = (int32)previousInfo->max.w;
		range.previousSize = (int32)previousInfo->layoutFrame.Width();
		range.nextMin = (int32)nextInfo->min.w;
		range.nextMax = (int32)nextInfo->max.w;
		range.nextSize = (int32)nextInfo->layoutFrame.Width();
	} else {
		range.previousMin = (int32)previousInfo->min.h;
		range.previousMax = (int32)previousInfo->max.h;
		range.previousSize = (int32)previousInfo->layoutFrame.Height();
		range.nextMin = (int32)nextInfo->min.h;
		range.nextMax = (int32)nextInfo->max.h;
		range.nextSize = (int32)nextInfo->layoutFrame.Height();
	}

	range.sumValue = range.previousSize + range.nextSize;
	if (previousInfo->isVisible)
		range.sumValue += (int32)Spacing();
	if (nextInfo->isVisible)
		range.sumValue += (int32)Spacing();
}


int32
BSplitLayout::_SplitterValue(int32 index) const
{
	ItemLayoutInfo* info = _ItemLayoutInfo(ItemAt(index));
	if (info && info->isVisible) {
		if (fOrientation == BOrientation::Horizontal)
			return (int32)info->layoutFrame.Width() + (int32)Spacing();
		else
			return (int32)info->layoutFrame.Height() + (int32)Spacing();
	} else
		return 0;
}


void
BSplitLayout::_LayoutItem(BLayoutItem* item, gfx::Rect frame, bool visible)
{
	// update the layout frame
	ItemLayoutInfo* info = _ItemLayoutInfo(item);
	info->isVisible = visible;
	if (visible)
		info->layoutFrame = frame;
	else
		info->layoutFrame = {0, 0, 0, 0};

	// update min/max
	info->min = item->MinSize();
	info->max = item->MaxSize();

	if (item->HasHeightForWidth()) {
		const gfx::Rect area = LayoutArea();
		gfx::Size size = _SubtractInsets({area.Width(), area.Height()});
		float minHeight, maxHeight;
		item->GetHeightForWidth(size.w, &minHeight, &maxHeight, nullptr);
		info->min.h = std::max(info->min.h, minHeight);
		info->max.h = std::min(info->max.h, maxHeight);
	}

	// layout the item
	if (visible)
		item->AlignInFrame(frame);
}


void
BSplitLayout::_LayoutItem(BLayoutItem* item, ItemLayoutInfo* info)
{
	// update the visibility of the item
	bool isVisible = item->IsVisible();
	bool visibilityChanged = (info->isVisible != isVisible);
	if (visibilityChanged)
		item->SetVisible(info->isVisible);

	// nothing more to do, if the item is not visible
	if (!info->isVisible)
		return;

	item->AlignInFrame(info->layoutFrame);

	// if the item became visible, we need to update its internal layout
	if (visibilityChanged &&
		(fOrientation != BOrientation::Horizontal || !HasHeightForWidth())) {
		item->Relayout(true);
	}
}


bool
BSplitLayout::_SetSplitterValue(int32 index, int32 value)
{
	// if both items are collapsed, nothing can be dragged
	BLayoutItem* previousItem = ItemAt(index);
	BLayoutItem* nextItem = ItemAt(index + 1);
	ItemLayoutInfo* previousInfo = _ItemLayoutInfo(previousItem);
	ItemLayoutInfo* nextInfo = _ItemLayoutInfo(nextItem);
	ItemLayoutInfo* splitterInfo = _ItemLayoutInfo(_SplitterItemAt(index));
	bool previousVisible = previousInfo->isVisible;
	bool nextVisible = nextInfo->isVisible;
	if (!previousVisible && !nextVisible)
		return false;

	const auto spacing = (int32)Spacing();

	ValueRange range;
	_GetSplitterValueRange(index, range);

	value = std::max(std::min(value, range.sumValue), -spacing);

	int32 previousSize = value - spacing;
	int32 nextSize = range.sumValue - value - spacing;

	// Note: While this collapsed-check is mathmatically correct (i.e. we
	// collapse an item, if it would become smaller than half its minimum
	// size), we might want to change it, since for the user it looks like
	// collapsing happens earlier. The reason being that the only visual mark
	// the user has is the mouse cursor which indeed hasn't crossed the middle
	// of the item yet.
	bool previousCollapsed = (previousSize <= range.previousMin / 2)
		&& previousInfo->isCollapsible;
	bool nextCollapsed = (nextSize <= range.nextMin / 2)
		&& nextInfo->isCollapsible;
	if (previousCollapsed && nextCollapsed) {
		// we cannot collapse both items; we have to decide for one
		if (previousSize < nextSize) {
			// collapse previous
			nextCollapsed = false;
			nextSize = range.sumValue - spacing;
		} else {
			// collapse next
			previousCollapsed = false;
			previousSize = range.sumValue - spacing;
		}
	}

	if (previousCollapsed || nextCollapsed) {
		// one collapsed item -- check whether that violates the constraints
		// of the other one
		int32 availableSpace = range.sumValue - spacing;
		if (previousCollapsed) {
			if (availableSpace < range.nextMin
				|| availableSpace > range.nextMax) {
				// we cannot collapse the previous item
				previousCollapsed = false;
			}
		} else {
			if (availableSpace < range.previousMin
				|| availableSpace > range.previousMax) {
				// we cannot collapse the next item
				nextCollapsed = false;
			}
		}
	}

	if (!(previousCollapsed || nextCollapsed)) {
		// no collapsed item -- check whether there is a close solution
		previousSize = value - spacing;
		nextSize = range.sumValue - value - spacing;

		if (range.previousMin + range.nextMin + 2 * spacing > range.sumValue) {
			// we don't have enough space to uncollapse both items
			int32 availableSpace = range.sumValue - spacing;
			if (previousSize < nextSize && availableSpace >= range.nextMin
				&& availableSpace <= range.nextMax
				&& previousInfo->isCollapsible) {
				previousCollapsed = true;
			} else if (availableSpace >= range.previousMin
				&& availableSpace <= range.previousMax
				&& nextInfo->isCollapsible) {
				nextCollapsed = true;
			} else if (availableSpace >= range.nextMin
				&& availableSpace <= range.nextMax
				&& previousInfo->isCollapsible) {
				previousCollapsed = true;
			} else {
				if (previousSize < nextSize && previousInfo->isCollapsible) {
					previousCollapsed = true;
				} else if (nextInfo->isCollapsible) {
					nextCollapsed = true;
				} else {
					// Neither item is collapsible although there's not enough
					// space: Give them both their minimum size.
					previousSize = range.previousMin;
					nextSize = range.nextMin;
				}
			}

		} else {
			// there is enough space for both items
			// make sure the min constraints are satisfied
			if (previousSize < range.previousMin) {
				previousSize = range.previousMin;
				nextSize = range.sumValue - previousSize - 2 * spacing;
			} else if (nextSize < range.nextMin) {
				nextSize = range.nextMin;
				previousSize = range.sumValue - nextSize - 2 * spacing;
			}

			// if we can, also satisfy the max constraints
			// NewGUI: summed in 64 bits; two unlimited maxima (2^30 each)
			// overflowed int32 in Haiku.
			if ((int64_t)range.previousMax + range.nextMax + 2 * spacing
					>= range.sumValue) {
				if (previousSize > range.previousMax) {
					previousSize = range.previousMax;
					nextSize = range.sumValue - previousSize
						- 2 * spacing;
				} else if (nextSize > range.nextMax) {
					nextSize = range.nextMax;
					previousSize = range.sumValue - nextSize
						- 2 * spacing;
				}
			}
		}
	}

	// compute the size for one collapsed item; for none collapsed item we
	// already have correct values
	if (previousCollapsed || nextCollapsed) {
		int32 availableSpace = range.sumValue - spacing;
		if (previousCollapsed) {
			previousSize = 0;
			nextSize = availableSpace;
		} else {
			previousSize = availableSpace;
			nextSize = 0;
		}
	}

	int32 newValue = previousSize + (previousCollapsed ? 0 : spacing);
	if (newValue == fDraggingCurrentValue) {
		// nothing changed
		return false;
	}

	// something changed: we need to recompute the layout
	int32 baseOffset = -fDraggingCurrentValue;
		// offset to the current splitter position
	int32 splitterOffset = baseOffset + newValue;
	int32 nextOffset = splitterOffset + (int32)fSplitterSize + spacing;

	// NewGUI: half-open frames
	gfx::Rect splitterFrame(splitterInfo->layoutFrame);
	if (fOrientation == BOrientation::Horizontal) {
		// horizontal layout
		// previous item
		float left = splitterFrame.x0 + (float)baseOffset;
		previousInfo->layoutFrame = {left, splitterFrame.y0,
			left + (float)previousSize, splitterFrame.y1};

		// next item
		left = splitterFrame.x0 + (float)nextOffset;
		nextInfo->layoutFrame = {left, splitterFrame.y0,
			left + (float)nextSize, splitterFrame.y1};

		// splitter
		splitterInfo->layoutFrame.x0 += (float)splitterOffset;
		splitterInfo->layoutFrame.x1 += (float)splitterOffset;
	} else {
		// vertical layout
		// previous item
		float top = splitterFrame.y0 + (float)baseOffset;
		previousInfo->layoutFrame = {splitterFrame.x0, top,
			splitterFrame.x1, top + (float)previousSize};

		// next item
		top = splitterFrame.y0 + (float)nextOffset;
		nextInfo->layoutFrame = {splitterFrame.x0, top,
			splitterFrame.x1, top + (float)nextSize};

		// splitter
		splitterInfo->layoutFrame.y0 += (float)splitterOffset;
		splitterInfo->layoutFrame.y1 += (float)splitterOffset;
	}

	previousInfo->isVisible = !previousCollapsed;
	nextInfo->isVisible = !nextCollapsed;

	bool heightForWidth = (fOrientation == BOrientation::Horizontal && HasHeightForWidth());

	// If the item visibility is to be changed, we need to update the splitter
	// values now, since the visibility change will cause an invalidation.
	if (previousVisible != previousInfo->isVisible
		|| nextVisible != nextInfo->isVisible || heightForWidth) {
		_UpdateSplitterWeights();
	}

	// If we have height for width items, we need to invalidate the previous
	// and the next item. Actually we would only need to invalidate height for
	// width items, but since non height for width items might be aligned with
	// height for width items, we need to trigger a layout that creates a
	// context that spans all aligned items.
	// We invalidate already here, so that changing the items' size won't cause
	// an immediate relayout.
	if (heightForWidth) {
		previousItem->InvalidateLayout();
		nextItem->InvalidateLayout();
	}

	// do the layout
	_LayoutItem(previousItem, previousInfo);
	_LayoutItem(_SplitterItemAt(index), splitterInfo);
	_LayoutItem(nextItem, nextInfo);

	fDraggingCurrentValue = newValue;

	return true;
}


BSplitLayout::ItemLayoutInfo*
BSplitLayout::_ItemLayoutInfo(const BLayoutItem* item) const
{
	const auto found = fItemInfos.find(item);
	return found != fItemInfos.end() ? found->second.get() : nullptr;
}


void
BSplitLayout::_UpdateSplitterWeights()
{
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		float weight;
		if (fOrientation == BOrientation::Horizontal)
			weight = _ItemLayoutInfo(ItemAt(i))->layoutFrame.Width();
		else
			weight = _ItemLayoutInfo(ItemAt(i))->layoutFrame.Height();

		SetItemWeight(i, weight, false);
	}

	// Just updating the splitter weights is fine in principle. The next
	// LayoutItems() will use the correct values. But, if our orientation is
	// vertical, the cached height for width info needs to be flushed, or the
	// obsolete cached values will be used.
	if (fOrientation == BOrientation::Vertical)
		_InvalidateCachedHeightForWidth();
}


void
BSplitLayout::_ValidateMinMax()
{
	if (fHorizontalLayouter != nullptr)
		return;

	fLayoutValid = false;

	fVisibleItems.clear();
	fHeightForWidthItems.clear();
	_InvalidateCachedHeightForWidth();

	// filter the visible items
	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->IsVisible())
			fVisibleItems.push_back(item);

		// Add "height for width" items even, if they aren't visible. Otherwise
		// we may get our parent into trouble, since we could change from
		// "height for width" to "not height for width".
		if (item->HasHeightForWidth())
			fHeightForWidthItems.push_back(item);
	}
	itemCount = static_cast<int32>(fVisibleItems.size());

	// create the layouters
	std::unique_ptr<Layouter> itemLayouter = std::make_unique<SimpleLayouter>(itemCount, 0.0f);

	if (fOrientation == BOrientation::Horizontal) {
		fHorizontalLayouter = std::move(itemLayouter);
		fVerticalLayouter = std::make_unique<OneElementLayouter>();
	} else {
		fHorizontalLayouter = std::make_unique<OneElementLayouter>();
		fVerticalLayouter = std::move(itemLayouter);
	}

	// tell the layouters about our constraints
	if (itemCount > 0) {
		for (int32 i = 0; i < itemCount; i++) {
			BLayoutItem* item = fVisibleItems[static_cast<std::size_t>(i)];
			gfx::Size min = item->MinSize();
			gfx::Size max = item->MaxSize();
			gfx::Size preferred = item->PreferredSize();

			fHorizontalLayouter->AddConstraints(i, 1, min.w, max.w, preferred.w);
			fVerticalLayouter->AddConstraints(i, 1, min.h, max.h, preferred.h);

			float weight = ItemWeight(item);
			fHorizontalLayouter->SetWeight(i, weight);
			fVerticalLayouter->SetWeight(i, weight);
		}
	}

	fMin.w = fHorizontalLayouter->MinSize();
	fMin.h = fVerticalLayouter->MinSize();
	fMax.w = fHorizontalLayouter->MaxSize();
	fMax.h = fVerticalLayouter->MaxSize();
	fPreferred.w = fHorizontalLayouter->PreferredSize();
	fPreferred.h = fVerticalLayouter->PreferredSize();

	fHorizontalLayoutInfo = fHorizontalLayouter->CreateLayoutInfo();
	if (fHeightForWidthItems.empty())
		fVerticalLayoutInfo = fVerticalLayouter->CreateLayoutInfo();

	ResetLayoutInvalidation();
}


void
BSplitLayout::_InternalGetHeightForWidth(float width, bool realLayout, float* minHeight,
	float* maxHeight, float* preferredHeight)
{
	if ((realLayout && fHeightForWidthVerticalLayouterWidth != width)
		|| (!realLayout && fCachedHeightForWidthWidth != width)) {
		// The general strategy is to clone the vertical layouter, which only
		// knows the general min/max constraints, do a horizontal layout for the
		// given width, and add the children's height for width constraints to
		// the cloned vertical layouter. If this method is invoked internally,
		// we keep the cloned vertical layouter, for it will be used for doing
		// the layout. Otherwise we just drop it after we've got the height for
		// width info.

		// clone the vertical layouter and get the horizontal layout info to be used
		LayoutInfo* horizontalLayoutInfo = nullptr;
		std::unique_ptr<Layouter> cloned = fVerticalLayouter->CloneLayouter();
		Layouter* verticalLayouter = cloned.get();
		if (realLayout) {
			horizontalLayoutInfo = fHorizontalLayoutInfo.get();
			fHeightForWidthVerticalLayouter = std::move(cloned);
			fVerticalLayoutInfo = verticalLayouter->CreateLayoutInfo();
			fHeightForWidthVerticalLayouterWidth = width;
		} else {
			if (fHeightForWidthHorizontalLayoutInfo == nullptr)
				fHeightForWidthHorizontalLayoutInfo = fHorizontalLayouter->CreateLayoutInfo();
			horizontalLayoutInfo = fHeightForWidthHorizontalLayoutInfo.get();
		}

		// do the horizontal layout (already done when doing this for the real
		// layout)
		if (!realLayout)
			fHorizontalLayouter->Layout(*horizontalLayoutInfo, width);

		// add the children's height for width constraints
		for (BLayoutItem* item : fHeightForWidthItems) {
			const auto found = std::find(fVisibleItems.begin(), fVisibleItems.end(), item);
			if (found != fVisibleItems.end()) {
				const auto index = static_cast<int32>(found - fVisibleItems.begin());
				float itemMinHeight, itemMaxHeight, itemPreferredHeight;
				item->GetHeightForWidth(
					horizontalLayoutInfo->ElementSize(index),
					&itemMinHeight, &itemMaxHeight, &itemPreferredHeight);
				verticalLayouter->AddConstraints(index, 1, itemMinHeight,
					itemMaxHeight, itemPreferredHeight);
			}
		}

		// get the height for width info
		fCachedHeightForWidthWidth = width;
		fCachedMinHeightForWidth = verticalLayouter->MinSize();
		fCachedMaxHeightForWidth = verticalLayouter->MaxSize();
		fCachedPreferredHeightForWidth = verticalLayouter->PreferredSize();
	}

	if (minHeight)
		*minHeight = fCachedMinHeightForWidth;
	if (maxHeight)
		*maxHeight = fCachedMaxHeightForWidth;
	if (preferredHeight)
		*preferredHeight = fCachedPreferredHeightForWidth;
}


float
BSplitLayout::_SplitterSpace() const
{
	const auto splitters = static_cast<float>(fSplitterItems.size());
	float space = 0;
	if (splitters > 0) {
		space = (static_cast<float>(fVisibleItems.size()) + splitters - 1) * Spacing()
			+ splitters * fSplitterSize;
	}

	return space;
}


float
BSplitLayout::_LeftInset() const
{
	return BLayoutUtils::ComposeSpacing(fLeftInset);
}


float
BSplitLayout::_TopInset() const
{
	return BLayoutUtils::ComposeSpacing(fTopInset);
}


float
BSplitLayout::_RightInset() const
{
	return BLayoutUtils::ComposeSpacing(fRightInset);
}


float
BSplitLayout::_BottomInset() const
{
	return BLayoutUtils::ComposeSpacing(fBottomInset);
}


gfx::Size
BSplitLayout::_AddInsets(gfx::Size size)
{
	// NewGUI: pixel counts
	size.w = BLayoutUtils::AddDistances(size.w, _LeftInset() + _RightInset());
	size.h = BLayoutUtils::AddDistances(size.h, _TopInset() + _BottomInset());

	float spacing = _SplitterSpace();
	if (fOrientation == BOrientation::Horizontal)
		size.w = BLayoutUtils::AddDistances(size.w, spacing);
	else
		size.h = BLayoutUtils::AddDistances(size.h, spacing);

	return size;
}


void
BSplitLayout::_AddInsets(float* minHeight, float* maxHeight, float* preferredHeight)
{
	float insets = _TopInset() + _BottomInset();
	if (fOrientation == BOrientation::Vertical)
		insets += _SplitterSpace();
	if (minHeight)
		*minHeight = BLayoutUtils::AddDistances(*minHeight, insets);
	if (maxHeight)
		*maxHeight = BLayoutUtils::AddDistances(*maxHeight, insets);
	if (preferredHeight)
		*preferredHeight = BLayoutUtils::AddDistances(*preferredHeight, insets);
}


gfx::Size
BSplitLayout::_SubtractInsets(gfx::Size size)
{
	size.w = BLayoutUtils::SubtractDistances(size.w, _LeftInset() + _RightInset());
	size.h = BLayoutUtils::SubtractDistances(size.h, _TopInset() + _BottomInset());

	float spacing = _SplitterSpace();
	if (fOrientation == BOrientation::Horizontal)
		size.w = BLayoutUtils::SubtractDistances(size.w, spacing);
	else
		size.h = BLayoutUtils::SubtractDistances(size.h, spacing);

	return size;
}


} // namespace ngui
