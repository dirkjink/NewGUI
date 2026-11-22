/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>
#include <cstdint>
#include <optional>

#include <ngui/gfx/Color.h>
#include <ngui/gfx/LinearGradient.h>


namespace ngui::gfx {


/*!	How a Canvas operation fills or strokes (specification 8). Replaces the
	drawing state of Haiku's BView (high color, pen size, line mode), which
	the Canvas has no equivalent for: every operation gets its Paint.

	An aggregate and trivially copyable, as the DisplayList requires:
	`Paint{.color = red, .strokeWidth = 2}`.
*/
struct Paint {
	/*!	The end of an open stroke. */
	enum class Cap : std::uint8_t {
		Butt,	/*!< Ends exactly at the end point. */
		Round,	/*!< A half circle beyond the end point. */
		Square	/*!< Half the stroke width beyond the end point (rule A3). */
	};

	/*!	The corner between two segments of a stroke. */
	enum class Join : std::uint8_t {
		Miter,	/*!< A sharp corner. */
		Round,	/*!< A rounded corner. */
		Bevel	/*!< A cut-off corner. */
	};

			Color				color = {0, 0, 0, 1};
									/*!< Solid color; ignored if #gradient is set. */
			std::optional<LinearGradient> gradient = std::nullopt;
									/*!< If set, used instead of #color; ignored by
										DrawLines, DrawImage and DrawGlyphRun. */
			float				strokeWidth = 1.0f;
									/*!< Width of strokes in logical pixels. */
			Cap					cap = Cap::Butt;
									/*!< Ends of open strokes. */
			Join				join = Join::Miter;
									/*!< Corners of strokes. */
			bool				antialias = true;
									/*!< Antialiasing of non-axis-parallel geometry (rule A2);
										axis-parallel primitives are never antialiased (A1). */

			bool				operator==(const Paint& other) const = default;
									/*!< All fields equal. */

			bool				IsValid() const;
									/*!< The color and the gradient (if set) are valid, and the
										stroke width is finite and not negative
										(specification 6.2). */
};


inline bool
Paint::IsValid() const
{
	return color.IsValid() && (!gradient.has_value() || gradient->IsValid())
		&& std::isfinite(strokeWidth) && strokeWidth >= 0.0f;
}


} // namespace ngui::gfx
