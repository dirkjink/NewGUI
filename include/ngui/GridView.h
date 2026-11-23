/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/GridLayout.h>
#include <ngui/View.h>


namespace ngui {


/*!	A view with a BGridLayout (Haiku's BGridView), in the panel background
	color. Only grid layouts can be set. */
class BGridView : public BView {
public:
								BGridView(float horizontalSpacing = kUseDefaultSpacing,
									float verticalSpacing = kUseDefaultSpacing);
									/*!< A grid without a name. */
								BGridView(std::string_view name,
									float horizontalSpacing = kUseDefaultSpacing,
									float verticalSpacing = kUseDefaultSpacing);
									/*!< A grid called \a name. */
								~BGridView() override;
									/*!< Destroys the view. */

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout) override;
									/*!< Takes only a BGridLayout; nullptr for others. */
			BGridLayout*		GridLayout() const;
									/*!< Its layout. */
};


} // namespace ngui
