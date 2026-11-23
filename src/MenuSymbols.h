/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The modifier key symbols menu items draw beside their shortcuts (Haiku's
// MenuPrivate::MenuItemShift() ...). Internal.


#include <cstdint>

#include <ngui/gfx/GlyphRun.h>


namespace ngui {


enum class MenuSymbol : std::uint8_t {
	Shift,
	Control,
	Option,
	Alt,
	Menu
};


// The image of \a symbol, registered on first use.
gfx::ImageHandle				MenuSymbolImage(MenuSymbol symbol);

// Haiku's width of \a symbol: its pixels - 1.
float							MenuSymbolWidth(MenuSymbol symbol);


} // namespace ngui
