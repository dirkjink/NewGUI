/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "DisplayListBuilder.h"

#include <algorithm>
#include <cstring>
#include <utility>


namespace ngui::gfx {


namespace {


constexpr std::size_t
_Padded(std::size_t size)
{
	return (size + 3) & ~std::size_t(3);
}


template<typename T>
void
_AddUnique(std::vector<T>& list, const T& value)
{
	if (std::ranges::find(list, value) == list.end())
		list.push_back(value);
}


} // namespace


PaintRecord
PaintRecord::From(const Paint& paint)
{
	// Value-initialized, so that everything not set below, including the
	// unused gradient stops, is zero and the binary form is canonical.
	PaintRecord record = {};
	record.color = paint.color;
	record.strokeWidth = paint.strokeWidth;
	record.cap = static_cast<std::uint8_t>(paint.cap);
	record.join = static_cast<std::uint8_t>(paint.join);
	record.antialias = paint.antialias ? 1 : 0;
	if (paint.gradient.has_value()) {
		const LinearGradient& gradient = *paint.gradient;
		record.hasGradient = 1;
		record.gradient.p0 = gradient.p0;
		record.gradient.p1 = gradient.p1;
		record.gradient.count = gradient.count;
		const std::span<const GradientStop> stops = gradient.Stops();
		std::ranges::copy(stops, record.gradient.stops);
	}
	return record;
}


Paint
PaintRecord::ToPaint() const
{
	Paint paint;
	paint.color = color;
	paint.strokeWidth = strokeWidth;
	paint.cap = static_cast<Paint::Cap>(cap);
	paint.join = static_cast<Paint::Join>(join);
	paint.antialias = antialias != 0;
	if (hasGradient != 0) {
		LinearGradient result = {gradient.p0, gradient.p1, {},
			static_cast<std::uint8_t>(std::min<std::uint32_t>(gradient.count, 255))};
		std::copy_n(gradient.stops, 4, result.stops.begin());
		paint.gradient = result;
	}
	return paint;
}


DisplayListBuilder::DisplayListBuilder(const Rect& bounds)
	:
	fList(new DisplayList())
{
	fList->fBounds = bounds;
	fList->fClipStarts.push_back(0);
}


std::uint32_t
DisplayListBuilder::AddClip(const Region& clip)
{
	std::vector<IntRect> rects;
	rects.reserve(static_cast<std::size_t>(clip.CountRects()));
	for (int32 i = 0; i < clip.CountRects(); i++)
		rects.push_back(clip.RectAt(i));

	const std::size_t count = fList->fClipStarts.size() - 1;
	if (count > 0) {
		const std::span<const IntRect> previous
			= fList->Clip(static_cast<std::uint32_t>(count - 1));
		if (std::ranges::equal(previous, rects))
			return static_cast<std::uint32_t>(count - 1);
	}

	fList->fClipRects.insert(fList->fClipRects.end(), rects.begin(), rects.end());
	fList->fClipStarts.push_back(static_cast<std::uint32_t>(fList->fClipRects.size()));
	return static_cast<std::uint32_t>(count);
}


void
DisplayListBuilder::FillRect(std::uint32_t clip, const Rect& rect, const Paint& paint)
{
	_BeginOp(OpCode::FillRect, clip, RectRecord{rect, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::FillRRect(std::uint32_t clip, const RRect& rrect, const Paint& paint)
{
	_BeginOp(OpCode::FillRRect, clip, RRectRecord{rrect, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::FillRing(std::uint32_t clip, const RRect& outer, const RRect& inner,
	const Paint& paint)
{
	_BeginOp(OpCode::FillRing, clip, RingRecord{outer, inner, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::FillEllipse(std::uint32_t clip, const Rect& rect, const Paint& paint)
{
	_BeginOp(OpCode::FillEllipse, clip, RectRecord{rect, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::FillConvexPolygon(std::uint32_t clip, std::span<const Point> points,
	const Paint& paint)
{
	const PointsRecord record = {PaintRecord::From(paint),
		static_cast<std::uint32_t>(points.size()), 0};
	const std::size_t offset = _BeginOp(OpCode::FillConvexPolygon, clip, record,
		points.size_bytes());
	_Write(offset, points.data(), points.size_bytes());
}


void
DisplayListBuilder::StrokeRect(std::uint32_t clip, const Rect& rect, const Paint& paint)
{
	_BeginOp(OpCode::StrokeRect, clip, RectRecord{rect, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::StrokeRRect(std::uint32_t clip, const RRect& rrect, const Paint& paint)
{
	_BeginOp(OpCode::StrokeRRect, clip, RRectRecord{rrect, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::StrokePolyline(std::uint32_t clip, std::span<const Point> points,
	bool closed, const Paint& paint)
{
	const PointsRecord record = {PaintRecord::From(paint),
		static_cast<std::uint32_t>(points.size()), closed ? 1u : 0u};
	const std::size_t offset = _BeginOp(OpCode::StrokePolyline, clip, record,
		points.size_bytes());
	_Write(offset, points.data(), points.size_bytes());
}


void
DisplayListBuilder::StrokeArc(std::uint32_t clip, const Rect& oval, float startDeg,
	float sweepDeg, const Paint& paint)
{
	_BeginOp(OpCode::StrokeArc, clip,
		ArcRecord{oval, startDeg, sweepDeg, PaintRecord::From(paint)}, 0);
}


void
DisplayListBuilder::DrawLines(std::uint32_t clip, std::span<const Line> lines, float width,
	Paint::Cap cap)
{
	const LinesRecord record = {width, static_cast<std::uint8_t>(cap), {0, 0, 0},
		static_cast<std::uint32_t>(lines.size())};
	const std::size_t offset = _BeginOp(OpCode::DrawLines, clip, record, lines.size_bytes());
	_Write(offset, lines.data(), lines.size_bytes());
}


void
DisplayListBuilder::DrawImage(std::uint32_t clip, ImageHandle image, const Rect& src,
	const Rect& dst, float alpha)
{
	_BeginOp(OpCode::DrawImage, clip, ImageRecord{image, alpha, src, dst}, 0);
	_AddUnique(fList->fImages, image);
}


void
DisplayListBuilder::DrawGlyphRun(std::uint32_t clip, const GlyphRun& run, Point origin,
	const Color& color)
{
	const GlyphRunRecord record = {run.font, run.sizePx, origin, color,
		static_cast<std::uint32_t>(run.glyphs.size()),
		static_cast<std::uint32_t>(run.clusters.size()),
		static_cast<std::uint32_t>(run.text.size())};
	const std::size_t trailing = run.glyphs.size_bytes() + run.positions.size_bytes()
		+ run.clusters.size_bytes() + _Padded(run.text.size());
	std::size_t offset = _BeginOp(OpCode::DrawGlyphRun, clip, record, trailing);
	_Write(offset, run.glyphs.data(), run.glyphs.size_bytes());
	offset += run.glyphs.size_bytes();
	_Write(offset, run.positions.data(), run.positions.size_bytes());
	offset += run.positions.size_bytes();
	_Write(offset, run.clusters.data(), run.clusters.size_bytes());
	offset += run.clusters.size_bytes();
	_Write(offset, run.text.data(), run.text.size());
	_AddUnique(fList->fFonts, run.font);
}


void
DisplayListBuilder::DrawList(std::uint32_t clip, const DisplayListRef& child, Point offset)
{
	std::vector<DisplayListRef>& children = fList->fChildren;
	auto found = std::ranges::find(children, child);
	if (found == children.end())
		found = children.insert(children.end(), child);
	const auto index = static_cast<std::uint32_t>(found - children.begin());
	_BeginOp(OpCode::DrawList, clip, ListRecord{index, offset}, 0);
}


DisplayListRef
DisplayListBuilder::Finish()
{
	fList->_RetainImages();
	DisplayListRef result(std::move(fList));
	fList.reset(new DisplayList());
	fList->fClipStarts.push_back(0);
	return result;
}


template<typename Record>
std::size_t
DisplayListBuilder::_BeginOp(OpCode code, std::uint32_t clip, const Record& record,
	std::size_t trailingBytes)
{
	const std::size_t payload = sizeof(Record) + _Padded(trailingBytes);
	const std::uint16_t opcode = static_cast<std::uint16_t>(code);
	const std::uint16_t flags = 0;
	const auto size = static_cast<std::uint32_t>(payload);

	const std::size_t start = fList->fOps.size();
	// Zero-filled, so that the padding of the trailing data is zero.
	fList->fOps.resize(start + kOpHeaderSize + payload);
	_Write(start, &opcode, sizeof(opcode));
	_Write(start + 2, &flags, sizeof(flags));
	_Write(start + 4, &size, sizeof(size));
	_Write(start + 8, &clip, sizeof(clip));
	_Write(start + kOpHeaderSize, &record, sizeof(record));
	fList->fOpCount++;
	return start + kOpHeaderSize + sizeof(Record);
}


void
DisplayListBuilder::_Write(std::size_t offset, const void* data, std::size_t size)
{
	if (size > 0)
		std::memcpy(fList->fOps.data() + offset, data, size);
}


} // namespace ngui::gfx
