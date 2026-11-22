/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/gfx/Color.h>
#include <ngui/gfx/Point.h>


namespace ngui::gfx {


/*!	One line of Canvas::DrawLines, with its own color (specification 6.1).
	The successor of the line arrays of Haiku's BView (BeginLineArray(),
	AddLine(), EndLineArray()).

	An aggregate and trivially copyable, as the DisplayList requires:
	`Line{p0, p1, color}`.
*/
struct Line {
			Point				p0;		/*!< Start point. */
			Point				p1;		/*!< End point. */
			Color				color;	/*!< Color of this line. */

			constexpr bool		operator==(const Line& other) const = default;
									/*!< Exact comparison of both points and the color. */
};


} // namespace ngui::gfx
