/*
 * Copyright 2001-2014 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/MenuItem.h>


namespace ngui {


/*!	A line between groups of menu items (Haiku's BSeparatorItem); it can
	never be enabled or chosen. */
class BSeparatorItem : public BMenuItem {
public:
								BSeparatorItem();
									/*!< A separator. */
								~BSeparatorItem() override;
									/*!< Destroys it. */

			void				SetEnabled(bool enable) override;
									/*!< Does nothing: a separator stays disabled. */

protected:
			void				GetContentSize(float* width, float* height) override;
									/*!< 2 wide; in a column 0.8 times the font size high
										(even, at least 4); in a row 2. */
			void				Draw(DrawContext& context) override;
									/*!< An etched line across the middle. */
};


} // namespace ngui
