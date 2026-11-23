/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ViewLayoutItem.h (commit
 * b1d497bb69). Internal: the item BLayout::AddView() makes for a view.
 */
#pragma once


#include <ngui/LayoutItem.h>


namespace ngui {


class BViewLayoutItem : public BLayoutItem {
public:
								BViewLayoutItem(BView* view);
								~BViewLayoutItem() override;

			gfx::Size			MinSize() override;
			gfx::Size			MaxSize() override;
			gfx::Size			PreferredSize() override;
			BAlignment			Alignment() override;

			void				SetExplicitMinSize(gfx::Size size) override;
			void				SetExplicitMaxSize(gfx::Size size) override;
			void				SetExplicitPreferredSize(gfx::Size size) override;
			void				SetExplicitAlignment(BAlignment alignment) override;

			bool				IsVisible() override;
			void				SetVisible(bool visible) override;

			gfx::Rect			Frame() override;
			void				SetFrame(gfx::Rect frame) override;

			bool				HasHeightForWidth() override;
			void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred) override;

			BView*				View() override;

			void				Relayout(bool immediate = false) override;

protected:
			void				LayoutInvalidated(bool children) override;
			void				AncestorVisibilityChanged(bool shown) override;

private:
			BView*				fView;
			bool				fAncestorsVisible;
};


} // namespace ngui
