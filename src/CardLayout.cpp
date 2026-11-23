/*
 * Copyright 2006-2009, Ingo Weinhold <ingo_weinhold@gmx.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/CardLayout.cpp (commit b1d497bb69),
 * without archiving. NewGUI: GetHeightForWidth() takes the largest preferred
 * height of the items, as _ValidateMinMax() does for the preferred size
 * (Haiku took the smallest); an item that is not in the layout is ignored
 * by SetVisibleItem() (Haiku called the debugger).
 */


#include <ngui/CardLayout.h>

#include <algorithm>

#include <ngui/LayoutItem.h>
#include <ngui/LayoutUtils.h>


namespace ngui {


BCardLayout::BCardLayout()
	:
	BAbstractLayout(),
	fMin{0, 0},
	fMax{kSizeUnlimited, kSizeUnlimited},
	fPreferred{0, 0},
	fVisibleItem(nullptr),
	fMinMaxValid(false)
{
}


BCardLayout::~BCardLayout()
{
}


BLayoutItem*
BCardLayout::VisibleItem() const
{
	return fVisibleItem;
}


int32
BCardLayout::VisibleIndex() const
{
	return IndexOfItem(fVisibleItem);
}


void
BCardLayout::SetVisibleItem(int32 index)
{
	SetVisibleItem(ItemAt(index));
}


void
BCardLayout::SetVisibleItem(BLayoutItem* item)
{
	if (item == fVisibleItem)
		return;

	if (item != nullptr && IndexOfItem(item) < 0)
		return;

	// Changing an item's visibility will invalidate its parent's layout (us),
	// which would normally cause the min-max to be re-computed. But in this
	// case, that is unnecessary, and so we can skip it.
	const bool minMaxValid = fMinMaxValid;

	if (fVisibleItem != nullptr)
		fVisibleItem->SetVisible(false);

	fVisibleItem = item;

	if (fVisibleItem != nullptr)
		fVisibleItem->SetVisible(true);

	fMinMaxValid = minMaxValid;
}


gfx::Size
BCardLayout::BaseMinSize()
{
	_ValidateMinMax();
	return fMin;
}


gfx::Size
BCardLayout::BaseMaxSize()
{
	_ValidateMinMax();
	return fMax;
}


gfx::Size
BCardLayout::BasePreferredSize()
{
	_ValidateMinMax();
	return fPreferred;
}


BAlignment
BCardLayout::BaseAlignment()
{
	return {BHorizontalAlignment::UseFullWidth, BVerticalAlignment::UseFullHeight};
}


bool
BCardLayout::HasHeightForWidth()
{
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		if (ItemAt(i)->HasHeightForWidth())
			return true;
	}

	return false;
}


void
BCardLayout::GetHeightForWidth(float width, float* min, float* max,
	float* preferred)
{
	_ValidateMinMax();

	// init with useful values
	float minHeight = fMin.h;
	float maxHeight = fMax.h;
	float preferredHeight = fPreferred.h;

	// apply the items' constraints
	int32 count = CountItems();
	for (int32 i = 0; i < count; i++) {
		BLayoutItem* item = ItemAt(i);
		if (item->HasHeightForWidth()) {
			float itemMinHeight;
			float itemMaxHeight;
			float itemPreferredHeight;
			item->GetHeightForWidth(width, &itemMinHeight, &itemMaxHeight,
				&itemPreferredHeight);
			minHeight = std::max(minHeight, itemMinHeight);
			maxHeight = std::min(maxHeight, itemMaxHeight);
			preferredHeight = std::max(preferredHeight, itemPreferredHeight);
		}
	}

	// adjust max and preferred, if necessary
	maxHeight = std::max(maxHeight, minHeight);
	preferredHeight = std::max(preferredHeight, minHeight);
	preferredHeight = std::min(preferredHeight, maxHeight);

	if (min)
		*min = minHeight;
	if (max)
		*max = maxHeight;
	if (preferred)
		*preferred = preferredHeight;
}


void
BCardLayout::LayoutInvalidated(bool /*children*/)
{
	fMinMaxValid = false;
}


void
BCardLayout::DoLayout()
{
	_ValidateMinMax();

	const gfx::Rect area = LayoutArea();
	gfx::Size size = {area.Width(), area.Height()};

	// this cannot be done when we are viewless, as our children
	// would not get cut off in the right place.
	if (Owner()) {
		size.w = std::max(size.w, fMin.w);
		size.h = std::max(size.h, fMin.h);
	}

	if (fVisibleItem != nullptr) {
		fVisibleItem->AlignInFrame({area.x0, area.y0, area.x0 + size.w,
			area.y0 + size.h});
	}
}


bool
BCardLayout::ItemAdded(BLayoutItem* item, int32 /*atIndex*/)
{
	if (CountItems() <= 1)
		SetVisibleItem(item);
	else
		item->SetVisible(false);
	return true;
}


void
BCardLayout::ItemRemoved(BLayoutItem* item, int32 /*fromIndex*/)
{
	fMinMaxValid = false;

	if (fVisibleItem == item) {
		BLayoutItem* newVisibleItem = nullptr;
		SetVisibleItem(newVisibleItem);
	}
}


void
BCardLayout::_ValidateMinMax()
{
	if (fMinMaxValid)
		return;

	fMin.w = 0;
	fMin.h = 0;
	fMax.w = kSizeUnlimited;
	fMax.h = kSizeUnlimited;
	fPreferred.w = 0;
	fPreferred.h = 0;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BLayoutItem* item = ItemAt(i);

		gfx::Size min = item->MinSize();
		gfx::Size max = item->MaxSize();
		gfx::Size preferred = item->PreferredSize();

		fMin.w = std::max(fMin.w, min.w);
		fMin.h = std::max(fMin.h, min.h);

		fMax.w = std::min(fMax.w, max.w);
		fMax.h = std::min(fMax.h, max.h);

		fPreferred.w = std::max(fPreferred.w, preferred.w);
		fPreferred.h = std::max(fPreferred.h, preferred.h);
	}

	fMax.w = std::max(fMax.w, fMin.w);
	fMax.h = std::max(fMax.h, fMin.h);

	fPreferred.w = std::max(fPreferred.w, fMin.w);
	fPreferred.h = std::max(fPreferred.h, fMin.h);
	fPreferred.w = std::min(fPreferred.w, fMax.w);
	fPreferred.h = std::min(fPreferred.h, fMax.h);

	fMinMaxValid = true;
	ResetLayoutInvalidation();
}


} // namespace ngui
