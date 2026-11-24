/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Path.h>

#include <algorithm>
#include <cmath>


namespace ngui::gfx {


namespace {


constexpr int kMaxSegmentsPerCurve = 1024;


// Points needed after \a verb.
std::size_t
PointsOf(PathVerb verb)
{
	switch (verb) {
		case PathVerb::Move:
		case PathVerb::Line:
			return 1;
		case PathVerb::Cubic:
			return 3;
		case PathVerb::Close:
			return 0;
	}
	return 0;
}


float
Length(Point point)
{
	return std::hypot(point.x, point.y);
}


} // namespace


void
Path::MoveTo(Point point)
{
	// a Move after a Move replaces it
	if (!fVerbs.empty() && fVerbs.back() == PathVerb::Move) {
		fPoints.back() = point;
	} else {
		fVerbs.push_back(PathVerb::Move);
		fPoints.push_back(point);
	}
	fContourStart = point;
	fOpen = true;
}


void
Path::LineTo(Point point)
{
	_EnsureContour();
	fVerbs.push_back(PathVerb::Line);
	fPoints.push_back(point);
}


void
Path::CubicTo(Point control1, Point control2, Point end)
{
	_EnsureContour();
	fVerbs.push_back(PathVerb::Cubic);
	fPoints.push_back(control1);
	fPoints.push_back(control2);
	fPoints.push_back(end);
}


void
Path::Close()
{
	if (!fOpen)
		return;
	fVerbs.push_back(PathVerb::Close);
	fOpen = false;
}


void
Path::Clear()
{
	fVerbs.clear();
	fPoints.clear();
	fContourStart = {0, 0};
	fOpen = false;
}


void
Path::SetFillRule(FillRule rule)
{
	fRule = rule;
}


FillRule
Path::Rule() const
{
	return fRule;
}


bool
Path::IsEmpty() const
{
	return fVerbs.empty();
}


bool
Path::IsFinite() const
{
	return std::ranges::all_of(fPoints, [](const Point& point) { return point.IsFinite(); });
}


std::span<const PathVerb>
Path::Verbs() const
{
	return fVerbs;
}


std::span<const Point>
Path::Points() const
{
	return fPoints;
}


Rect
Path::Bounds() const
{
	if (fPoints.empty())
		return {0, 0, 0, 0};
	Rect bounds{fPoints[0].x, fPoints[0].y, fPoints[0].x, fPoints[0].y};
	for (const Point& point : fPoints) {
		bounds.x0 = std::min(bounds.x0, point.x);
		bounds.y0 = std::min(bounds.y0, point.y);
		bounds.x1 = std::max(bounds.x1, point.x);
		bounds.y1 = std::max(bounds.y1, point.y);
	}
	return bounds;
}


void
Path::Translate(float dx, float dy)
{
	for (Point& point : fPoints) {
		point.x += dx;
		point.y += dy;
	}
	fContourStart.x += dx;
	fContourStart.y += dy;
}


std::vector<Path::Polyline>
Path::Flatten(float tolerance) const
{
	if (!(tolerance > 0))
		tolerance = 0.25f;

	std::vector<Polyline> contours;
	Polyline current{{}, false};
	const auto finish = [&] {
		if (current.points.size() >= 2)
			contours.push_back(std::move(current));
		current = {{}, false};
	};

	std::size_t next = 0;
	for (const PathVerb verb : fVerbs) {
		switch (verb) {
			case PathVerb::Move:
				finish();
				current.points.push_back(fPoints[next++]);
				break;
			case PathVerb::Line:
				current.points.push_back(fPoints[next++]);
				break;
			case PathVerb::Cubic:
			{
				const Point p0 = current.points.back();
				const Point p1 = fPoints[next];
				const Point p2 = fPoints[next + 1];
				const Point p3 = fPoints[next + 2];
				next += 3;
				// Wang's formula: segments for the given flatness
				const float d = std::max(Length(p0 - p1 - p1 + p2), Length(p1 - p2 - p2 + p3));
				const float wanted = std::ceil(std::sqrt(0.75f * d / tolerance));
				const int segments = std::isfinite(wanted)
					? std::clamp(static_cast<int>(wanted), 1, kMaxSegmentsPerCurve)
					: kMaxSegmentsPerCurve;
				for (int i = 1; i <= segments; i++) {
					const float t = static_cast<float>(i) / static_cast<float>(segments);
					const float u = 1 - t;
					const float a = u * u * u;
					const float b = 3 * u * u * t;
					const float c = 3 * u * t * t;
					const float e = t * t * t;
					current.points.push_back({a * p0.x + b * p1.x + c * p2.x + e * p3.x,
						a * p0.y + b * p1.y + c * p2.y + e * p3.y});
				}
				break;
			}
			case PathVerb::Close:
			{
				current.closed = true;
				const Point start = current.points.front();
				finish();
				// a following Line starts here
				current.points.push_back(start);
				break;
			}
		}
	}
	finish();
	return contours;
}


bool
Path::IsValidSequence(std::span<const PathVerb> verbs, std::size_t pointCount)
{
	if (!verbs.empty() && verbs.front() != PathVerb::Move)
		return false;
	std::size_t needed = 0;
	for (const PathVerb verb : verbs) {
		if (static_cast<std::uint8_t>(verb) > static_cast<std::uint8_t>(PathVerb::Close))
			return false;
		needed += PointsOf(verb);
	}
	return needed == pointCount;
}


void
Path::_EnsureContour()
{
	if (fOpen)
		return;
	// start a contour where the last one started (Skia's injected moveTo)
	fVerbs.push_back(PathVerb::Move);
	fPoints.push_back(fContourStart);
	fOpen = true;
}


} // namespace ngui::gfx
