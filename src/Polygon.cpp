/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/interface/Polygon.cpp (commit b1d497bb69).
// Differences: see Polygon.h. The frame is computed from the points after
// MapTo() (Haiku mapped its corners, which flipped it for a mirroring
// destination).

#include <ngui/Polygon.h>

#include <algorithm>


namespace ngui {


namespace {


// Limit to avoid integer overflow when calculating the size to allocate
constexpr std::size_t kMaxPointCount = 10000000;


float
MapCoordinate(float value, float sourceStart, float sourceSize, float destinationStart,
	float destinationSize)
{
	// NewGUI: an axis of no size maps to the destination's start
	if (sourceSize == 0)
		return destinationStart;
	return (value - sourceStart) * destinationSize / sourceSize + destinationStart;
}


} // namespace


BPolygon::BPolygon(std::span<const gfx::Point> points)
{
	AddPoints(points);
}


gfx::Rect
BPolygon::Frame() const
{
	if (fPoints.empty())
		return {0, 0, 0, 0};
	gfx::Rect bounds{fPoints[0].x, fPoints[0].y, fPoints[0].x, fPoints[0].y};
	for (const gfx::Point& point : fPoints) {
		bounds.x0 = std::min(bounds.x0, point.x);
		bounds.y0 = std::min(bounds.y0, point.y);
		bounds.x1 = std::max(bounds.x1, point.x);
		bounds.y1 = std::max(bounds.y1, point.y);
	}
	return bounds;
}


void
BPolygon::AddPoints(std::span<const gfx::Point> points)
{
	if (points.empty() || points.size() > kMaxPointCount
		|| fPoints.size() + points.size() > kMaxPointCount) {
		return;
	}
	fPoints.insert(fPoints.end(), points.begin(), points.end());
}


int32_t
BPolygon::CountPoints() const
{
	return static_cast<int32_t>(fPoints.size());
}


std::span<const gfx::Point>
BPolygon::Points() const
{
	return fPoints;
}


void
BPolygon::MapTo(gfx::Rect source, gfx::Rect destination)
{
	for (gfx::Point& point : fPoints) {
		point.x = MapCoordinate(point.x, source.x0, source.Width(), destination.x0,
			destination.Width());
		point.y = MapCoordinate(point.y, source.y0, source.Height(), destination.y0,
			destination.Height());
	}
}


gfx::Path
BPolygon::ToPath(bool closed) const
{
	gfx::Path path;
	if (fPoints.size() < 2)
		return path;
	path.MoveTo(fPoints[0]);
	for (std::size_t i = 1; i < fPoints.size(); i++)
		path.LineTo(fPoints[i]);
	if (closed)
		path.Close();
	return path;
}


} // namespace ngui
