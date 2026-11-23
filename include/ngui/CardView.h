/*
 * Copyright 2006-2009, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>
#include <string_view>

#include <ngui/CardLayout.h>
#include <ngui/View.h>


namespace ngui {


/*!	A view with a BCardLayout (Haiku's BCardView), in the panel background
	color. Only card layouts can be set. */
class BCardView : public BView {
public:
								BCardView(std::string_view name = "");
									/*!< A card view called \a name. */
								~BCardView() override;
									/*!< Destroys the view. */

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout) override;
									/*!< Takes only a BCardLayout; nullptr for others. */
			BCardLayout*		CardLayout() const;
									/*!< Its layout. */
};


} // namespace ngui
