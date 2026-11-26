/*
 * Copyright 2001-2022 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string>
#include <string_view>

#include <ngui/InterfaceDefs.h>
#include <ngui/View.h>


namespace ngui {


/*!	A frame around a group of views (Haiku's BBox), with a label string or a
	label view (for instance a check box) in its top border. With one child
	and no layout of its own, it sizes itself to the child plus its border
	and label and places the child inside; a box one pixel high or wide is a
	separator line.

	Differences to Haiku: children and the label view are owned
	(std::unique_ptr); the label view is never an item of the box's layout
	(Haiku removed it from the layout during every layout pass and put it
	back at the end, so it still counted for the layout's size). Sizes are
	pixel counts, one more than Haiku's.
*/
class BBox : public BView {
public:
								BBox(gfx::Rect frame, std::string_view name = {},
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents
										| BViewFlag::NavigableJump,
									BBorderStyle border = BBorderStyle::Fancy);
									/*!< A box at \a frame. */
								BBox(std::string_view name,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::FrameEvents
										| BViewFlag::NavigableJump,
									BBorderStyle border = BBorderStyle::Fancy,
									std::unique_ptr<BView> child = nullptr);
									/*!< A box for a layout around \a child. */
	explicit					BBox(BBorderStyle border,
									std::unique_ptr<BView> child = nullptr);
									/*!< An unnamed box for a layout around \a child. */
								~BBox() override;
									/*!< Destroys the box, its label view and children. */

	virtual	void				SetBorder(BBorderStyle border);
									/*!< Plain, fancy or no border. */
			BBorderStyle		Border() const;
									/*!< The border style. */

			float				TopBorderOffset();
									/*!< How far the top border is below the top: half the
										height of the label. */
			gfx::Rect			InnerFrame();
									/*!< The bounds without the border and the label. */

			void				SetLabel(std::string_view label);
									/*!< A label string (bold); empty for none. Removes a
										label view. */
			void				SetLabel(std::unique_ptr<BView> labelView);
									/*!< A label view in the top border, at its preferred
										size; nullptr for none. Removes a label string. */
			const std::string&	Label() const;
									/*!< The label string, empty if there is none. */
			BView*				LabelView() const;
									/*!< The label view, or nullptr. */

			void				Draw(DrawContext& context) override;
									/*!< The border, interrupted by the label. */
			void				AttachedToWindow() override;
									/*!< Takes its parent's view color. */
			void				FrameResized(float width, float height) override;
									/*!< Draws the border anew. */

			void				ResizeToPreferred() override;
									/*!< Grows to its preferred size, never shrinks. */
			void				GetPreferredSize(float* width, float* height) override;
									/*!< The preferred size. */

			gfx::Size			MinSize() override;
									/*!< Its layout's, or the child's plus border and label;
										at least as wide as the label. */
			gfx::Size			MaxSize() override;
									/*!< Its layout's, or the child's plus border and label. */
			gfx::Size			PreferredSize() override;
									/*!< Its layout's, or the child's plus border and label. */
			BAlignment			LayoutAlignment() override;
									/*!< Centered, or full width and height if the child
										wants them. */

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout) override;
									/*!< Sets a layout for its children; the label view stays
										out of it. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Group, its label as name. */

protected:
			void				LayoutInvalidated(bool descendants) override;
									/*!< Drops the cached sizes. */
			void				DoLayout() override;
									/*!< Places the label view and lays out the child or the
										layout. */

private:
			struct LayoutData {
				gfx::Rect		labelBox;	// label string or label view; for a
											// string without its descent
				float			insetLeft = 0;	// insets of border and label
				float			insetTop = 0;
				float			insetRight = 0;
				float			insetBottom = 0;
				gfx::Size		min;
				gfx::Size		max;
				gfx::Size		preferred;
				BAlignment		alignment;
				bool			valid = false;
			};

			void				_InitObject();
			void				_DrawPlain(DrawContext& context, gfx::Rect labelBox);
			void				_DrawFancy(DrawContext& context, gfx::Rect labelBox);
			void				_ClearLabel();

			BView*				_Child() const;
			void				_ValidateLayoutData();

			std::string			fLabel;
			BView*				fLabelView = nullptr;
			BBorderStyle		fStyle;
			LayoutData			fLayoutData;
};


} // namespace ngui
