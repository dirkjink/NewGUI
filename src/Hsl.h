/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's hsl_color (headers/private/shared/HSL.h, src/kits/shared/HSL.cpp):
// a color as hue, saturation and lightness, from and to 8-bit RGB as Haiku
// converts it (the components truncated to whole bytes).


#include <ngui/gfx/Color.h>


namespace ngui::detail {


struct HslColor {
			float				hue = 0;
			float				saturation = 0;
			float				lightness = 0;

	static	HslColor			FromRgb(const gfx::Color& rgb);
			gfx::Color			ToRgb() const;

private:
	static	float				_HueToRgb(float p, float q, float t);
};


} // namespace ngui::detail
