/*
 * Copyright 2024, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "Hsl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>


namespace ngui::detail {


namespace {


// A component as Haiku's 8-bit value.
float
Byte(float value)
{
	return std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}


} // namespace


HslColor
HslColor::FromRgb(const gfx::Color& rgb)
{
	HslColor result;

	const float r = Byte(rgb.r) / 255.0f;
	const float g = Byte(rgb.g) / 255.0f;
	const float b = Byte(rgb.b) / 255.0f;

	const float max = std::max(std::max(r, g), b);
	const float min = std::min(std::min(r, g), b);

	result.hue = result.saturation = result.lightness = (max + min) / 2;

	if (max == min) {
		// grayscale
		result.hue = result.saturation = 0;
	} else {
		const float diff = max - min;
		result.saturation
			= (result.lightness > 0.5f) ? (diff / (2 - max - min)) : (diff / (max + min));

		if (max == r)
			result.hue = (g - b) / diff + (g < b ? 6.0f : 0.0f);
		else if (max == g)
			result.hue = (b - r) / diff + 2;
		else
			result.hue = (r - g) / diff + 4;

		result.hue /= 6;
	}

	return result;
}


gfx::Color
HslColor::ToRgb() const
{
	// uint8(x * 255): truncated, as Haiku's to_rgb()
	const auto byte = [](float value) {
		return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255);
	};
	if (saturation == 0) {
		// grayscale
		const std::uint8_t gray = byte(lightness);
		return gfx::Color::FromRgba8(gray, gray, gray, 255);
	}
	const float q = lightness < 0.5f ? (lightness * (1 + saturation))
		: (lightness + saturation - lightness * saturation);
	const float p = 2 * lightness - q;
	return gfx::Color::FromRgba8(byte(_HueToRgb(p, q, hue + 1.0f / 3)),
		byte(_HueToRgb(p, q, hue)), byte(_HueToRgb(p, q, hue - 1.0f / 3)), 255);
}


// reference: https://en.wikipedia.org/wiki/HSL_and_HSV#Color_conversion_formulae
// (FromRgb() and ToRgb() are derived from the same)
float
HslColor::_HueToRgb(float p, float q, float t)
{
	if (t < 0)
		t += 1;
	if (t > 1)
		t -= 1;
	if (t < 1.0f / 6)
		return p + (q - p) * 6 * t;
	if (t < 1.0f / 2)
		return q;
	if (t < 2.0f / 3)
		return p + (q - p) * (2.0f / 3 - t) * 6;

	return p;
}


} // namespace ngui::detail
