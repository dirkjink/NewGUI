/*
 * Copyright 2006-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/InterfaceDefs.h>
#include <ngui/LayoutUtils.h>
#include <ngui/SupportDefs.h>
#include <ngui/View.h>


namespace ngui {


class BSplitLayout;


/*!	Views or layout items in a row or a column with a splitter between each
	two (Haiku's BSplitView). Dragging a splitter moves space from one
	neighbour to the other; a collapsible item that would become smaller
	than half its minimum collapses. The weights of the items follow their
	sizes after each drag.

	Differences to Haiku: children are owned (AddChild() takes them as
	std::unique_ptr); the mouse cursor does not change over a splitter yet
	(no cursors); insets and spacing may be spacing constants, resolved
	when they are used. Sizes are pixel counts.
*/
class BSplitView : public BView {
public:
								BSplitView(BOrientation orientation = BOrientation::Horizontal,
									float spacing = kUseDefaultSpacing);
									/*!< A row (Horizontal) or a column (Vertical) for a
										layout. */
								~BSplitView() override;
									/*!< Destroys it with its children. */

			void				SetInsets(float left, float top, float right,
									float bottom);
									/*!< The insets (pixels or spacing constants). */
			void				SetInsets(float horizontal, float vertical);
									/*!< The insets (pixels or spacing constants). */
			void				SetInsets(float insets);
									/*!< The insets (pixels or spacing constants). */
			void				GetInsets(float* left, float* top, float* right,
									float* bottom) const;
									/*!< The insets, constants resolved; each pointer may be
										nullptr. */

			float				Spacing() const;
									/*!< The space on both sides of a splitter. */
			void				SetSpacing(float spacing);
									/*!< Sets the spacing (pixels or a spacing constant). */

			BOrientation		Orientation() const;
									/*!< Row or column. */
			void				SetOrientation(BOrientation orientation);
									/*!< Makes it a row or a column. */

			float				SplitterSize() const;
									/*!< The thickness of a splitter (6). */
			void				SetSplitterSize(float size);
									/*!< Sets the thickness of the splitters. */

			int32				CountItems() const;
									/*!< The number of items (not counting splitters). */
			float				ItemWeight(int32 index) const;
									/*!< The weight of item \a index; 0 if there is none. */
			float				ItemWeight(BLayoutItem* item) const;
									/*!< The weight of \a item. */
			void				SetItemWeight(int32 index, float weight,
									bool invalidateLayout);
									/*!< Sets the weight of item \a index. */
			void				SetItemWeight(BLayoutItem* item, float weight);
									/*!< Sets the weight of \a item. */

			bool				IsCollapsible(int32 index) const;
									/*!< Whether item \a index may collapse. */
			void				SetCollapsible(bool collapsible);
									/*!< Whether all items may collapse (true by default). */
			void				SetCollapsible(int32 index, bool collapsible);
									/*!< Whether item \a index may collapse. */
			void				SetCollapsible(int32 first, int32 last,
									bool collapsible);
									/*!< Whether items \a first to \a last may collapse. */

			bool				IsItemCollapsed(int32 index) const;
									/*!< Whether item \a index is collapsed. */
			void				SetItemCollapsed(int32 index, bool collapsed);
									/*!< Collapses or shows item \a index. */

			using				BView::AddChild;
			BLayoutItem*		AddChild(std::unique_ptr<BView> child, float weight);
									/*!< Adds \a child at the end with \a weight. */
			BLayoutItem*		AddChild(int32 index, std::unique_ptr<BView> child,
									float weight);
									/*!< Adds \a child at \a index with \a weight. */
			BLayoutItem*		AddChild(std::unique_ptr<BLayoutItem> child, float weight);
									/*!< Adds \a child at the end with \a weight. */
			BLayoutItem*		AddChild(int32 index, std::unique_ptr<BLayoutItem> child,
									float weight);
									/*!< Adds \a child at \a index with \a weight. */

			void				AttachedToWindow() override;
									/*!< Takes its parent's view color. */
			void				Draw(DrawContext& context) override;
									/*!< The splitters, the dragged one pressed. */
			void				MouseDown(gfx::Point where) override;
									/*!< Starts dragging the splitter under the mouse. */
			void				MouseUp(gfx::Point where) override;
									/*!< Ends dragging and lays the items out anew. */
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Drags the splitter. */

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout) override;
									/*!< Not allowed: returns nullptr. */

protected:
	virtual	void				DrawSplitter(DrawContext& context, gfx::Rect frame,
									BOrientation orientation, bool pressed);
									/*!< Hook: draws one splitter in \a frame. */

private:
			BSplitLayout*		fSplitLayout;
};


} // namespace ngui
