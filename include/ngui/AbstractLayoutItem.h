/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/LayoutItem.h>
#include <ngui/LayoutUtils.h>


namespace ngui {


/*!	A layout item that keeps explicit sizes and an explicit alignment and
	composes them with base values (Haiku's BAbstractLayoutItem):
	MinSize() is the explicit minimum where set, BaseMinSize() elsewhere. */
class BAbstractLayoutItem : public BLayoutItem {
public:
								BAbstractLayoutItem();
								~BAbstractLayoutItem() override;
									/*!< Destroys the item. */

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
									/*!< Centered by default. */

private:
			gfx::Size			fMinSize = kUnsetSize;
			gfx::Size			fMaxSize = kUnsetSize;
			gfx::Size			fPreferredSize = kUnsetSize;
			BAlignment			fAlignment;
};


} // namespace ngui
