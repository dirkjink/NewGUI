/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Palette.h>

#include <cmath>
#include <cstdint>


namespace ngui {


namespace {


// Haiku's _kDefaultColors (src/kits/interface/InterfaceDefs.cpp, commit
// b1d497bb69), in the order of BColorWhich.
constexpr std::uint8_t kDefaultColors[kColorWhichCount][4] = {
	{216, 216, 216, 255},	// PanelBackground
	{216, 216, 216, 255},	// MenuBackground
	{255, 203, 0, 255},		// WindowTab
	{0, 0, 229, 255},		// KeyboardNavigation
	{51, 102, 152, 255},	// Desktop
	{153, 153, 153, 255},	// MenuSelectedBackground
	{0, 0, 0, 255},			// MenuItemText
	{0, 0, 0, 255},			// MenuSelectedItemText
	{0, 0, 0, 255},			// MenuSelectedBorder
	{0, 0, 0, 255},			// PanelText
	{255, 255, 255, 255},	// DocumentBackground
	{0, 0, 0, 255},			// DocumentText
	{222, 222, 222, 255},	// ControlBackground
	{0, 0, 0, 255},			// ControlText
	{172, 172, 172, 255},	// ControlBorder
	{102, 152, 203, 255},	// ControlHighlight
	{0, 0, 0, 255},			// NavigationPulse
	{255, 255, 255, 255},	// Shine
	{0, 0, 0, 255},			// Shadow
	{255, 255, 216, 255},	// ToolTipBackground
	{0, 0, 0, 255},			// ToolTipText
	{0, 0, 0, 255},			// WindowText
	{232, 232, 232, 255},	// WindowInactiveTab
	{80, 80, 80, 255},		// WindowInactiveText
	{224, 224, 224, 255},	// WindowBorder
	{232, 232, 232, 255},	// WindowInactiveBorder
	{27, 82, 140, 255},		// ControlMark
	{255, 255, 255, 255},	// ListBackground
	{190, 190, 190, 255},	// ListSelectedBackground
	{0, 0, 0, 255},			// ListItemText
	{0, 0, 0, 255},			// ListSelectedItemText
	{216, 216, 216, 255},	// ScrollBarThumb
	{51, 102, 187, 255},	// LinkText
	{102, 152, 203, 255},	// LinkHover
	{145, 112, 155, 255},	// LinkVisited
	{121, 142, 203, 255},	// LinkActive
	{50, 150, 255, 255},	// StatusBar
	{46, 204, 64, 255},		// Success
	{255, 65, 54, 255},		// Failure
};


// A float component as Haiku's 8-bit value.
std::uint8_t
_ToByte(float value)
{
	const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
	return static_cast<std::uint8_t>(std::lround(clamped * 255.0f));
}


} // namespace


const Palette&
Palette::Default()
{
	static const Palette sDefault = [] {
		Palette palette;
		for (std::size_t i = 0; i < kColorWhichCount; i++) {
			const auto& c = kDefaultColors[i];
			palette.fColors[i] = gfx::Color::FromRgba8(c[0], c[1], c[2], c[3]);
		}
		return palette;
	}();
	return sDefault;
}


gfx::Color
Palette::Get(BColorWhich which) const
{
	return fColors[static_cast<std::size_t>(which)];
}


void
Palette::Set(BColorWhich which, const gfx::Color& color)
{
	fColors[static_cast<std::size_t>(which)] = color;
}


gfx::Color
TintColor(const gfx::Color& color, float tint)
{
	// Haiku's tint_color(), src/kits/interface/InterfaceDefs.cpp:
	//   LIGHTEN(x) ((uint8)(255.0f - (255.0f - x) * tint))
	//   DARKEN(x)  ((uint8)(x * (2 - tint)))
	const auto tinted = [tint](float component) {
		const float x = _ToByte(component);
		const float value = tint < 1.0f ? 255.0f - (255.0f - x) * tint : x * (2 - tint);
		const float clamped = value < 0.0f ? 0.0f : (value > 255.0f ? 255.0f : value);
		return static_cast<std::uint8_t>(clamped);
	};
	return gfx::Color::FromRgba8(tinted(color.r), tinted(color.g), tinted(color.b),
		_ToByte(color.a));
}


} // namespace ngui
