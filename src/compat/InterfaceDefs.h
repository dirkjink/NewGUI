/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's types and constants of InterfaceDefs.h and GraphicsDefs.h that the
// ported looks use, internal to the compatibility layer (specification 17,
// K2): here, and only here, Haiku's B_ constants exist; they map onto the
// vocabulary of NewGUI.


#include <cmath>
#include <cstdint>

#include <ngui/InterfaceDefs.h>
#include <ngui/Palette.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Color.h>


namespace ngui::compat {


// Haiku's rgb_color: 8 bits per component, not premultiplied.
struct rgb_color {
	uint8	red;
	uint8	green;
	uint8	blue;
	uint8	alpha;

	rgb_color& set_to(uint8 r, uint8 g, uint8 b, uint8 a = 255)
	{
		red = r;
		green = g;
		blue = b;
		alpha = a;
		return *this;
	}

	bool operator==(const rgb_color& other) const = default;

	// Ported from Haiku: src/kits/interface/GraphicsDefs.cpp and
	// headers/os/interface/GraphicsDefs.h (commit b1d497bb69).
	int32 Brightness() const
	{
		// From http://alienryderflex.com/hsp.html
		// Useful in particular to decide if the color is "light" or "dark"
		// by checking if the perceptual brightness is > 127.

		return (uint8)roundf(sqrtf(
			0.299f * red * red + 0.587f * green * green + 0.114f * blue * blue));
	}

	bool IsDark() const
	{
		return Brightness() <= 127;
	}

	bool IsLight() const
	{
		return Brightness() > 127;
	}
};


inline rgb_color
make_color(uint8 red, uint8 green, uint8 blue, uint8 alpha = 255)
{
	return rgb_color{red, green, blue, alpha};
}


// The Color of the Canvas for an rgb_color, and back (exact for 8 bits).
inline gfx::Color
ToColor(const rgb_color& color)
{
	return gfx::Color::FromRgba8(color.red, color.green, color.blue, color.alpha);
}


inline rgb_color
ToRgbColor(const gfx::Color& color)
{
	const auto byte = [](float value) {
		const float clamped = value < 0 ? 0 : (value > 1 ? 1 : value);
		return static_cast<uint8>(clamped * 255.0f + 0.5f);
	};
	return {byte(color.r), byte(color.g), byte(color.b), byte(color.a)};
}


// Haiku's tint_color(), on 8-bit components like the original.
inline rgb_color
tint_color(rgb_color color, float tint)
{
	return ToRgbColor(TintColor(ToColor(color), tint));
}


// Ported from Haiku: src/kits/interface/GraphicsDefs.cpp (commit
// b1d497bb69). Mix two colors without respect for their alpha values.
inline rgb_color
mix_color(rgb_color color1, rgb_color color2, uint8 amount)
{
	color1.red = (uint8)(((int16(color2.red) - int16(color1.red)) * amount)
		/ 255 + color1.red);
	color1.green = (uint8)(((int16(color2.green) - int16(color1.green))
		* amount) / 255 + color1.green);
	color1.blue = (uint8)(((int16(color2.blue) - int16(color1.blue)) * amount)
		/ 255 + color1.blue );
	color1.alpha = (uint8)(((int16(color2.alpha) - int16(color1.alpha))
		* amount) / 255 + color1.alpha );

	return color1;
}


inline rgb_color
disable_color(rgb_color color, rgb_color background)
{
	return mix_color(color, background, 185);
}


// The tints.
inline constexpr float B_LIGHTEN_MAX_TINT = kLightenMaxTint;
inline constexpr float B_LIGHTEN_2_TINT = kLighten2Tint;
inline constexpr float B_LIGHTEN_1_TINT = kLighten1Tint;
inline constexpr float B_NO_TINT = kNoTint;
inline constexpr float B_DARKEN_1_TINT = kDarken1Tint;
inline constexpr float B_DARKEN_2_TINT = kDarken2Tint;
inline constexpr float B_DARKEN_3_TINT = kDarken3Tint;
inline constexpr float B_DARKEN_4_TINT = kDarken4Tint;
inline constexpr float B_DARKEN_MAX_TINT = kDarkenMaxTint;
inline constexpr float B_DISABLED_LABEL_TINT = kDisabledLabelTint;
inline constexpr float B_HIGHLIGHT_BACKGROUND_TINT = kHighlightBackgroundTint;
inline constexpr float B_DISABLED_MARK_TINT = kDisabledMarkTint;


// The system colors; ui_color() is CompatView::UiColor(), which reads the
// palette of the DrawContext.
using color_which = BColorWhich;

inline constexpr color_which B_PANEL_BACKGROUND_COLOR = BColorWhich::PanelBackground;
inline constexpr color_which B_PANEL_TEXT_COLOR = BColorWhich::PanelText;
inline constexpr color_which B_DOCUMENT_BACKGROUND_COLOR = BColorWhich::DocumentBackground;
inline constexpr color_which B_DOCUMENT_TEXT_COLOR = BColorWhich::DocumentText;
inline constexpr color_which B_CONTROL_BACKGROUND_COLOR = BColorWhich::ControlBackground;
inline constexpr color_which B_CONTROL_TEXT_COLOR = BColorWhich::ControlText;
inline constexpr color_which B_CONTROL_BORDER_COLOR = BColorWhich::ControlBorder;
inline constexpr color_which B_CONTROL_HIGHLIGHT_COLOR = BColorWhich::ControlHighlight;
inline constexpr color_which B_CONTROL_MARK_COLOR = BColorWhich::ControlMark;
inline constexpr color_which B_NAVIGATION_BASE_COLOR = BColorWhich::KeyboardNavigation;
inline constexpr color_which B_KEYBOARD_NAVIGATION_COLOR = BColorWhich::KeyboardNavigation;
inline constexpr color_which B_NAVIGATION_PULSE_COLOR = BColorWhich::NavigationPulse;
inline constexpr color_which B_SHINE_COLOR = BColorWhich::Shine;
inline constexpr color_which B_SHADOW_COLOR = BColorWhich::Shadow;
inline constexpr color_which B_MENU_BACKGROUND_COLOR = BColorWhich::MenuBackground;
inline constexpr color_which B_MENU_SELECTED_BACKGROUND_COLOR
	= BColorWhich::MenuSelectedBackground;
inline constexpr color_which B_MENU_ITEM_TEXT_COLOR = BColorWhich::MenuItemText;
inline constexpr color_which B_MENU_SELECTED_ITEM_TEXT_COLOR = BColorWhich::MenuSelectedItemText;
inline constexpr color_which B_MENU_SELECTED_BORDER_COLOR = BColorWhich::MenuSelectedBorder;
inline constexpr color_which B_LIST_BACKGROUND_COLOR = BColorWhich::ListBackground;
inline constexpr color_which B_LIST_SELECTED_BACKGROUND_COLOR
	= BColorWhich::ListSelectedBackground;
inline constexpr color_which B_LIST_ITEM_TEXT_COLOR = BColorWhich::ListItemText;
inline constexpr color_which B_LIST_SELECTED_ITEM_TEXT_COLOR = BColorWhich::ListSelectedItemText;
inline constexpr color_which B_SCROLL_BAR_THUMB_COLOR = BColorWhich::ScrollBarThumb;
inline constexpr color_which B_TOOL_TIP_BACKGROUND_COLOR = BColorWhich::ToolTipBackground;
inline constexpr color_which B_TOOL_TIP_TEXT_COLOR = BColorWhich::ToolTipText;
inline constexpr color_which B_STATUS_BAR_COLOR = BColorWhich::StatusBar;
inline constexpr color_which B_SUCCESS_COLOR = BColorWhich::Success;
inline constexpr color_which B_FAILURE_COLOR = BColorWhich::Failure;
inline constexpr color_which B_DESKTOP_COLOR = BColorWhich::Desktop;
inline constexpr color_which B_WINDOW_TAB_COLOR = BColorWhich::WindowTab;


// Patterns: only the solid ones exist; the Canvas has no patterns.
enum pattern_kind {
	B_SOLID_HIGH,
	B_SOLID_LOW
};


// Drawing modes: everything is SrcOver (specification 4.3); B_OP_INVERT is
// not supported (16) and warned about in debug builds.
enum drawing_mode {
	B_OP_COPY,
	B_OP_OVER,
	B_OP_ERASE,
	B_OP_INVERT,
	B_OP_ADD,
	B_OP_SUBTRACT,
	B_OP_BLEND,
	B_OP_MIN,
	B_OP_MAX,
	B_OP_SELECT,
	B_OP_ALPHA
};


// Line modes of BView::SetLineMode().
enum cap_mode {
	B_ROUND_CAP,
	B_BUTT_CAP,
	B_SQUARE_CAP
};

enum join_mode {
	B_ROUND_JOIN,
	B_MITER_JOIN,
	B_BEVEL_JOIN,
	B_BUTT_JOIN,
	B_SQUARE_JOIN
};


// Orientation, border style and alignment, as the ported code spells them.
using orientation = BOrientation;
using border_style = BBorderStyle;

enum hash_mark_location {
	B_HASH_MARKS_NONE = 0,
	B_HASH_MARKS_TOP = 1,
	B_HASH_MARKS_LEFT = 1,
	B_HASH_MARKS_BOTTOM = 2,
	B_HASH_MARKS_RIGHT = 2,
	B_HASH_MARKS_BOTH = 3
};

// View flags: only B_SUBPIXEL_PRECISE has an effect (CompatView::SetFlags()).
inline constexpr uint32 B_SUBPIXEL_PRECISE = 0x01000000UL;

inline constexpr BOrientation B_HORIZONTAL = BOrientation::Horizontal;
inline constexpr BOrientation B_VERTICAL = BOrientation::Vertical;
inline constexpr BBorderStyle B_PLAIN_BORDER = BBorderStyle::Plain;
inline constexpr BBorderStyle B_FANCY_BORDER = BBorderStyle::Fancy;
inline constexpr BBorderStyle B_NO_BORDER = BBorderStyle::None;
inline constexpr BHorizontalAlignment B_ALIGN_LEFT = BHorizontalAlignment::Left;
inline constexpr BHorizontalAlignment B_ALIGN_RIGHT = BHorizontalAlignment::Right;
inline constexpr BHorizontalAlignment B_ALIGN_CENTER = BHorizontalAlignment::Center;
inline constexpr BHorizontalAlignment B_ALIGN_HORIZONTAL_CENTER = BHorizontalAlignment::Center;
inline constexpr BHorizontalAlignment B_ALIGN_HORIZONTAL_UNSET = BHorizontalAlignment::Unset;
inline constexpr BHorizontalAlignment B_ALIGN_USE_FULL_WIDTH = BHorizontalAlignment::UseFullWidth;
inline constexpr BVerticalAlignment B_ALIGN_TOP = BVerticalAlignment::Top;
inline constexpr BVerticalAlignment B_ALIGN_MIDDLE = BVerticalAlignment::Middle;
inline constexpr BVerticalAlignment B_ALIGN_BOTTOM = BVerticalAlignment::Bottom;
inline constexpr BVerticalAlignment B_ALIGN_VERTICAL_CENTER = BVerticalAlignment::Middle;
inline constexpr BVerticalAlignment B_ALIGN_VERTICAL_UNSET = BVerticalAlignment::Unset;
inline constexpr BVerticalAlignment B_ALIGN_USE_FULL_HEIGHT = BVerticalAlignment::UseFullHeight;


} // namespace ngui::compat
