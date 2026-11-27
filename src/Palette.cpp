/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Palette.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>

#include <ngui/AppDefs.h>

#include "Hsl.h"
#include "WindowList.h"


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


// Haiku's _kDefaultColorsDark, src/kits/interface/InterfaceDefs.cpp (commit
// b1d497bb69), in the order of BColorWhich.
constexpr std::array<std::array<std::uint8_t, 4>, kColorWhichCount> kDarkColors = {{
	{43, 43, 43, 255},		// PanelBackground
	{28, 28, 28, 255},		// MenuBackground
	{227, 73, 17, 255},		// WindowTab
	{0, 0, 229, 255},		// KeyboardNavigation
	{51, 102, 152, 255},	// Desktop
	{90, 90, 90, 255},		// MenuSelectedBackground
	{255, 255, 255, 255},	// MenuItemText
	{255, 255, 255, 255},	// MenuSelectedItemText
	{0, 0, 0, 255},			// MenuSelectedBorder
	{253, 253, 253, 255},	// PanelText
	{0, 0, 0, 255},			// DocumentBackground
	{234, 234, 234, 255},	// DocumentText
	{29, 29, 29, 255},		// ControlBackground
	{230, 230, 230, 255},	// ControlText
	{195, 195, 195, 255},	// ControlBorder
	{75, 124, 168, 255},	// ControlHighlight
	{0, 0, 0, 255},			// NavigationPulse
	{255, 255, 255, 255},	// Shine
	{0, 0, 0, 255},			// Shadow
	{76, 68, 79, 255},		// ToolTipBackground
	{255, 255, 255, 255},	// ToolTipText
	{255, 255, 255, 255},	// WindowText
	{203, 32, 9, 255},		// WindowInactiveTab
	{255, 255, 255, 255},	// WindowInactiveText
	{227, 73, 17, 255},		// WindowBorder
	{203, 32, 9, 255},		// WindowInactiveBorder
	{27, 82, 140, 255},		// ControlMark
	{0, 0, 0, 255},			// ListBackground
	{90, 90, 90, 255},		// ListSelectedBackground
	{255, 255, 255, 255},	// ListItemText
	{255, 255, 255, 255},	// ListSelectedItemText
	{39, 39, 39, 255},		// ScrollBarThumb
	{106, 112, 212, 255},	// LinkText
	{102, 152, 203, 255},	// LinkHover
	{145, 112, 155, 255},	// LinkVisited
	{121, 142, 203, 255},	// LinkActive
	{50, 150, 255, 255},	// StatusBar
	{46, 204, 64, 255},		// Success
	{255, 40, 54, 255},		// Failure
}};


// The application's colors (Palette::Current()), from any thread.
std::mutex sCurrentLock;
std::optional<Palette> sCurrent;


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


const Palette&
Palette::Dark()
{
	static const Palette sDark = [] {
		Palette palette;
		for (std::size_t i = 0; i < kColorWhichCount; i++) {
			const auto& c = kDarkColors[i];
			palette.fColors[i] = gfx::Color::FromRgba8(c[0], c[1], c[2], c[3]);
		}
		return palette;
	}();
	return sDark;
}


Palette
Palette::Current()
{
	std::scoped_lock lock(sCurrentLock);
	return sCurrent.value_or(Default());
}


void
Palette::SetCurrent(const Palette& palette)
{
	{
		std::scoped_lock lock(sCurrentLock);
		if (sCurrent.value_or(Default()) == palette)
			return;
		sCurrent = palette;
	}
	// every window draws anew in the new colors (B_COLORS_UPDATED)
	detail::PostToWindows(kColorsUpdated);
}


bool
Palette::IsDark() const
{
	// Haiku's rgb_color::IsDark() of the panel background: the perceived
	// brightness (Brightness(), http://alienryderflex.com/hsp.html) at most
	// 127
	const gfx::Color panel = Get(BColorWhich::PanelBackground);
	const auto component = [](float value) {
		return static_cast<float>(_ToByte(value));
	};
	const float red = component(panel.r);
	const float green = component(panel.g);
	const float blue = component(panel.b);
	const auto brightness = static_cast<std::uint8_t>(std::round(std::sqrt(
		0.299f * red * red + 0.587f * green * green + 0.114f * blue * blue)));
	return brightness <= 127;
}


Palette
Palette::WithAccent(const gfx::Color& accent) const
{
	// Haiku's ColorsView::_SetColor() of B_STATUS_BAR_COLOR with the secondary
	// colors picked automatically (src/preferences/appearance/ColorsView.cpp)
	Palette result = *this;
	result.Set(BColorWhich::StatusBar, accent);
	const detail::HslColor statusColorHSL = detail::HslColor::FromRgb(accent);

	detail::HslColor controlHighlight = statusColorHSL;
	controlHighlight.saturation = std::max(0.2f, controlHighlight.saturation / 2.f);
	result.Set(BColorWhich::ControlHighlight, controlHighlight.ToRgb());

	detail::HslColor controlMark = statusColorHSL;
	controlMark.saturation = std::max(0.2f, controlMark.saturation * 0.67f);
	controlMark.lightness = std::max(0.25f, controlMark.lightness * 0.55f);
	result.Set(BColorWhich::ControlMark, controlMark.ToRgb());

	detail::HslColor keyboardNavHSL = statusColorHSL;
	keyboardNavHSL.lightness = std::max(0.2f, keyboardNavHSL.lightness * 0.75f);
	const gfx::Color keyboardNav = keyboardNavHSL.ToRgb();
	// Use primary color channel only.
	const std::uint8_t red = _ToByte(keyboardNav.r);
	const std::uint8_t green = _ToByte(keyboardNav.g);
	const std::uint8_t blue = _ToByte(keyboardNav.b);
	if (blue >= std::max(red, green))
		result.Set(BColorWhich::KeyboardNavigation, gfx::Color::FromRgba8(0, 0, blue, 255));
	else if (red >= std::max(green, blue))
		result.Set(BColorWhich::KeyboardNavigation, gfx::Color::FromRgba8(red, 0, 0, 255));
	else
		result.Set(BColorWhich::KeyboardNavigation, gfx::Color::FromRgba8(0, green, 0, 255));
	return result;
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
