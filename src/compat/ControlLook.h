/*
 * Copyright 2003-2023 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/os/interface/ControlLook.h and
 * src/kits/interface/ControlLook.cpp (commit b1d497bb69). Internal to the
 * compatibility layer (specification 17): the constants of BControlLook and
 * ShouldDraw(), as base of the 1:1 ports. The virtual interface is the one of
 * ngui::ControlLook; a port's adapter converts at it.
 */
#pragma once


#include <ngui/SupportDefs.h>

#include "CompatView.h"
#include "Rect.h"


namespace ngui::compat {


// The ported looks draw into a BView; here that is the CompatView.
using BView = CompatView;


class BControlLook {
public:
	enum frame_type {
		B_BUTTON_FRAME,
		B_GROUP_FRAME,
		B_MENU_FIELD_FRAME,
		B_SCROLL_VIEW_FRAME,
		B_TEXT_CONTROL_FRAME,
	};

	enum background_type {
		B_BUTTON_BACKGROUND,
		B_BUTTON_WITH_POP_UP_BACKGROUND,
		B_MENU_BACKGROUND,
		B_MENU_BAR_BACKGROUND,
		B_MENU_FIELD_BACKGROUND,
		B_MENU_ITEM_BACKGROUND,
		B_HORIZONTAL_SCROLL_BAR_BACKGROUND,
		B_VERTICAL_SCROLL_BAR_BACKGROUND,
	};

	enum {
		B_LEFT_BORDER		= 1 << 0,
		B_RIGHT_BORDER		= 1 << 1,
		B_TOP_BORDER		= 1 << 2,
		B_BOTTOM_BORDER		= 1 << 3,

		B_ALL_BORDERS		= B_LEFT_BORDER | B_RIGHT_BORDER
								| B_TOP_BORDER | B_BOTTOM_BORDER
	};

	enum {
		B_LEFT_ARROW		= 0,
		B_RIGHT_ARROW		= 1,
		B_UP_ARROW			= 2,
		B_DOWN_ARROW		= 3,
		B_LEFT_UP_ARROW		= 4,
		B_RIGHT_UP_ARROW	= 5,
		B_RIGHT_DOWN_ARROW	= 6,
		B_LEFT_DOWN_ARROW	= 7
	};

	enum {
		B_FOCUSED				= 1 << 0,
		B_CLICKED				= 1 << 1, // some controls activate on mouse up
		B_ACTIVATED				= 1 << 2,
		B_HOVER					= 1 << 3,
		B_DISABLED				= 1 << 4,
		B_DEFAULT_BUTTON		= 1 << 5,
		B_IGNORE_OUTLINE		= 1 << 6,
		B_PARTIALLY_ACTIVATED	= 1 << 7, // like B_ACTIVATED, but for tri-state
		B_FLAT					= 1 << 8, // flat look (e.g. button background)
		B_INVALID				= 1 << 9, // invalid value, use B_FAILURE_COLOR
		B_IS_CONTROL			= 1 << 10, // use control colors

		B_BLEND_FRAME			= 1 << 16,
	};

	enum {
		B_KNOB_NONE = 0,
		B_KNOB_DOTS,
		B_KNOB_LINES
	};

protected:
	// Haiku transforms the corners to view coordinates first; the
	// CompatView has no transformation of its own, the Canvas does that.
	static	bool				ShouldDraw(BView* view, const BRect& rect,
									const BRect& updateRect)
								{
									(void)view;
									return rect.IsValid() && rect.Intersects(updateRect);
								}
};


} // namespace ngui::compat
