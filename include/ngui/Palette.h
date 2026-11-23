/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <array>
#include <cstddef>
#include <cstdint>

#include <ngui/gfx/Color.h>


namespace ngui {


/*!	The system colors (Haiku's color_which), the roles a Palette assigns
	colors to. The order follows Haiku's table of default colors. */
enum class BColorWhich : std::uint8_t {
	PanelBackground,			/*!< B_PANEL_BACKGROUND_COLOR */
	MenuBackground,				/*!< B_MENU_BACKGROUND_COLOR */
	WindowTab,					/*!< B_WINDOW_TAB_COLOR */
	KeyboardNavigation,			/*!< B_KEYBOARD_NAVIGATION_COLOR (B_NAVIGATION_BASE_COLOR) */
	Desktop,					/*!< B_DESKTOP_COLOR */
	MenuSelectedBackground,		/*!< B_MENU_SELECTED_BACKGROUND_COLOR */
	MenuItemText,				/*!< B_MENU_ITEM_TEXT_COLOR */
	MenuSelectedItemText,		/*!< B_MENU_SELECTED_ITEM_TEXT_COLOR */
	MenuSelectedBorder,			/*!< B_MENU_SELECTED_BORDER_COLOR */
	PanelText,					/*!< B_PANEL_TEXT_COLOR */
	DocumentBackground,			/*!< B_DOCUMENT_BACKGROUND_COLOR */
	DocumentText,				/*!< B_DOCUMENT_TEXT_COLOR */
	ControlBackground,			/*!< B_CONTROL_BACKGROUND_COLOR */
	ControlText,				/*!< B_CONTROL_TEXT_COLOR */
	ControlBorder,				/*!< B_CONTROL_BORDER_COLOR */
	ControlHighlight,			/*!< B_CONTROL_HIGHLIGHT_COLOR */
	NavigationPulse,			/*!< B_NAVIGATION_PULSE_COLOR */
	Shine,						/*!< B_SHINE_COLOR */
	Shadow,						/*!< B_SHADOW_COLOR */
	ToolTipBackground,			/*!< B_TOOL_TIP_BACKGROUND_COLOR */
	ToolTipText,				/*!< B_TOOL_TIP_TEXT_COLOR */
	WindowText,					/*!< B_WINDOW_TEXT_COLOR */
	WindowInactiveTab,			/*!< B_WINDOW_INACTIVE_TAB_COLOR */
	WindowInactiveText,			/*!< B_WINDOW_INACTIVE_TEXT_COLOR */
	WindowBorder,				/*!< B_WINDOW_BORDER_COLOR */
	WindowInactiveBorder,		/*!< B_WINDOW_INACTIVE_BORDER_COLOR */
	ControlMark,				/*!< B_CONTROL_MARK_COLOR */
	ListBackground,				/*!< B_LIST_BACKGROUND_COLOR */
	ListSelectedBackground,		/*!< B_LIST_SELECTED_BACKGROUND_COLOR */
	ListItemText,				/*!< B_LIST_ITEM_TEXT_COLOR */
	ListSelectedItemText,		/*!< B_LIST_SELECTED_ITEM_TEXT_COLOR */
	ScrollBarThumb,				/*!< B_SCROLL_BAR_THUMB_COLOR */
	LinkText,					/*!< B_LINK_TEXT_COLOR */
	LinkHover,					/*!< B_LINK_HOVER_COLOR */
	LinkVisited,				/*!< B_LINK_VISITED_COLOR */
	LinkActive,					/*!< B_LINK_ACTIVE_COLOR */
	StatusBar,					/*!< B_STATUS_BAR_COLOR */
	Success,					/*!< B_SUCCESS_COLOR */
	Failure						/*!< B_FAILURE_COLOR */
};

/*!	The number of BColorWhich values. */
inline constexpr std::size_t kColorWhichCount = static_cast<std::size_t>(BColorWhich::Failure) + 1;


/*!	The system colors a look and self-drawing views take their colors from
	(specification 18.1, L3), the counterpart of Haiku's ui_color(). A value
	type: a look gets the palette through its DrawContext. Change
	notification and user settings come with module G6.
*/
class Palette {
public:
	static	const Palette&		Default();
									/*!< Haiku's default light colors. */

			gfx::Color			Get(BColorWhich which) const;
									/*!< The color for \a which. */
			void				Set(BColorWhich which, const gfx::Color& color);
									/*!< Changes the color for \a which. */

			bool				operator==(const Palette& other) const = default;
									/*!< The same colors. */

private:
			std::array<gfx::Color, kColorWhichCount> fColors = {};
};


// The tints of Haiku's InterfaceDefs.h, for TintColor(); the comments give
// the result of TintColor() for a component of 216. (Haiku's comments say
// 240 and 232 for the lighten tints, but tint_color() truncates.)
inline constexpr float kLightenMaxTint = 0.0f;		/*!< B_LIGHTEN_MAX_TINT: 216 → 255 */
inline constexpr float kLighten2Tint = 0.385f;		/*!< B_LIGHTEN_2_TINT: 216 → 239 */
inline constexpr float kLighten1Tint = 0.590f;		/*!< B_LIGHTEN_1_TINT: 216 → 231 */
inline constexpr float kNoTint = 1.0f;				/*!< B_NO_TINT: 216 → 216 */
inline constexpr float kDarken1Tint = 1.147f;		/*!< B_DARKEN_1_TINT: 216 → 184 */
inline constexpr float kDarken2Tint = 1.295f;		/*!< B_DARKEN_2_TINT: 216 → 152 */
inline constexpr float kDarken3Tint = 1.407f;		/*!< B_DARKEN_3_TINT: 216 → 128 */
inline constexpr float kDarken4Tint = 1.555f;		/*!< B_DARKEN_4_TINT: 216 → 96 */
inline constexpr float kDarkenMaxTint = 2.0f;		/*!< B_DARKEN_MAX_TINT: 216 → 0 */
inline constexpr float kDisabledLabelTint = kDarken3Tint;		/*!< B_DISABLED_LABEL_TINT */
inline constexpr float kHighlightBackgroundTint = kDarken2Tint;	/*!< B_HIGHLIGHT_BACKGROUND_TINT */
inline constexpr float kDisabledMarkTint = kLighten2Tint;		/*!< B_DISABLED_MARK_TINT */


/*!	\a color lightened (tint < 1) or darkened (tint > 1) exactly like Haiku's
	tint_color(): on 8-bit components, truncated, alpha unchanged. The 8-bit
	steps are what makes the ported looks pixel-identical to Haiku.
*/
gfx::Color						TintColor(const gfx::Color& color, float tint);


} // namespace ngui
