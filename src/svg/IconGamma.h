/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Icons blended in linear light, as Haiku's IconRenderer
// (src/libs/icon/IconRenderer.cpp) does: its colors go through a gamma
// table of 2.2 before drawing (GammaTable::dir()), the drawn pixels back
// through its inverse (apply_gamma_inv()). Both tables are AGG's
// gamma_lut<int8u, int8u> of 2.2, as Haiku's.


#include <array>
#include <cstdint>
#include <string>
#include <string_view>


namespace ngui::svg::detail {


/*!	AGG's gamma_lut of 2.2: dir() into linear light, inv() back. */
struct IconGamma {
	std::array<std::uint8_t, 256> dir;
	std::array<std::uint8_t, 256> inv;

	static const IconGamma& Haiku();
};


/*!	\a svg with each color it gives fills, strokes, gradient stops and the
	current color (fill, stroke, stop-color, flood-color, lighting-color,
	color, as attributes or CSS declarations) in linear light: #rgb, #rgba,
	#rrggbb, #rrggbbaa and rgb()/rgba() with numbers or percentages go
	through IconGamma::dir(), the alpha stays. Other values (names, url(),
	none, currentColor) stay as they are; black and white are the same in
	linear light. */
std::string						LinearizeColors(std::string_view svg);


} // namespace ngui::svg::detail
