/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Canvas.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <utility>

#include <ngui/gfx/IntRect.h>

#include "DisplayListBuilder.h"


namespace ngui::gfx {


namespace {


bool
_IsFinite(const RRect& rrect)
{
	return rrect.rect.IsFinite() && std::isfinite(rrect.radii.tl)
		&& std::isfinite(rrect.radii.tr) && std::isfinite(rrect.radii.br)
		&& std::isfinite(rrect.radii.bl);
}


bool
_AllFinite(std::span<const Point> points)
{
	return std::ranges::all_of(points, [](const Point& point) { return point.IsFinite(); });
}


#ifndef NDEBUG
// Whether the polygon turns in one direction only; collinear points count
// as convex. Debug builds only (specification 6.1).
bool
_IsConvex(std::span<const Point> points)
{
	int sign = 0;
	const std::size_t count = points.size();
	for (std::size_t i = 0; i < count; i++) {
		const Point a = points[i];
		const Point b = points[(i + 1) % count];
		const Point c = points[(i + 2) % count];
		const double cross = (static_cast<double>(b.x) - a.x) * (static_cast<double>(c.y) - b.y)
			- (static_cast<double>(b.y) - a.y) * (static_cast<double>(c.x) - b.x);
		const int turn = cross > 0 ? 1 : (cross < 0 ? -1 : 0);
		if (turn != 0) {
			if (sign != 0 && turn != sign)
				return false;
			sign = turn;
		}
	}
	return true;
}
#endif


// What DisplayList::Deserialize() accepts for a glyph run (specification
// 10): one position per glyph, clusters empty or one per glyph, monotonic
// and inside the text.
bool
_IsValidRun(const GlyphRun& run)
{
	if (run.glyphs.empty() || run.positions.size() != run.glyphs.size()
		|| !std::isfinite(run.sizePx) || run.sizePx <= 0 || !_AllFinite(run.positions)) {
		return false;
	}
	if (run.clusters.empty())
		return true;
	if (run.clusters.size() != run.glyphs.size() || run.text.empty())
		return false;
	const bool ascending = std::ranges::is_sorted(run.clusters);
	const bool descending = std::ranges::is_sorted(run.clusters, std::greater<>());
	return (ascending || descending)
		&& std::ranges::all_of(run.clusters,
			[&](std::uint32_t cluster) { return cluster < run.text.size(); });
}


} // namespace


Canvas::Canvas(const Rect& bounds)
	:
	// Bounds that are not finite would make a list Deserialize() rejects;
	// such a Canvas records into nothing.
	fBounds(bounds.IsFinite() ? bounds : Rect{0, 0, 0, 0}),
	fBuilder(std::make_unique<DisplayListBuilder>(fBounds)),
	fState{{0, 0}, Region(IntRect::RoundedFrom(fBounds))}
{
}


Canvas::~Canvas() = default;


void
Canvas::Save()
{
	if (fBuilder == nullptr)
		return;
	fStack.push_back(fState);
}


void
Canvas::Restore()
{
	assert(!fStack.empty() && "Canvas::Restore() without a matching Save()");
	if (fBuilder == nullptr || fStack.empty())
		return;
	fState = std::move(fStack.back());
	fStack.pop_back();
}


void
Canvas::Translate(float dx, float dy)
{
	const Point moved = fState.translation + Point{dx, dy};
	if (!_Accept(moved.IsFinite(), "Translate"))
		return;
	fState.translation = moved;
}


void
Canvas::ClipRect(const Rect& rect)
{
	const Rect moved = _Moved(rect);
	if (fBuilder == nullptr || !_Accept(moved.IsFinite(), "ClipRect"))
		return;
	fState.clip.IntersectWith(Region(IntRect::RoundedFrom(moved)));
}


void
Canvas::ClipOutRect(const Rect& rect)
{
	const Rect moved = _Moved(rect);
	if (fBuilder == nullptr || !_Accept(moved.IsFinite(), "ClipOutRect"))
		return;
	fState.clip.Exclude(IntRect::RoundedFrom(moved));
}


void
Canvas::FillRect(const Rect& rect, const Paint& paint)
{
	const Rect moved = _Moved(rect);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw() || !_Accept(moved.IsFinite() && movedPaint.IsValid(), "FillRect")
		|| moved.IsEmpty()) {
		return;
	}
	fBuilder->FillRect(_Clip(), moved, movedPaint);
}


void
Canvas::FillRRect(const RRect& rrect, const Paint& paint)
{
	const RRect moved = _Moved(rrect);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw() || !_Accept(_IsFinite(moved) && movedPaint.IsValid(), "FillRRect")
		|| moved.rect.IsEmpty()) {
		return;
	}
	fBuilder->FillRRect(_Clip(), moved.Normalized(), movedPaint);
}


void
Canvas::FillRing(const RRect& outer, const RRect& inner, const Paint& paint)
{
	const RRect movedOuter = _Moved(outer);
	const RRect movedInner = _Moved(inner);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw()
		|| !_Accept(_IsFinite(movedOuter) && _IsFinite(movedInner) && movedPaint.IsValid(),
			"FillRing")
		|| movedOuter.rect.IsEmpty()) {
		return;
	}
	fBuilder->FillRing(_Clip(), movedOuter.Normalized(), movedInner.Normalized(), movedPaint);
}


void
Canvas::FillEllipse(const Rect& rect, const Paint& paint)
{
	const Rect moved = _Moved(rect);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw() || !_Accept(moved.IsFinite() && movedPaint.IsValid(), "FillEllipse")
		|| moved.IsEmpty()) {
		return;
	}
	fBuilder->FillEllipse(_Clip(), moved, movedPaint);
}


void
Canvas::FillConvexPolygon(std::span<const Point> points, const Paint& paint)
{
	if (!_CanDraw())
		return;
	fScratchPoints.clear();
	for (const Point& point : points)
		fScratchPoints.push_back(_Moved(point));
	const Paint movedPaint = _Moved(paint);
	if (!_Accept(points.size() >= 3 && _AllFinite(fScratchPoints) && movedPaint.IsValid(),
			"FillConvexPolygon")) {
		return;
	}
#ifndef NDEBUG
	if (!_IsConvex(fScratchPoints))
		std::fprintf(stderr, "ngui::gfx::Canvas: FillConvexPolygon with a non-convex polygon\n");
#endif
	fBuilder->FillConvexPolygon(_Clip(), fScratchPoints, movedPaint);
}


void
Canvas::StrokeRect(const Rect& rect, const Paint& paint)
{
	const Rect moved = _Moved(rect);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw() || !_Accept(moved.IsFinite() && movedPaint.IsValid(), "StrokeRect")
		|| moved.IsEmpty()) {
		return;
	}
	fBuilder->StrokeRect(_Clip(), moved, movedPaint);
}


void
Canvas::StrokeRRect(const RRect& rrect, const Paint& paint)
{
	const RRect moved = _Moved(rrect);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw() || !_Accept(_IsFinite(moved) && movedPaint.IsValid(), "StrokeRRect")
		|| moved.rect.IsEmpty()) {
		return;
	}
	fBuilder->StrokeRRect(_Clip(), moved.Normalized(), movedPaint);
}


void
Canvas::StrokePolyline(std::span<const Point> points, bool closed, const Paint& paint)
{
	if (!_CanDraw())
		return;
	fScratchPoints.clear();
	for (const Point& point : points)
		fScratchPoints.push_back(_Moved(point));
	const Paint movedPaint = _Moved(paint);
	if (!_Accept(points.size() >= 2 && _AllFinite(fScratchPoints) && movedPaint.IsValid(),
			"StrokePolyline")) {
		return;
	}
	fBuilder->StrokePolyline(_Clip(), fScratchPoints, closed, movedPaint);
}


void
Canvas::StrokeArc(const Rect& oval, float startDeg, float sweepDeg, const Paint& paint)
{
	const Rect moved = _Moved(oval);
	const Paint movedPaint = _Moved(paint);
	if (!_CanDraw()
		|| !_Accept(moved.IsFinite() && std::isfinite(startDeg) && std::isfinite(sweepDeg)
			&& movedPaint.IsValid(), "StrokeArc")
		|| moved.IsEmpty()) {
		return;
	}
	fBuilder->StrokeArc(_Clip(), moved, startDeg, sweepDeg, movedPaint);
}


void
Canvas::DrawLines(std::span<const Line> lines, float width, Paint::Cap cap)
{
	if (!_CanDraw())
		return;
	fScratchLines.clear();
	for (const Line& line : lines)
		fScratchLines.push_back({_Moved(line.p0), _Moved(line.p1), line.color});
	const bool valid = !lines.empty() && std::isfinite(width) && width >= 0
		&& cap <= Paint::Cap::Square
		&& std::ranges::all_of(fScratchLines, [](const Line& line) {
			return line.p0.IsFinite() && line.p1.IsFinite() && line.color.IsValid();
		});
	if (!_Accept(valid, "DrawLines"))
		return;
	fBuilder->DrawLines(_Clip(), fScratchLines, width, cap);
}


void
Canvas::DrawImage(ImageHandle image, const Rect& src, const Rect& dst, float alpha)
{
	const Rect moved = _Moved(dst);
	if (!_CanDraw()
		|| !_Accept(image != 0 && src.IsFinite() && moved.IsFinite() && alpha >= 0
			&& alpha <= 1, "DrawImage")
		|| moved.IsEmpty() || src.IsEmpty()) {
		return;
	}
	fBuilder->DrawImage(_Clip(), image, src, moved, alpha);
}


void
Canvas::DrawGlyphRun(const GlyphRun& run, Point origin, const Paint& paint)
{
	const Point moved = _Moved(origin);
	if (!_CanDraw()
		|| !_Accept(moved.IsFinite() && paint.color.IsValid() && _IsValidRun(run),
			"DrawGlyphRun")) {
		return;
	}
	GlyphRun recorded = run;
	if (!fRecordGlyphText) {
		recorded.text = {};
		recorded.clusters = {};
	}
	fBuilder->DrawGlyphRun(_Clip(), recorded, moved, paint.color);
}


void
Canvas::DrawList(const DisplayListRef& child, Point offset)
{
	const Point moved = _Moved(offset);
	if (!_CanDraw() || !_Accept(child != nullptr && moved.IsFinite(), "DrawList"))
		return;
	fBuilder->DrawList(_Clip(), child, moved);
}


void
Canvas::SetRecordGlyphText(bool record)
{
	fRecordGlyphText = record;
}


DisplayListRef
Canvas::Finish()
{
	if (fBuilder == nullptr)
		return nullptr;
	fStack.clear();
	DisplayListRef list = fBuilder->Finish();
	fBuilder.reset();
	return list;
}


std::uint32_t
Canvas::DiscardedOperations() const
{
	return fDiscarded;
}


bool
Canvas::_Accept(bool valid, const char* operation)
{
	if (valid)
		return true;
	fDiscarded++;
#ifndef NDEBUG
	std::fprintf(stderr, "ngui::gfx::Canvas: %s with invalid arguments dropped\n", operation);
#else
	(void)operation;
#endif
	return false;
}


bool
Canvas::_CanDraw() const
{
	// After Finish() nothing is recorded; under an empty clip nothing would
	// be visible (specification 7).
	return fBuilder != nullptr && !fState.clip.IsEmpty();
}


std::uint32_t
Canvas::_Clip()
{
	return fBuilder->AddClip(fState.clip);
}


Point
Canvas::_Moved(Point point) const
{
	return point + fState.translation;
}


Rect
Canvas::_Moved(const Rect& rect) const
{
	return rect.OffsetByCopy(fState.translation);
}


RRect
Canvas::_Moved(const RRect& rrect) const
{
	return {_Moved(rrect.rect), rrect.radii};
}


Paint
Canvas::_Moved(const Paint& paint) const
{
	Paint moved = paint;
	if (moved.gradient.has_value()) {
		moved.gradient->p0 = _Moved(moved.gradient->p0);
		moved.gradient->p1 = _Moved(moved.gradient->p1);
	}
	return moved;
}


} // namespace ngui::gfx
