/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <span>
#include <vector>

#include <ngui/gfx/Path.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>


namespace ngui {


/*!	A polygon (Haiku's BPolygon): points in order, drawn closed as ToPath().

	Differences to Haiku: no PrintToStream(); Frame() is the half-open
	rectangle around the points ({0, 0, 0, 0} for none); MapTo() scales by
	half-open widths and leaves an axis of zero source size where it maps
	to (Haiku divided by zero); Points() gives the points (Haiku: only
	BView could).
*/
class BPolygon {
public:
								BPolygon() = default;
									/*!< No points. */
	explicit					BPolygon(std::span<const gfx::Point> points);
									/*!< A polygon of \a points. */

			gfx::Rect			Frame() const;
									/*!< The smallest rectangle around the points. */
			void				AddPoints(std::span<const gfx::Point> points);
									/*!< Appends \a points; nothing beyond 10 000 000
										points in all. */
			int32_t				CountPoints() const;
									/*!< The number of points. */
			std::span<const gfx::Point> Points() const;
									/*!< The points. */
			void				MapTo(gfx::Rect source, gfx::Rect destination);
									/*!< Scales and moves the points so that \a source
										becomes \a destination. */

			gfx::Path			ToPath(bool closed = true) const;
									/*!< NewGUI: the polygon as a gfx::Path, closed unless
										\a closed is false; empty with fewer than two
										points. */

			bool				operator==(const BPolygon& other) const = default;
									/*!< The same points. */

private:
			std::vector<gfx::Point> fPoints;
};


} // namespace ngui
