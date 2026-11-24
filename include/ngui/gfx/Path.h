/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <span>
#include <vector>

#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>


namespace ngui::gfx {


/*!	A step of a Path. The values are part of the DisplayList format and never
	change. */
enum class PathVerb : std::uint8_t {
	Move = 0,	/*!< Starts a contour at one point. */
	Line = 1,	/*!< A straight line to one point. */
	Cubic = 2,	/*!< A cubic Bézier curve: two control points, then the end. */
	Close = 3	/*!< Closes the contour with a line to its start; no point. */
};


/*!	Which areas a filled path covers. The values are part of the DisplayList
	format and never change. */
enum class FillRule : std::uint8_t {
	NonZero = 0,	/*!< Where the winding number is not zero (Haiku's default). */
	EvenOdd = 1		/*!< Where an odd number of edges is crossed. */
};


/*!	A figure of straight lines and cubic Bézier curves, in contours
	(extended profile, specification 16): what Canvas::FillPath() and
	Canvas::StrokePath() draw, and what BShape and BPolygon turn into.

	A LineTo() or CubicTo() without a contour starts one where the last one
	started (at 0, 0 for the first), as in Skia; a MoveTo() after a MoveTo()
	replaces it. Not thread-safe.
*/
class Path {
public:
	/*!	A contour as straight lines, from Flatten(). */
	struct Polyline {
			std::vector<Point>	points;	/*!< The points; the first is the start. */
			bool				closed;	/*!< Close() ended it. */
	};

			void				MoveTo(Point point);
									/*!< Starts a contour at \a point. */
			void				LineTo(Point point);
									/*!< A line to \a point. */
			void				CubicTo(Point control1, Point control2, Point end);
									/*!< A cubic Bézier curve to \a end. */
			void				Close();
									/*!< Closes the contour; nothing without one or when
										it is closed already. */
			void				Clear();
									/*!< Removes everything, keeps the fill rule. */

			void				SetFillRule(FillRule rule);
									/*!< How it is filled; NonZero by default. */
			FillRule			Rule() const;
									/*!< How it is filled. */

			bool				IsEmpty() const;
									/*!< Nothing was added. */
			bool				IsFinite() const;
									/*!< No point is NaN or infinite. */
			std::span<const PathVerb> Verbs() const;
									/*!< The steps, in order. */
			std::span<const Point> Points() const;
									/*!< The points of the steps: one for Move and Line,
										three for Cubic, none for Close. */
			Rect				Bounds() const;
									/*!< The smallest rectangle around all points, control
										points included; empty for an empty path. */

			void				Translate(float dx, float dy);
									/*!< Moves all points. */
			std::vector<Polyline> Flatten(float tolerance) const;
									/*!< The contours as straight lines, the curves within
										\a tolerance of their exact shape (at most 1024
										lines per curve): the fallback for a backend without
										Caps::bezierPaths. A contour of a single point is
										dropped. */

			bool				operator==(const Path& other) const = default;
									/*!< Same steps, points and fill rule. */

	static	bool				IsValidSequence(std::span<const PathVerb> verbs,
									std::size_t pointCount);
									/*!< The verbs are known, start with Move (if any) and
										need exactly \a pointCount points. */

private:
			void				_EnsureContour();

			std::vector<PathVerb> fVerbs;
			std::vector<Point>	fPoints;
			FillRule			fRule = FillRule::NonZero;
			Point				fContourStart = {0, 0};
			bool				fOpen = false;
									// a contour is open (Move, not yet closed)
};


} // namespace ngui::gfx
