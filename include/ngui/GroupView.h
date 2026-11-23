/*
 * Copyright 2006-2010, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/GroupLayout.h>
#include <ngui/View.h>


namespace ngui {


/*!	A view with a BGroupLayout (Haiku's BGroupView), in the panel
	background color. Only group layouts can be set. */
class BGroupView : public BView {
public:
								BGroupView(BOrientation orientation = BOrientation::Horizontal,
									float spacing = kUseDefaultSpacing);
									/*!< A group without a name. */
								BGroupView(std::string_view name,
									BOrientation orientation = BOrientation::Horizontal,
									float spacing = kUseDefaultSpacing);
									/*!< A group called \a name. */
								~BGroupView() override;
									/*!< Destroys the view. */

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout) override;
									/*!< Takes only a BGroupLayout; nullptr for others. */
			BGroupLayout*		GroupLayout() const;
									/*!< Its layout. */
};


} // namespace ngui
