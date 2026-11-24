/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>

#include <ngui/SupportDefs.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/Rect.h>


namespace ngui {


class BView;
class TextSystem;
struct DrawContext;


/*!	An item of a BListView (Haiku's BListItem): its height and width,
	whether it is selected and enabled, and for BOutlineListView its level
	and whether it is expanded. Subclasses draw it (DrawItem()) and measure
	it (Update()).

	Differences to Haiku: DrawItem() draws into a DrawContext and gets the
	text color the list chose (Haiku: the owner's high color); Update()
	gets the text system (Haiku: the owner's font, here the plain one). No
	archiving.
*/
class BListItem {
public:
	explicit					BListItem(uint32 outlineLevel = 0, bool expanded = true);
									/*!< An item at \a outlineLevel. */
	virtual						~BListItem();
									/*!< Destroys the item. */
								BListItem(const BListItem&) = delete;
			BListItem&			operator=(const BListItem&) = delete;

			float				Height() const;
									/*!< Its height in pixels. */
			float				Width() const;
									/*!< Its width. */
			bool				IsSelected() const;
									/*!< Whether it is selected. */
			void				Select();
									/*!< Marks it selected (the list does it). */
			void				Deselect();
									/*!< Marks it not selected. */

	virtual	void				SetEnabled(bool enabled);
									/*!< A disabled item cannot be selected. */
			bool				IsEnabled() const;
									/*!< Whether it is enabled. */

			void				SetHeight(float height);
									/*!< Its height in pixels. */
			void				SetWidth(float width);
									/*!< Its width. */

	virtual	void				DrawItem(BView* owner, DrawContext& context, gfx::Rect frame,
									const gfx::Color& textColor, bool complete = false) = 0;
									/*!< Draws it into \a frame of \a owner; \a complete
										asks for the background too. */
	virtual	void				Update(BView* owner, const TextSystem& text);
									/*!< Measures it for \a owner: the owner's width and
										the height of a line of the plain font. */

			bool				IsExpanded() const;
									/*!< Whether its subitems show (BOutlineListView). */
			void				SetExpanded(bool expanded);
									/*!< Whether its subitems show. */
			uint32				OutlineLevel() const;
									/*!< Its level in an outline. */
			void				SetOutlineLevel(uint32 level);
									/*!< Its level in an outline. */

private:
	friend class BListView;
	friend class BOutlineListView;

			bool				HasSubitems() const;
			bool				IsItemVisible() const;
			void				SetItemVisible(bool visible);
			float				Top() const
									{ return fTop; }
			float				Bottom() const
									{ return fTop + std::ceil(fHeight) - 1.0f; }
									// Haiku's inclusive bottom
			void				SetTop(float top);

			float				fTop = 0;
			float				fWidth = 0;
			float				fHeight = 0;
			uint32				fLevel;
			bool				fSelected = false;
			bool				fEnabled = true;
			bool				fExpanded;
			bool				fHasSubitems = false;
			bool				fVisible = true;
};


} // namespace ngui
