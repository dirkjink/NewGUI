/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/InterfaceDefs.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


class BLayout;
class BView;


/*!	Something a layout places (module G10), as Haiku's BLayoutItem: a view
	(through an internal item AddView() makes), a space (BSpaceLayoutItem)
	or a nested layout.

	Sizes are pixel counts (see BLayoutUtils); kSizeUnset marks an unset
	part of an explicit size. Frames are in the coordinates of the layout's
	target view, half-open like every gfx::Rect.

	An item belongs to the layout it was added to (BLayout::AddItem() takes
	it). If the item stands for a view and is the last one of it, removing
	it from the layout takes the view out of the target view as well; the
	item then holds the view, which lives as long as the item does.
*/
class BLayoutItem {
public:
								BLayoutItem();
								BLayoutItem(const BLayoutItem&) = delete;
			BLayoutItem&		operator=(const BLayoutItem&) = delete;
	virtual						~BLayoutItem();
									/*!< Destroys a view it was given on removal. */

			BLayout*			Layout() const;
									/*!< The layout it belongs to, or nullptr. */
			std::unique_ptr<BLayoutItem> RemoveSelf();
									/*!< Takes the item out of its layout and gives it
										back; nullptr if it has none. */

	virtual	gfx::Size			MinSize() = 0;
									/*!< The smallest size it can have. */
	virtual	gfx::Size			MaxSize() = 0;
									/*!< The largest size it can use. */
	virtual	gfx::Size			PreferredSize() = 0;
									/*!< The size it would like to have. */
	virtual	BAlignment			Alignment() = 0;
									/*!< Where it goes in a larger frame than it uses. */

	virtual	void				SetExplicitMinSize(gfx::Size size) = 0;
									/*!< Overrides the set parts of MinSize(). */
	virtual	void				SetExplicitMaxSize(gfx::Size size) = 0;
									/*!< Overrides the set parts of MaxSize(). */
	virtual	void				SetExplicitPreferredSize(gfx::Size size) = 0;
									/*!< Overrides the set parts of PreferredSize(). */
			void				SetExplicitSize(gfx::Size size);
									/*!< Sets all three explicit sizes. */
	virtual	void				SetExplicitAlignment(BAlignment alignment) = 0;
									/*!< Overrides the set parts of Alignment(). */

	virtual	bool				IsVisible() = 0;
									/*!< Whether it is shown; hidden items take no
										space. */
	virtual	void				SetVisible(bool visible) = 0;
									/*!< Shows or hides it. */

	virtual	gfx::Rect			Frame() = 0;
									/*!< Where the layout placed it. */
	virtual	void				SetFrame(gfx::Rect frame) = 0;
									/*!< Places it; called by the layout. */

	virtual	bool				HasHeightForWidth();
									/*!< Whether its height depends on its width (wrapped
										text); false by default. */
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);
									/*!< The heights for \a width; the pointers may be
										nullptr. Nothing by default. */

	virtual	BView*				View();
									/*!< The view it stands for, or nullptr. */

	virtual	void				InvalidateLayout(bool children = false);
									/*!< Its sizes changed: LayoutInvalidated(), then the
										layout it belongs to is invalidated. */
	virtual	void				Relayout(bool immediate = false);
									/*!< Lays out its view again (now if \a immediate). */

			void*				LayoutData() const;
									/*!< Data its layout keeps with it; owned by the
										layout. */
			void				SetLayoutData(void* data);
									/*!< Sets the layout's data. */

			void				AlignInFrame(gfx::Rect frame);
									/*!< SetFrame() with \a frame reduced to MaxSize() and
										placed by Alignment(); height for width
										included. */

protected:
	virtual	void				LayoutInvalidated(bool children);
									/*!< Hook: its sizes changed. */
	virtual	void				AttachedToLayout();
									/*!< Hook: it was added to Layout(). */
	virtual	void				DetachedFromLayout(BLayout* layout);
									/*!< Hook: it was removed from \a layout. */
	virtual	void				AncestorVisibilityChanged(bool shown);
									/*!< Hook: a layout it is in was shown or hidden. */

private:
	friend class BLayout;

			void				SetLayout(BLayout* layout);

			BLayout*			fLayout = nullptr;
			void*				fLayoutData = nullptr;
			std::unique_ptr<BView> fRemovedView;
									// its view, taken out of the target on removal
};


} // namespace ngui
