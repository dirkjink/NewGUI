/*
 * Copyright 2003-2010, Haiku, Inc.
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


class BShape;


/*!	Walks through the steps of a BShape (Haiku's BShapeIterator): Iterate()
	calls a hook for every step, in order. The hooks do nothing by default.

	Differences to Haiku: the hooks get spans of constant points and return
	nothing (Haiku's status_t was not checked); Iterate() takes a constant
	shape.
*/
class BShapeIterator {
public:
	virtual						~BShapeIterator();
									/*!< Default. */

	virtual	void				IterateMoveTo(gfx::Point point);
									/*!< A contour starts at \a point. */
	virtual	void				IterateLineTo(std::span<const gfx::Point> points);
									/*!< Lines to each of \a points in turn. */
	virtual	void				IterateBezierTo(std::span<const gfx::Point> points);
									/*!< Cubic Bézier curves, three points each: two
										control points, then the end. */
	virtual	void				IterateClose();
									/*!< The contour is closed. */
	virtual	void				IterateArcTo(float rx, float ry, float angle,
									bool largeArc, bool counterClockWise,
									gfx::Point point);
									/*!< An elliptical arc to \a point, as ArcTo() took
										it. */

			void				Iterate(const BShape& shape);
									/*!< Calls the hooks for the steps of \a shape. */
};


/*!	A figure of lines, cubic Bézier curves and elliptical arcs in contours
	(Haiku's BShape). Canvas::FillPath() and Canvas::StrokePath() draw it as
	ToPath(); add a translation for Haiku's pen position:

	\code
	context.canvas.FillPath(shape.ToPath(), gfx::Paint{.color = blue});
	\endcode

	Differences to Haiku: no archiving; building returns nothing (no
	B_NO_MEMORY). Bounds() covers the points of lines and curves and the
	arcs' control points, not the radii and angles Haiku counted in, and is
	half-open like every NewGUI rectangle. Views draw through their
	DrawContext's Canvas (no BView::FillShape()).
*/
class BShape {
public:
								BShape() = default;
									/*!< An empty shape. */
								BShape(const BShape& other) = default;
									/*!< A copy. */
								BShape(BShape&& other) noexcept;
									/*!< Takes over the steps of \a other, which becomes
										empty. */
			BShape&				operator=(const BShape& other) = default;
									/*!< Becomes a copy. */
			BShape&				operator=(BShape&& other) noexcept;
									/*!< Takes over the steps of \a other, which becomes
										empty. */

			bool				operator==(const BShape& other) const;
									/*!< The same steps and points. */

			void				Clear();
									/*!< Removes everything. */
			void				MoveFrom(BShape& other);
									/*!< Takes over the steps of \a other, which becomes
										empty. */
			gfx::Rect			Bounds() const;
									/*!< The smallest rectangle around the points, control
										points included; empty for an empty shape. */
			gfx::Point			CurrentPosition() const;
									/*!< The last point added, (0, 0) for none. */

			void				AddShape(const BShape& other);
									/*!< Appends the steps of \a other. */
			void				MoveTo(gfx::Point point);
									/*!< Starts a contour; replaces the point of a MoveTo()
										right before. */
			void				LineTo(gfx::Point point);
									/*!< A line to \a point. */
			void				BezierTo(const gfx::Point controlPoints[3]);
									/*!< A cubic Bézier curve: two control points, then the
										end. */
			void				BezierTo(gfx::Point control1, gfx::Point control2,
									gfx::Point endPoint);
									/*!< A cubic Bézier curve to \a endPoint. */
			void				ArcTo(float rx, float ry, float angle, bool largeArc,
									bool counterClockWise, gfx::Point point);
									/*!< An elliptical arc to \a point, as SVG's "A": radii
										\a rx and \a ry, the ellipse turned by \a angle
										(radians, as Haiku's app_server takes it), the large
										or the small one of the two arcs, drawn against or
										with the clock. */
			void				Close();
									/*!< Closes the contour; nothing after Close() or
										MoveTo(). */

			gfx::Path			ToPath() const;
									/*!< NewGUI: the shape as a gfx::Path, arcs as cubic
										Bézier curves (as SVG's implementation notes F.6;
										an arc with a zero radius is a line, one to its
										own start nothing). */

private:
	friend class BShapeIterator;

			std::vector<std::uint32_t> fOps;
			std::vector<gfx::Point> fPoints;
			std::uint32_t		fBuildingOp = 0;
};


} // namespace ngui
