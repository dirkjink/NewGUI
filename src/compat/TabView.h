/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: headers/os/interface/TabView.h (commit b1d497bb69),
 * only the tab sides. Internal to the compatibility layer (specification 17).
 */
#pragma once


namespace ngui::compat {


class BTabView {
public:
			enum tab_side {
				kLeftSide	= 1 << 0,
				kRightSide	= 1 << 1,
				kTopSide	= 1 << 2,
				kBottomSide	= 1 << 3
			};
};


} // namespace ngui::compat
