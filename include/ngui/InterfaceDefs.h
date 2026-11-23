/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>

#include <ngui/Flags.h>


namespace ngui {


// Enumerations of Haiku's InterfaceDefs.h, Alignment.h and Slider.h with the
// same meaning, as enum class (CONVENTIONS.md, section 3, rule 5); in the
// port phase with the B prefix, without it after the switch (section 6).


/*!	Horizontal or vertical (Haiku's orientation). */
enum class BOrientation : std::uint8_t {
	Horizontal,	/*!< B_HORIZONTAL */
	Vertical	/*!< B_VERTICAL */
};


/*!	The border of a view (Haiku's border_style). */
enum class BBorderStyle : std::uint8_t {
	Plain,	/*!< B_PLAIN_BORDER */
	Fancy,	/*!< B_FANCY_BORDER */
	None	/*!< B_NO_BORDER */
};


/*!	Horizontal alignment (Haiku's alignment). */
enum class BHorizontalAlignment : std::int8_t {
	Left,			/*!< B_ALIGN_LEFT */
	Right,			/*!< B_ALIGN_RIGHT */
	Center,			/*!< B_ALIGN_CENTER */
	Unset,			/*!< B_ALIGN_HORIZONTAL_UNSET */
	UseFullWidth	/*!< B_ALIGN_USE_FULL_WIDTH */
};


/*!	Vertical alignment (Haiku's vertical_alignment). */
enum class BVerticalAlignment : std::int8_t {
	Top,			/*!< B_ALIGN_TOP */
	Middle,			/*!< B_ALIGN_MIDDLE */
	Bottom,			/*!< B_ALIGN_BOTTOM */
	Unset,			/*!< B_ALIGN_VERTICAL_UNSET */
	UseFullHeight	/*!< B_ALIGN_USE_FULL_HEIGHT */
};


// The bytes of keys that type no text (documented in docs/api/ngui.dox).
namespace keys {
inline constexpr char kBackspace = 0x08;	/*!< B_BACKSPACE */
inline constexpr char kTab = 0x09;			/*!< B_TAB */
inline constexpr char kEnter = 0x0a;		/*!< B_ENTER, B_RETURN */
inline constexpr char kSpace = 0x20;		/*!< B_SPACE */
inline constexpr char kEscape = 0x1b;		/*!< B_ESCAPE */
inline constexpr char kLeftArrow = 0x1c;	/*!< B_LEFT_ARROW */
inline constexpr char kRightArrow = 0x1d;	/*!< B_RIGHT_ARROW */
inline constexpr char kUpArrow = 0x1e;		/*!< B_UP_ARROW */
inline constexpr char kDownArrow = 0x1f;	/*!< B_DOWN_ARROW */
inline constexpr char kInsert = 0x05;		/*!< B_INSERT */
inline constexpr char kDelete = 0x7f;		/*!< B_DELETE */
inline constexpr char kHome = 0x01;			/*!< B_HOME */
inline constexpr char kEnd = 0x04;			/*!< B_END */
inline constexpr char kPageUp = 0x0b;		/*!< B_PAGE_UP */
inline constexpr char kPageDown = 0x0c;		/*!< B_PAGE_DOWN */
inline constexpr char kFunctionKey = 0x10;	/*!< B_FUNCTION_KEY: F1 to F12 */
} // namespace keys


/*!	Horizontal and vertical alignment together, like Haiku's BAlignment. */
struct BAlignment {
			BHorizontalAlignment horizontal = BHorizontalAlignment::Unset;
									/*!< Horizontal part. */
			BVerticalAlignment	vertical = BVerticalAlignment::Unset;
									/*!< Vertical part. */

			constexpr bool		operator==(const BAlignment& other) const = default;
									/*!< Both parts equal. */
};


/*!	Where a slider draws its hash marks (Haiku's hash_mark_location). */
enum class BHashMarkLocation : std::uint8_t {
	None = 0,			/*!< B_HASH_MARKS_NONE */
	TopOrLeft = 1,		/*!< B_HASH_MARKS_TOP, B_HASH_MARKS_LEFT */
	BottomOrRight = 2,	/*!< B_HASH_MARKS_BOTTOM, B_HASH_MARKS_RIGHT */
	Both = 3			/*!< B_HASH_MARKS_BOTH */
};


/*!	The modifier keys in the "modifiers" field of mouse and key events (AppDefs.h), as
	Haiku's B_SHIFT_KEY ... On PC keyboards Alt is Command (Haiku's default),
	Ctrl is Control and the Windows or Super key is Option. */
enum class BModifier : std::uint32_t {
	Shift = 1u << 0,		/*!< B_SHIFT_KEY */
	Command = 1u << 1,		/*!< B_COMMAND_KEY: Alt, or Cmd on a Mac. */
	Control = 1u << 2,		/*!< B_CONTROL_KEY: Ctrl. */
	CapsLock = 1u << 3,		/*!< B_CAPS_LOCK */
	ScrollLock = 1u << 4,	/*!< B_SCROLL_LOCK */
	NumLock = 1u << 5,		/*!< B_NUM_LOCK */
	Option = 1u << 6		/*!< B_OPTION_KEY: Windows or Super key, Option on a Mac. */
};
NGUI_FLAG_ENUM(BModifier)

/*!	A set of BModifier keys. */
using BModifierFlags = Flags<BModifier>;


} // namespace ngui
