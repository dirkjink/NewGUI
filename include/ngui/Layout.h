/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <vector>

#include <ngui/LayoutItem.h>
#include <ngui/SupportDefs.h>


namespace ngui {


class BLayoutContext;


/*!	Places layout items in the bounds of a view (module G10), as Haiku's
	BLayout. Subclasses compute the sizes in BLayoutItem's methods and place
	the items in DoLayout().

	The view a layout belongs to is its owner (BView::SetLayout()); a layout
	without owner can be an item of another layout (nested). Either way the
	views of its items are children of its target view: the owner, or the
	target of the layout it is nested in. A nested layout must be added to
	its layout before items are added to it, as in Haiku.

	Ownership (NewGUI): the layout owns its items; AddView() makes an item
	for a view, which becomes a child of the target view if it is not one
	already. A layout that has an owner belongs to that view and cannot be
	an item of another layout; Haiku shared it between the two.

	Layout happens on the thread of the window of the target view.
*/
class BLayout : public BLayoutItem {
public:
								BLayout();
								~BLayout() override;
									/*!< Destroys its items; views stay children of the
										target. */

			BView*				Owner() const;
									/*!< The view whose layout this is, or nullptr. */
			BView*				TargetView() const;
									/*!< The view the items' views are children of. */
			BView*				View() override;
									/*!< The owner. */

	// methods dealing with items

			BLayoutItem*		AddView(std::unique_ptr<BView> child);
									/*!< Makes \a child a child of the target view and adds
										an item for it; returns the item, nullptr if there
										is no target. */
			BLayoutItem*		AddView(int32 index, std::unique_ptr<BView> child);
									/*!< The same at \a index (at the end if out of
										range). */
			BLayoutItem*		AddView(BView* child);
									/*!< Adds an item for \a child, which must be a child of
										the target view already; nullptr otherwise. */
			BLayoutItem*		AddView(int32 index, BView* child);
									/*!< The same at \a index. */
			BLayoutItem*		AddItem(std::unique_ptr<BLayoutItem> item);
									/*!< Adds \a item at the end; returns it, nullptr if
										there is no target, \a item is nullptr, a layout
										with an owner, or refused by ItemAdded(). */
	virtual	BLayoutItem*		AddItem(int32 index, std::unique_ptr<BLayoutItem> item);
									/*!< The same at \a index (at the end if out of
										range). */

			std::unique_ptr<BView> RemoveView(BView* child);
									/*!< Removes all items of \a child and takes it out of
										the target view; nullptr if it has no item here. */
			std::unique_ptr<BLayoutItem> RemoveItem(BLayoutItem* item);
									/*!< Removes \a item and gives it back, with its view
										if it was the view's last item; nullptr if it is
										not here. */
	virtual	std::unique_ptr<BLayoutItem> RemoveItem(int32 index);
									/*!< The same for the item at \a index. */

			BLayoutItem*		ItemAt(int32 index) const;
									/*!< The item at \a index, or nullptr. */
			int32				CountItems() const;
									/*!< The number of items. */
			int32				IndexOfItem(const BLayoutItem* item) const;
									/*!< The index of \a item, or -1. */
			int32				IndexOfView(BView* child) const;
									/*!< The index of the first item of \a child, or -1. */

			bool				AncestorsVisible() const;
									/*!< Whether the layouts it is nested in are shown. */

	// Layouting related methods

			void				InvalidateLayout(bool children = false) override;
									/*!< Its sizes must be computed again; tells the owner
										and the layout it is nested in. */
			void				Relayout(bool immediate = false) override;
									/*!< Lays the items out again. */

			void				RequireLayout();
									/*!< Marks that the items must be laid out again. */
			bool				IsValid();
									/*!< Whether no layout is pending. */
			void				EnableLayoutInvalidation();
									/*!< Undoes one DisableLayoutInvalidation(). */
			void				DisableLayoutInvalidation();
									/*!< Ignores InvalidateLayout() until enabled again. */

			void				LayoutItems(bool force = false);
									/*!< Lays out the items if needed (always if \a force). */
			gfx::Rect			LayoutArea();
									/*!< The area to place the items in: the owner's
										bounds, or the frame of a nested layout. */
			BLayoutContext*		LayoutContext() const;
									/*!< The layout pass running, or nullptr. */

protected:
	static	std::unique_ptr<BLayoutItem> ItemForView(std::unique_ptr<BView> view);
									/*!< The item AddView() makes for a new view: added
										with AddItem(), the view becomes a child of the
										target; destroyed unadded, it destroys the view. */
	static	std::unique_ptr<BLayoutItem> ItemForView(BView* view);
									/*!< The item AddView() makes for a child of the
										target. */

	// BLayout hook methods
	virtual	bool				ItemAdded(BLayoutItem* item, int32 atIndex);
									/*!< \a item was added at \a atIndex; false refuses it. */
	virtual	void				ItemRemoved(BLayoutItem* item, int32 fromIndex);
									/*!< \a item was removed from \a fromIndex. */
			void				LayoutInvalidated(bool children) override;
									/*!< The sizes must be computed again. */
	virtual	void				DoLayout() = 0;
									/*!< Places the items in LayoutArea(). */
	virtual	void				OwnerChanged(BView* was);
									/*!< The owner changed from \a was to Owner(). */

	// BLayoutItem hook methods
			void				AttachedToLayout() override;
									/*!< Nested: takes over the target of its layout. */
			void				DetachedFromLayout(BLayout* layout) override;
									/*!< Nested: drops the target and its items. */
			void				AncestorVisibilityChanged(bool shown) override;
									/*!< Passes the visibility on. */

	// To be called by sub-classes in SetVisible().
			void				VisibilityChanged(bool show);
									/*!< Tells the items of a nested layout. */
	// To be called when layout data is known to be good
			void				ResetLayoutInvalidation();
									/*!< The sizes are computed. */

private:
	friend class BView;

			void				SetOwner(BView* owner);
			void				SetTarget(BView* target);

			void				_LayoutWithinContext(bool force,
									BLayoutContext* context);

			uint32				fState;
			bool				fAncestorsVisible;
			int32				fInvalidationDisabled;
			BLayoutContext*		fContext;
			BView*				fOwner;
			BView*				fTarget;
			std::vector<std::unique_ptr<BLayoutItem>> fItems;
			std::vector<BLayout*> fNestedLayouts;
};


} // namespace ngui
