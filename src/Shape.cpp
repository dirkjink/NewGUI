/*
 * Copyright 2003-2010, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Marc Flerackers, mflerackers@androme.be
 *		Michael Lotz, mmlr@mlotz.ch
 *		Marcus Overhagen, marcus@overhagen.de
 */

// Ported from Haiku: src/kits/interface/Shape.cpp and the op encoding of
// headers/private/interface/ShapePrivate.h (commit b1d497bb69). The steps
// are kept in Haiku's encoding, so that BShapeIterator groups them as in
// Haiku. ToPath() replaces the app_server's Painter::_IterateShapeData():
// arcs become cubic Bézier curves as AGG's arc_to() makes them (SVG's
// implementation notes F.6), with the angle in radians and "clockwise" as
// SVG's sweep flag 1.
//
// Differences to Haiku: no archiving and no B_NO_MEMORY; Bounds() leaves
// out the radii and angles of arcs; a step count is kept below the 24 bits
// of its op (Haiku's count would run into the flags after 16 million
// points).

#include <ngui/Shape.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>


namespace ngui {


namespace {


constexpr std::uint32_t kOpLineTo = 0x10000000;
constexpr std::uint32_t kOpBezierTo = 0x20000000;
constexpr std::uint32_t kOpClose = 0x40000000;
constexpr std::uint32_t kOpMoveTo = 0x80000000;
constexpr std::uint32_t kOpLargeArcToCw = 0x01000000;
constexpr std::uint32_t kOpLargeArcToCcw = 0x02000000;
constexpr std::uint32_t kOpSmallArcToCw = 0x04000000;
constexpr std::uint32_t kOpSmallArcToCcw = 0x08000000;
constexpr std::uint32_t kOpArcs = kOpLargeArcToCw | kOpLargeArcToCcw | kOpSmallArcToCw
	| kOpSmallArcToCcw;
constexpr std::uint32_t kCountMask = 0x00FFFFFF;
// NewGUI: a new op before the count reaches the flags
constexpr std::uint32_t kMaxCount = kCountMask - 3;


// An elliptical arc from \a from to \a to into \a path as cubic Bézier
// curves (SVG 1.1, implementation notes F.6.5 and F.6.6; AGG's
// bezier_arc_svg). \a sweep: the positive-angle direction, clockwise on the
// screen.
void
AddArc(gfx::Path& path, gfx::Point from, float rx, float ry, float angle, bool largeArc,
	bool sweep, gfx::Point to)
{
	// F.6.2: identical end points omit the arc, a zero radius makes a line
	if (from == to)
		return;
	double radiusX = std::fabs(static_cast<double>(rx));
	double radiusY = std::fabs(static_cast<double>(ry));
	if (radiusX == 0 || radiusY == 0 || !std::isfinite(radiusX) || !std::isfinite(radiusY)
		|| !std::isfinite(angle)) {
		path.LineTo(to);
		return;
	}

	const double cosPhi = std::cos(static_cast<double>(angle));
	const double sinPhi = std::sin(static_cast<double>(angle));
	const double dx = (static_cast<double>(from.x) - to.x) / 2;
	const double dy = (static_cast<double>(from.y) - to.y) / 2;
	// F.6.5.1
	const double x1 = cosPhi * dx + sinPhi * dy;
	const double y1 = -sinPhi * dx + cosPhi * dy;
	// F.6.6: radii too small are scaled up
	const double lambda = (x1 * x1) / (radiusX * radiusX) + (y1 * y1) / (radiusY * radiusY);
	if (lambda > 1) {
		radiusX *= std::sqrt(lambda);
		radiusY *= std::sqrt(lambda);
	}
	// F.6.5.2
	const double rx2 = radiusX * radiusX;
	const double ry2 = radiusY * radiusY;
	const double denominator = rx2 * y1 * y1 + ry2 * x1 * x1;
	double coefficient = denominator > 0
		? std::sqrt(std::max(0.0, (rx2 * ry2 - denominator) / denominator)) : 0;
	if (largeArc == sweep)
		coefficient = -coefficient;
	const double cx1 = coefficient * radiusX * y1 / radiusY;
	const double cy1 = -coefficient * radiusY * x1 / radiusX;
	// F.6.5.3
	const double cx = cosPhi * cx1 - sinPhi * cy1 + (static_cast<double>(from.x) + to.x) / 2;
	const double cy = sinPhi * cx1 + cosPhi * cy1 + (static_cast<double>(from.y) + to.y) / 2;
	// F.6.5.5 and 6
	const auto angleOf = [](double ux, double uy, double vx, double vy) {
		return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
	};
	const double ux = (x1 - cx1) / radiusX;
	const double uy = (y1 - cy1) / radiusY;
	const double vx = (-x1 - cx1) / radiusX;
	const double vy = (-y1 - cy1) / radiusY;
	const double start = angleOf(1, 0, ux, uy);
	double extent = angleOf(ux, uy, vx, vy);
	if (!sweep && extent > 0)
		extent -= 2 * std::numbers::pi;
	else if (sweep && extent < 0)
		extent += 2 * std::numbers::pi;
	if (!std::isfinite(start) || !std::isfinite(extent)) {
		path.LineTo(to);
		return;
	}

	// at most a quarter turn per curve
	const int segments = std::max(1, static_cast<int>(std::ceil(std::fabs(extent)
		/ (std::numbers::pi / 2) - 1e-9)));
	const double step = extent / segments;
	const double handle = 4.0 / 3.0 * std::tan(step / 4);
	const auto map = [&](double x, double y) {
		return gfx::Point{static_cast<float>(cx + radiusX * cosPhi * x - radiusY * sinPhi * y),
			static_cast<float>(cy + radiusX * sinPhi * x + radiusY * cosPhi * y)};
	};
	for (int i = 0; i < segments; i++) {
		const double a = start + step * i;
		const double b = a + step;
		const double cosA = std::cos(a);
		const double sinA = std::sin(a);
		const double cosB = std::cos(b);
		const double sinB = std::sin(b);
		const gfx::Point control1 = map(cosA - handle * sinA, sinA + handle * cosA);
		const gfx::Point control2 = map(cosB + handle * sinB, sinB - handle * cosB);
		// the last curve ends exactly at the target
		const gfx::Point end = i == segments - 1 ? to : map(cosB, sinB);
		path.CubicTo(control1, control2, end);
	}
}


} // namespace


// #pragma mark - BShapeIterator


BShapeIterator::~BShapeIterator() = default;


void
BShapeIterator::IterateMoveTo(gfx::Point /*point*/)
{
}


void
BShapeIterator::IterateLineTo(std::span<const gfx::Point> /*points*/)
{
}


void
BShapeIterator::IterateBezierTo(std::span<const gfx::Point> /*points*/)
{
}


void
BShapeIterator::IterateClose()
{
}


void
BShapeIterator::IterateArcTo(float /*rx*/, float /*ry*/, float /*angle*/, bool /*largeArc*/,
	bool /*counterClockWise*/, gfx::Point /*point*/)
{
}


void
BShapeIterator::Iterate(const BShape& shape)
{
	const std::span<const gfx::Point> points = shape.fPoints;
	std::size_t next = 0;
	for (const std::uint32_t entry : shape.fOps) {
		const std::uint32_t op = entry & ~kCountMask;
		const std::size_t count = entry & kCountMask;
		if ((op & kOpMoveTo) != 0) {
			IterateMoveTo(points[next]);
			next++;
		}

		if ((op & kOpLineTo) != 0) {
			IterateLineTo(points.subspan(next, count));
			next += count;
		}

		if ((op & kOpBezierTo) != 0) {
			IterateBezierTo(points.subspan(next, count));
			next += count;
		}

		if ((op & kOpArcs) != 0) {
			for (std::size_t i = 0; i < count / 3; i++) {
				IterateArcTo(points[next].x, points[next].y, points[next + 1].x,
					(op & (kOpLargeArcToCw | kOpLargeArcToCcw)) != 0,
					(op & (kOpSmallArcToCcw | kOpLargeArcToCcw)) != 0, points[next + 2]);
				next += 3;
			}
		}

		if ((op & kOpClose) != 0)
			IterateClose();
	}
}


// #pragma mark - BShape


BShape::BShape(BShape&& other) noexcept
{
	MoveFrom(other);
}


BShape&
BShape::operator=(BShape&& other) noexcept
{
	if (this != &other)
		MoveFrom(other);
	return *this;
}


bool
BShape::operator==(const BShape& other) const
{
	return fOps == other.fOps && fPoints == other.fPoints;
}


void
BShape::Clear()
{
	fOps.clear();
	fPoints.clear();
	fBuildingOp = 0;
}


void
BShape::MoveFrom(BShape& other)
{
	fOps = std::move(other.fOps);
	fPoints = std::move(other.fPoints);
	fBuildingOp = other.fBuildingOp;
	other.Clear();
}


gfx::Rect
BShape::Bounds() const
{
	// NewGUI: from the path; Haiku took the arcs' radii and angles for points
	return ToPath().Bounds();
}


gfx::Point
BShape::CurrentPosition() const
{
	if (fPoints.empty())
		return {0, 0};
	return fPoints.back();
}


void
BShape::AddShape(const BShape& other)
{
	fOps.insert(fOps.end(), other.fOps.begin(), other.fOps.end());
	fPoints.insert(fPoints.end(), other.fPoints.begin(), other.fPoints.end());
	fBuildingOp = other.fBuildingOp;
}


void
BShape::MoveTo(gfx::Point point)
{
	// If the last op is MoveTo, replace the point
	if (fBuildingOp == kOpMoveTo) {
		fPoints.back() = point;
		return;
	}

	fBuildingOp = kOpMoveTo;
	fOps.push_back(fBuildingOp);
	fPoints.push_back(point);
}


void
BShape::LineTo(gfx::Point point)
{
	// If the last op is MoveTo, replace the op and set the count
	// If the last op is LineTo increase the count
	// Otherwise add the op
	if (((fBuildingOp & kOpLineTo) != 0 || fBuildingOp == kOpMoveTo)
		&& (fBuildingOp & kCountMask) < kMaxCount) {
		fBuildingOp |= kOpLineTo;
		fBuildingOp += 1;
		fOps.back() = fBuildingOp;
	} else {
		fBuildingOp = kOpLineTo + 1;
		fOps.push_back(fBuildingOp);
	}

	fPoints.push_back(point);
}


void
BShape::BezierTo(const gfx::Point controlPoints[3])
{
	BezierTo(controlPoints[0], controlPoints[1], controlPoints[2]);
}


void
BShape::BezierTo(gfx::Point control1, gfx::Point control2, gfx::Point endPoint)
{
	// If the last op is MoveTo, replace the op and set the count
	// If the last op is BezierTo increase the count
	// Otherwise add the op
	if (((fBuildingOp & kOpBezierTo) != 0 || fBuildingOp == kOpMoveTo)
		&& (fBuildingOp & kCountMask) < kMaxCount) {
		fBuildingOp |= kOpBezierTo;
		fBuildingOp += 3;
		fOps.back() = fBuildingOp;
	} else {
		fBuildingOp = kOpBezierTo + 3;
		fOps.push_back(fBuildingOp);
	}

	fPoints.push_back(control1);
	fPoints.push_back(control2);
	fPoints.push_back(endPoint);
}


void
BShape::ArcTo(float rx, float ry, float angle, bool largeArc, bool counterClockWise,
	gfx::Point point)
{
	std::uint32_t op;
	if (largeArc)
		op = counterClockWise ? kOpLargeArcToCcw : kOpLargeArcToCw;
	else
		op = counterClockWise ? kOpSmallArcToCcw : kOpSmallArcToCw;

	// If the last op is MoveTo, replace the op and set the count
	// If the last op is ArcTo increase the count
	// Otherwise add the op
	if ((fBuildingOp == op || fBuildingOp == (op | kOpMoveTo))
		&& (fBuildingOp & kCountMask) < kMaxCount) {
		fBuildingOp |= op;
		fBuildingOp += 3;
		fOps.back() = fBuildingOp;
	} else {
		fBuildingOp = op + 3;
		fOps.push_back(fBuildingOp);
	}

	fPoints.push_back({rx, ry});
	fPoints.push_back({angle, 0});
	fPoints.push_back(point);
}


void
BShape::Close()
{
	// If the last op is Close or MoveTo, ignore this
	if (fBuildingOp == kOpClose || fBuildingOp == kOpMoveTo)
		return;

	fBuildingOp = kOpClose;
	fOps.push_back(fBuildingOp);
}


gfx::Path
BShape::ToPath() const
{
	// Haiku's Painter::_IterateShapeData()
	struct Builder : BShapeIterator {
		gfx::Path path;
		gfx::Point current = {0, 0};
		gfx::Point contourStart = {0, 0};

		void IterateMoveTo(gfx::Point point) override
		{
			path.MoveTo(point);
			current = contourStart = point;
		}

		void IterateLineTo(std::span<const gfx::Point> points) override
		{
			for (const gfx::Point& point : points)
				path.LineTo(point);
			if (!points.empty())
				current = points.back();
		}

		void IterateBezierTo(std::span<const gfx::Point> points) override
		{
			for (std::size_t i = 0; i + 2 < points.size(); i += 3)
				path.CubicTo(points[i], points[i + 1], points[i + 2]);
			if (points.size() >= 3)
				current = points[points.size() / 3 * 3 - 1];
		}

		void IterateClose() override
		{
			path.Close();
			current = contourStart;
		}

		void IterateArcTo(float rx, float ry, float angle, bool largeArc,
			bool counterClockWise, gfx::Point point) override
		{
			// AGG's arc_to() without a point before it moves to the end
			if (path.IsEmpty()) {
				path.MoveTo(point);
				current = contourStart = point;
				return;
			}
			AddArc(path, current, rx, ry, angle, largeArc, !counterClockWise, point);
			current = point;
		}
	} builder;
	builder.Iterate(*this);
	return std::move(builder.path);
}


} // namespace ngui
