/*
 * Copyright 2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/Layout.h>


namespace ngui {


/*!	A layout with explicit sizes, an alignment, a frame and visibility
	(Haiku's BAbstractLayout), the base of the group and grid layouts. With
	an owner these are the owner view's explicit sizes, frame and
	visibility; without one the layout keeps its own. */
class BAbstractLayout : public BLayout {
public:
								BAbstractLayout();
								~BAbstractLayout() override;
									/*!< Destroys the layout. */

			gfx::Size			MinSize() override;
									/*!< Explicit minimum over BaseMinSize(). */
			gfx::Size			MaxSize() override;
									/*!< Explicit maximum over BaseMaxSize(). */
			gfx::Size			PreferredSize() override;
									/*!< Explicit preferred size over
										BasePreferredSize(). */
			BAlignment			Alignment() override;
									/*!< Explicit alignment over BaseAlignment(). */

			void				SetExplicitMinSize(gfx::Size size) override;
									/*!< Sets the explicit minimum. */
			void				SetExplicitMaxSize(gfx::Size size) override;
									/*!< Sets the explicit maximum. */
			void				SetExplicitPreferredSize(gfx::Size size) override;
									/*!< Sets the explicit preferred size. */
			void				SetExplicitAlignment(BAlignment alignment) override;
									/*!< Sets the explicit alignment. */

	virtual	gfx::Size			BaseMinSize();
									/*!< 0 × 0 by default. */
	virtual	gfx::Size			BaseMaxSize();
									/*!< Unlimited by default. */
	virtual	gfx::Size			BasePreferredSize();
									/*!< 0 × 0 by default. */
	virtual	BAlignment			BaseAlignment();
									/*!< Full width and height by default. */

			gfx::Rect			Frame() override;
									/*!< The owner's frame, or its own. */
			void				SetFrame(gfx::Rect frame) override;
									/*!< Moves the owner, or sets its own frame and lays
										out again. */

			bool				IsVisible() override;
									/*!< Whether the owner, or the layout, is shown. */
			void				SetVisible(bool visible) override;
									/*!< Shows or hides the owner, or the layout. */

protected:
			void				OwnerChanged(BView* was) override;
									/*!< Switches between the owner's and its own data. */
			void				AncestorVisibilityChanged(bool shown) override;
									/*!< Shows or hides the owner with its ancestors. */

private:
			struct Proxy;
			struct DataProxy;
			struct ViewProxy;

			std::unique_ptr<Proxy> fExplicitData;
};


} // namespace ngui
