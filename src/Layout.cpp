/*
 * Copyright 2010, Haiku Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Layout.cpp (commit b1d497bb69),
 * without archiving. NewGUI: the layout owns its items (unique_ptr); an item
 * removed as the last one of its view takes the view along (see
 * LayoutItem.h); AddView() with a pointer takes views that are children of
 * the target already, AddView() with a unique_ptr adds new ones; a layout
 * with an owner is not added as an item (Haiku shared it between its view
 * and the other layout and needed BView::_LayoutLeft() for that).
 */


#include <ngui/Layout.h>

#include <algorithm>
#include <utility>

#include <ngui/LayoutContext.h>
#include <ngui/View.h>

#include "ViewLayoutItem.h"


namespace ngui {


namespace {
	// flags for our state
	const uint32 B_LAYOUT_INVALID = 0x80000000UL; // needs layout
	const uint32 B_LAYOUT_CACHE_INVALID = 0x40000000UL; // needs recalculation
	const uint32 B_LAYOUT_REQUIRED = 0x20000000UL; // needs layout
	const uint32 B_LAYOUT_IN_PROGRESS = 0x10000000UL;
	const uint32 B_LAYOUT_ALL_CLEAR = 0UL;

	// handy masks to check various states
	const uint32 B_LAYOUT_INVALIDATION_ILLEGAL
		= B_LAYOUT_CACHE_INVALID | B_LAYOUT_IN_PROGRESS;
	const uint32 B_LAYOUT_NECESSARY
		= B_LAYOUT_INVALID | B_LAYOUT_REQUIRED | B_LAYOUT_CACHE_INVALID;
	const uint32 B_RELAYOUT_NOT_OK
		= B_LAYOUT_INVALID | B_LAYOUT_IN_PROGRESS;
}


BLayout::BLayout()
	:
	fState(B_LAYOUT_ALL_CLEAR),
	fAncestorsVisible(true),
	fInvalidationDisabled(0),
	fContext(nullptr),
	fOwner(nullptr),
	fTarget(nullptr)
{
}


BLayout::~BLayout()
{
	// NewGUI: remaining items are destroyed; their views stay children of
	// the target. Subclass hooks are not called any more, as in Haiku.
	fTarget = nullptr;
	for (int32 i = CountItems() - 1; i >= 0; i--)
		(void)RemoveItem(i);
}


BView*
BLayout::Owner() const
{
	return fOwner;
}


BView*
BLayout::TargetView() const
{
	return fTarget;
}


BView*
BLayout::View()
{
	return fOwner;
}


BLayoutItem*
BLayout::AddView(std::unique_ptr<BView> child)
{
	return AddView(-1, std::move(child));
}


BLayoutItem*
BLayout::AddView(int32 index, std::unique_ptr<BView> child)
{
	if (!fTarget || !child)
		return nullptr;

	BView* view = fTarget->_AddChild(std::move(child), nullptr);
	BLayoutItem* item = AddView(index, view);
	if (item == nullptr) {
		// In case of errors, we don't want to leave this view added where it
		// shouldn't be.
		(void)view->RemoveSelf();
	}
	return item;
}


BLayoutItem*
BLayout::AddView(BView* child)
{
	return AddView(-1, child);
}


BLayoutItem*
BLayout::AddView(int32 index, BView* child)
{
	if (child == nullptr)
		return nullptr;
	return AddItem(index, std::make_unique<BViewLayoutItem>(child));
}


BLayoutItem*
BLayout::AddItem(std::unique_ptr<BLayoutItem> item)
{
	return AddItem(-1, std::move(item));
}


BLayoutItem*
BLayout::AddItem(int32 index, std::unique_ptr<BLayoutItem> item)
{
	if (!fTarget || !item || IndexOfItem(item.get()) >= 0)
		return nullptr;
	if (BLayout* layout = dynamic_cast<BLayout*>(item.get());
			layout != nullptr && layout->Owner() != nullptr) {
		return nullptr;
	}

	// if the item refers to a BView, we make sure it is a child of the
	// target view (NewGUI: a view the item took along on an earlier removal
	// is given back to the target; any other must be a child already)
	BView* view = item->View();
	bool viewAdded = false;
	if (view && view->fParent != fTarget) {
		if (item->fRemovedView.get() != view)
			return nullptr;
		fTarget->_AddChild(std::move(item->fRemovedView), nullptr);
		viewAdded = true;
	}

	// validate the index
	if (index < 0 || index > CountItems())
		index = CountItems();

	BLayoutItem* added = item.get();
	fItems.insert(fItems.begin() + index, std::move(item));

	if (!ItemAdded(added, index)) {
		item = std::move(fItems[static_cast<std::size_t>(index)]);
		fItems.erase(fItems.begin() + index);
		if (viewAdded)
			item->fRemovedView = view->RemoveSelf();
		return nullptr;
	}

	added->SetLayout(this);
	if (!fAncestorsVisible)
		added->AncestorVisibilityChanged(fAncestorsVisible);
	InvalidateLayout();

	return added;
}


std::unique_ptr<BView>
BLayout::RemoveView(BView* child)
{
	std::unique_ptr<BView> removedView;
	if (child == nullptr)
		return removedView;

	// a view can have any number of layout items - we need to remove them all
	int32 remaining = (int32)child->fLayoutItems.size();
	for (int32 i = CountItems() - 1; i >= 0 && remaining > 0; i--) {
		if (ItemAt(i)->View() != child)
			continue;

		std::unique_ptr<BLayoutItem> item = RemoveItem(i);
		if (item->fRemovedView)
			removedView = std::move(item->fRemovedView);
		remaining--;
	}

	return removedView;
}


std::unique_ptr<BLayoutItem>
BLayout::RemoveItem(BLayoutItem* item)
{
	int32 index = IndexOfItem(item);
	return index >= 0 ? RemoveItem(index) : nullptr;
}


std::unique_ptr<BLayoutItem>
BLayout::RemoveItem(int32 index)
{
	if (index < 0 || index >= CountItems())
		return nullptr;

	std::unique_ptr<BLayoutItem> item
		= std::move(fItems[static_cast<std::size_t>(index)]);
	fItems.erase(fItems.begin() + index);
	ItemRemoved(item.get(), index);
	item->SetLayout(nullptr);

	// If this is the last item in use that refers to its BView,
	// that BView now needs to be removed. UNLESS fTarget is NULL,
	// in which case we leave the view as is. (See SetTarget() for more info)
	// NewGUI: the item takes the view along.
	BView* view = item->View();
	if (fTarget && view && view->fLayoutItems.empty() && view->fParent == fTarget)
		item->fRemovedView = view->RemoveSelf();

	InvalidateLayout();

	return item;
}


BLayoutItem*
BLayout::ItemAt(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return nullptr;
	return fItems[static_cast<std::size_t>(index)].get();
}


int32
BLayout::CountItems() const
{
	return (int32)fItems.size();
}


int32
BLayout::IndexOfItem(const BLayoutItem* item) const
{
	for (std::size_t i = 0; i < fItems.size(); i++) {
		if (fItems[i].get() == item)
			return (int32)i;
	}
	return -1;
}


int32
BLayout::IndexOfView(BView* child) const
{
	if (child == nullptr)
		return -1;

	// A BView can have many items, so we just do our best and return the
	// index of the first one in this layout.
	for (BLayoutItem* item : child->fLayoutItems) {
		if (item->Layout() == this)
			return IndexOfItem(item);
	}
	return -1;
}


bool
BLayout::AncestorsVisible() const
{
	return fAncestorsVisible;
}


void
BLayout::InvalidateLayout(bool children)
{
	if (fTarget && fTarget->IsLayoutInvalidationDisabled())
		return;
	if (fInvalidationDisabled > 0
		|| (fState & B_LAYOUT_INVALIDATION_ILLEGAL) != 0) {
		return;
	}

	fState |= B_LAYOUT_NECESSARY;
	LayoutInvalidated(children);

	if (children) {
		for (int32 i = CountItems() - 1; i >= 0; i--)
			ItemAt(i)->InvalidateLayout(children);
	}

	if (fOwner)
		fOwner->InvalidateLayout(children);

	if (BLayout* nestedIn = Layout()) {
		nestedIn->InvalidateLayout();
	} else if (fOwner) {
		// If we weren't added as a BLayoutItem, we still have to invalidate
		// whatever layout our owner is in.
		fOwner->_InvalidateParentLayout();
	}
}


void
BLayout::RequireLayout()
{
	fState |= B_LAYOUT_REQUIRED;
}


bool
BLayout::IsValid()
{
	return (fState & B_LAYOUT_INVALID) == 0;
}


void
BLayout::DisableLayoutInvalidation()
{
	fInvalidationDisabled++;
}


void
BLayout::EnableLayoutInvalidation()
{
	if (fInvalidationDisabled > 0)
		fInvalidationDisabled--;
}


void
BLayout::LayoutItems(bool force)
{
	if ((fState & B_LAYOUT_NECESSARY) == 0 && !force)
		return;

	if (Layout() && (Layout()->fState & B_LAYOUT_IN_PROGRESS) != 0)
		return; // wait for parent layout to lay us out.

	if (fTarget && fTarget->LayoutContext())
		return;

	BLayoutContext context;
	_LayoutWithinContext(force, &context);
}


void
BLayout::Relayout(bool immediate)
{
	if ((fState & B_RELAYOUT_NOT_OK) == 0 || immediate) {
		fState |= B_LAYOUT_REQUIRED;
		LayoutItems(false);
	}
}


void
BLayout::_LayoutWithinContext(bool force, BLayoutContext* context)
{
	if ((fState & B_LAYOUT_NECESSARY) == 0 && !force)
		return;

	BLayoutContext* oldContext = fContext;
	fContext = context;

	if (fOwner && fOwner->_WillLayout()) {
		// in this case, let our owner decide whether or not to have us
		// do our layout, if they do, we won't end up here again.
		fOwner->_Layout(force, context);
	} else {
		fState |= B_LAYOUT_IN_PROGRESS;
		DoLayout();
		// we must ensure that all items are laid out, layouts with a view will
		// have their layout process triggered by their view, but nested
		// view-less layouts must have their layout triggered here (if it hasn't
		// already been triggered).
		// NewGUI: a copy, since a nested layout may change the list.
		const std::vector<BLayout*> nestedLayouts = fNestedLayouts;
		for (BLayout* layout : nestedLayouts) {
			if ((layout->fState & B_LAYOUT_NECESSARY) != 0)
				layout->_LayoutWithinContext(force, context);
		}
		fState = B_LAYOUT_ALL_CLEAR;
	}

	fContext = oldContext;
}


gfx::Rect
BLayout::LayoutArea()
{
	gfx::Rect area = Frame();
	if (fOwner)
		area = {0, 0, area.Width(), area.Height()};
	return area;
}


bool
BLayout::ItemAdded(BLayoutItem* /*item*/, int32 /*atIndex*/)
{
	return true;
}


void
BLayout::ItemRemoved(BLayoutItem* /*item*/, int32 /*fromIndex*/)
{
}


void
BLayout::LayoutInvalidated(bool /*children*/)
{
}


void
BLayout::OwnerChanged(BView* /*was*/)
{
}


void
BLayout::AttachedToLayout()
{
	if (!fOwner) {
		Layout()->fNestedLayouts.push_back(this);
		SetTarget(Layout()->TargetView());
	}
}


void
BLayout::DetachedFromLayout(BLayout* from)
{
	if (!fOwner) {
		std::erase(from->fNestedLayouts, this);
		SetTarget(nullptr);
	}
}


void
BLayout::AncestorVisibilityChanged(bool shown)
{
	if (fAncestorsVisible == shown)
		return;

	fAncestorsVisible = shown;
	VisibilityChanged(shown);
}


void
BLayout::VisibilityChanged(bool show)
{
	if (fOwner)
		return;

	for (int32 i = CountItems() - 1; i >= 0; i--)
		ItemAt(i)->AncestorVisibilityChanged(show);
}


void
BLayout::ResetLayoutInvalidation()
{
	fState &= ~B_LAYOUT_CACHE_INVALID;
}


BLayoutContext*
BLayout::LayoutContext() const
{
	return fContext;
}


void
BLayout::SetOwner(BView* owner)
{
	if (fOwner == owner)
		return;

	SetTarget(owner);
	std::swap(fOwner, owner);

	OwnerChanged(owner);
		// call hook
}


void
BLayout::SetTarget(BView* target)
{
	if (fTarget != target) {
		/* With fTarget NULL, RemoveItem() will not remove the views from their
		 * parent. This ensures that the views are not lost to the void.
		 */
		fTarget = nullptr;

		// remove and delete all items
		for (int32 i = CountItems() - 1; i >= 0; i--)
			(void)RemoveItem(i);

		fTarget = target;

		InvalidateLayout();
	}
}


} // namespace ngui
