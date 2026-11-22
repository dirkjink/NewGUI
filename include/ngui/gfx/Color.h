/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>
#include <cstdint>
#include <initializer_list>


namespace ngui::gfx {


/*!	A color: sRGB-encoded components in `[0, 1]`, **not** premultiplied
	(specification 4.3). Replaces Haiku's rgb_color, which has 8 bits per
	component; FromRgba8() converts.

	An aggregate and trivially copyable, as the DisplayList requires
	(specification 5): `Color{r, g, b, a}`.
*/
struct Color {
			float				r;	/*!< Red. */
			float				g;	/*!< Green. */
			float				b;	/*!< Blue. */
			float				a;	/*!< Alpha, 1 is opaque. */

	static	constexpr Color		FromRgba8(std::uint8_t red, std::uint8_t green,
									std::uint8_t blue, std::uint8_t alpha = 255);
									/*!< Converts 8-bit components, `value / 255`. */

			constexpr bool		operator==(const Color& other) const = default;
									/*!< Exact comparison of all components. */

			bool				IsValid() const;
									/*!< Every component is a number in `[0, 1]`. */

	[[nodiscard]] constexpr Color WithAlpha(float alpha) const;
									/*!< The same color with the alpha \a alpha. */
};


constexpr Color
Color::FromRgba8(std::uint8_t red, std::uint8_t green, std::uint8_t blue,
	std::uint8_t alpha)
{
	return {red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f};
}


inline bool
Color::IsValid() const
{
	for (float component : {r, g, b, a}) {
		if (!(component >= 0.0f && component <= 1.0f))
			return false;
	}
	return true;
}


constexpr Color
Color::WithAlpha(float alpha) const
{
	return {r, g, b, alpha};
}


} // namespace ngui::gfx
