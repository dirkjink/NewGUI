/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/LayoutItem.h>


namespace ngui {


/*!	Empty space in a layout (Haiku's BSpaceLayoutItem): glue that grows,
	struts of a fixed width or height, or any sizes.

	NewGUI: sizes are pixel counts; the spacing constants of Haiku's
	BControlLook::ComposeSpacing() (B_USE_DEFAULT_SPACING ...) come with the
	group and grid layouts. */
class BSpaceLayoutItem : public BLayoutItem {
public:
								BSpaceLayoutItem(gfx::Size minSize, gfx::Size maxSize,
									gfx::Size preferredSize, BAlignment alignment);
									/*!< A space with these sizes and alignment. */
								~BSpaceLayoutItem() override;
									/*!< Destroys the item. */

	static	std::unique_ptr<BSpaceLayoutItem> CreateGlue();
									/*!< Space from 0 to unlimited in both directions. */
	static	std::unique_ptr<BSpaceLayoutItem> CreateHorizontalStrut(float width);
									/*!< Exactly \a width wide, any height. */
	static	std::unique_ptr<BSpaceLayoutItem> CreateVerticalStrut(float height);
									/*!< Exactly \a height high, any width. */

			gfx::Size			MinSize() override;
									/*!< The minimum given. */
			gfx::Size			MaxSize() override;
									/*!< The maximum given. */
			gfx::Size			PreferredSize() override;
									/*!< The preferred size given. */
			BAlignment			Alignment() override;
									/*!< The alignment given. */

			void				SetExplicitMinSize(gfx::Size size) override;
									/*!< Replaces the set parts of the minimum. */
			void				SetExplicitMaxSize(gfx::Size size) override;
									/*!< Replaces the set parts of the maximum. */
			void				SetExplicitPreferredSize(gfx::Size size) override;
									/*!< Replaces the set parts of the preferred size. */
			void				SetExplicitAlignment(BAlignment alignment) override;
									/*!< Replaces the set parts of the alignment. */

			bool				IsVisible() override;
									/*!< Whether it takes space. */
			void				SetVisible(bool visible) override;
									/*!< Makes it take space or not. */

			gfx::Rect			Frame() override;
									/*!< Where the layout put it. */
			void				SetFrame(gfx::Rect frame) override;
									/*!< Records the frame. */

private:
			gfx::Rect			fFrame = {0, 0, 0, 0};
			gfx::Size			fMinSize;
			gfx::Size			fMaxSize;
			gfx::Size			fPreferredSize;
			BAlignment			fAlignment;
			bool				fVisible = true;
};


} // namespace ngui
