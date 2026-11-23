/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>


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


} // namespace ngui
