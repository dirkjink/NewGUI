/*
 * Copyright 2004-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <memory>
#include <string_view>

#include <ngui/ControlLook.h>
#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/View.h>


namespace ngui {


class BScrollBar;


/*!	A target view with a border and a horizontal and/or vertical scroll bar
	that scroll it (Haiku's BScrollView). At a frame, it is as large as the
	target's frame plus the bars and the border; for a layout, it lays out
	the target in the room left by them. A target that supports the layout
	and is not ScrollViewAware gets ranges, steps and proportions of its
	bars from its preferred size whenever the scroll view is resized.

	Differences to Haiku: it owns its target (SetTarget() gives the old one
	back); there is no resize knob of a document window to leave room for;
	SetTarget() makes the target as large as the room for it (Haiku: one
	pixel smaller, until the next layout). Sizes are pixel counts, one more
	than Haiku's.
*/
class BScrollView : public BView {
public:
								BScrollView(std::string_view name,
									std::unique_ptr<BView> target,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = {}, bool horizontal = false,
									bool vertical = false,
									BBorderStyle border = BBorderStyle::Fancy);
									/*!< A scroll view around \a target (may be nullptr),
										its frame computed from the target's. */
								BScrollView(std::string_view name,
									std::unique_ptr<BView> target, BViewFlags flags,
									bool horizontal, bool vertical,
									BBorderStyle border = BBorderStyle::Fancy);
									/*!< A scroll view for a layout. */
								~BScrollView() override;
									/*!< Destroys it with its target and scroll bars. */

			void				Draw(DrawContext& context) override;
									/*!< The frame around the target and the bars. */
			void				FrameResized(float width, float height) override;
									/*!< Sets up the bars for a target that supports the
										layout. */
			void				WindowActivated(bool active) override;
									/*!< Draws a highlighted border anew. */

			void				GetPreferredSize(float* width, float* height) override;
									/*!< PreferredSize(). */
			void				ResizeToPreferred() override;
									/*!< Only in a window, as in Haiku. */

			gfx::Size			MinSize() override;
									/*!< The target's (or 17 × 17) plus bars and border. */
			gfx::Size			MaxSize() override;
									/*!< The target's (or unlimited) plus bars and border. */
			gfx::Size			PreferredSize() override;
									/*!< The target's (or 33 × 33) plus bars and border. */

			BScrollBar*			ScrollBar(BOrientation direction) const;
									/*!< Its horizontal or vertical scroll bar, or nullptr. */

	virtual	void				SetBorder(BBorderStyle border);
									/*!< Fancy, plain or no border. */
			BBorderStyle		Border() const;
									/*!< The border style. */

			void				SetBorders(BorderFlags borders);
									/*!< Which sides have a border (only for a layout). */
			BorderFlags			Borders() const;
									/*!< Which sides have a border. */

	virtual	std::expected<void, Error> SetBorderHighlighted(bool highlight);
									/*!< Draws the border as focused; NotSupported unless it
										is fancy. */
			bool				IsBorderHighlighted() const;
									/*!< Whether the border is highlighted. */

			std::unique_ptr<BView> SetTarget(std::unique_ptr<BView> target);
									/*!< A new target (may be nullptr) in the room for it;
										gives the old one back. */
			BView*				Target() const;
									/*!< The target, or nullptr. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role ScrollView. */

protected:
			void				LayoutInvalidated(bool descendants) override;
									/*!< Nothing to drop. */
			void				DoLayout() override;
									/*!< The target in the room left by the bars, the bars
										along its sides. */

private:
			void				_Init(std::unique_ptr<BView> target, bool horizontal,
									bool vertical);

			BView*				fTarget = nullptr;
			BScrollBar*			fHorizontalScrollBar = nullptr;
			BScrollBar*			fVerticalScrollBar = nullptr;
			BBorderStyle		fBorder;
			BorderFlags			fBorders = kAllBorders;
			bool				fHighlighted = false;
};


} // namespace ngui
