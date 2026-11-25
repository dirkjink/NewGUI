/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "CompatView.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include <ngui/gfx/ImageRegistry.h>


namespace ngui::compat {


CompatView::CompatView(DrawContext& context)
	:
	fContext(context),
	// BView's defaults: black on white, pen size 1, B_OP_COPY, butt caps
	// and miter joins.
	fState{{0, 0, 0, 255}, {255, 255, 255, 255}, 1.0f, gfx::Paint::Cap::Butt,
		gfx::Paint::Join::Miter, B_OP_COPY, BPoint(0, 0), BFont(context.text)}
{
}


CompatView::~CompatView()
{
	// Every PushState() without a PopState() still has its Save() open.
	while (fCanvasSaves > 0) {
		fContext.canvas.Restore();
		fCanvasSaves--;
	}
}


void
CompatView::SetHighColor(rgb_color color)
{
	fState.high = color;
}


void
CompatView::SetHighColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	fState.high = {r, g, b, a};
}


rgb_color
CompatView::HighColor() const
{
	return fState.high;
}


void
CompatView::SetLowColor(rgb_color color)
{
	fState.low = color;
}


void
CompatView::SetLowColor(uint8 r, uint8 g, uint8 b, uint8 a)
{
	fState.low = {r, g, b, a};
}


rgb_color
CompatView::LowColor() const
{
	return fState.low;
}


rgb_color
CompatView::UiColor(color_which which) const
{
	return ToRgbColor(fContext.palette.Get(which));
}


void
CompatView::PushState()
{
	// Save()/Restore() for translation and clip, plus the layer's own state
	// (specification 17.2).
	fStack.push_back(fState);
	fContext.canvas.Save();
	fCanvasSaves++;
}


void
CompatView::PopState()
{
	if (fStack.empty())
		return;
	fState = fStack.back();
	fStack.pop_back();
	fContext.canvas.Restore();
	fCanvasSaves--;
}


void
CompatView::SetPenSize(float size)
{
	fState.penSize = size;
}


float
CompatView::PenSize() const
{
	return fState.penSize;
}


void
CompatView::SetLineMode(cap_mode cap, join_mode join, float)
{
	fState.cap = cap == B_ROUND_CAP ? gfx::Paint::Cap::Round
		: cap == B_SQUARE_CAP ? gfx::Paint::Cap::Square : gfx::Paint::Cap::Butt;
	fState.join = join == B_ROUND_JOIN ? gfx::Paint::Join::Round
		: join == B_BEVEL_JOIN ? gfx::Paint::Join::Bevel : gfx::Paint::Join::Miter;
}


void
CompatView::SetDrawingMode(drawing_mode mode)
{
	// Everything is SrcOver (specification 4.3); B_OP_INVERT has no
	// counterpart in the core profile (16).
#ifndef NDEBUG
	if (mode == B_OP_INVERT)
		std::fprintf(stderr, "ngui::compat::CompatView: B_OP_INVERT is not supported\n");
#endif
	fState.mode = mode;
}


drawing_mode
CompatView::DrawingMode() const
{
	return fState.mode;
}


void
CompatView::MovePenTo(BPoint point)
{
	fState.pen = point;
}


BPoint
CompatView::PenLocation() const
{
	return fState.pen;
}


void
CompatView::SetFont(const BFont* font)
{
	if (font != nullptr)
		fState.font = *font;
}


void
CompatView::GetFont(BFont* font) const
{
	if (font != nullptr)
		*font = fState.font;
}


uint32
CompatView::Flags() const
{
	return fFlags;
}


void
CompatView::SetFlags(uint32 flags)
{
	fFlags = flags;
}


BPoint
CompatView::_Align(BPoint point) const
{
	// Haiku truncates in screen coordinates, (int) of positive values; with
	// a translation by whole pixels that is floor() in view coordinates.
	if ((fFlags & B_SUBPIXEL_PRECISE) != 0)
		return point;
	return BPoint(std::floor(point.x), std::floor(point.y));
}


BRect
CompatView::_Align(BRect rect) const
{
	if ((fFlags & B_SUBPIXEL_PRECISE) != 0)
		return rect;
	return BRect(std::floor(rect.left), std::floor(rect.top), std::floor(rect.right),
		std::floor(rect.bottom));
}


void
CompatView::ClipToRect(BRect rect)
{
	fContext.canvas.ClipRect(FromBRect(rect));
}


void
CompatView::ClipToInverseRect(BRect rect)
{
	fContext.canvas.ClipOutRect(FromBRect(rect));
}


gfx::Paint
CompatView::_Paint(pattern_kind pattern) const
{
	gfx::Paint paint;
	paint.color = ToColor(pattern == B_SOLID_LOW ? fState.low : fState.high);
	paint.strokeWidth = fState.penSize;
	paint.cap = fState.cap;
	paint.join = fState.join;
	return paint;
}


void
CompatView::FillRect(BRect rect, pattern_kind pattern)
{
	fContext.canvas.FillRect(FromBRect(_Align(rect)), _Paint(pattern));
}


void
CompatView::StrokeRect(BRect rect, pattern_kind pattern)
{
	fContext.canvas.StrokeRect(FromBRect(_Align(rect)), _Paint(pattern));
}


void
CompatView::StrokeLine(BPoint start, BPoint end, pattern_kind pattern)
{
	// A line through the pixel centers with square caps covers the end
	// pixels as Haiku does (specification 17.2, rule A3).
	const gfx::Paint paint = _Paint(pattern);
	const std::array<gfx::Line, 1> line = {{{PixelCenter(_Align(start)),
		PixelCenter(_Align(end)), paint.color}}};
	fContext.canvas.DrawLines(line, fState.penSize, gfx::Paint::Cap::Square);
	fState.pen = end;
}


void
CompatView::StrokeLine(BPoint end, pattern_kind pattern)
{
	StrokeLine(fState.pen, end, pattern);
}


void
CompatView::BeginLineArray(int32 count)
{
	fLineArray.clear();
	if (count > 0)
		fLineArray.reserve(static_cast<std::size_t>(count));
	fInLineArray = true;
}


void
CompatView::AddLine(BPoint start, BPoint end, rgb_color color)
{
	if (!fInLineArray)
		return;
	fLineArray.push_back({PixelCenter(_Align(start)), PixelCenter(_Align(end)),
		ToColor(color)});
}


void
CompatView::EndLineArray()
{
	// The collected lines as one DrawLines(…, 1, Cap::Square)
	// (specification 17.2).
	if (fInLineArray && !fLineArray.empty())
		fContext.canvas.DrawLines(fLineArray, 1, gfx::Paint::Cap::Square);
	fLineArray.clear();
	fInLineArray = false;
}


void
CompatView::FillTriangle(BPoint p1, BPoint p2, BPoint p3, pattern_kind pattern)
{
	const std::array<gfx::Point, 3> points = {PixelCenter(_Align(p1)),
		PixelCenter(_Align(p2)), PixelCenter(_Align(p3))};
	fContext.canvas.FillConvexPolygon(points, _Paint(pattern));
}


void
CompatView::StrokeTriangle(BPoint p1, BPoint p2, BPoint p3, pattern_kind pattern)
{
	const std::array<gfx::Point, 3> points = {PixelCenter(_Align(p1)),
		PixelCenter(_Align(p2)), PixelCenter(_Align(p3))};
	fContext.canvas.StrokePolyline(points, true, _Paint(pattern));
}


void
CompatView::FillEllipse(BRect rect, pattern_kind pattern)
{
	fContext.canvas.FillEllipse(FromBRect(rect), _Paint(pattern));
}


void
CompatView::StrokeEllipse(BRect rect, pattern_kind pattern)
{
	fContext.canvas.StrokeArc(FromBRect(rect), 0, 360, _Paint(pattern));
}


void
CompatView::StrokeArc(BRect rect, float startAngle, float arcAngle, pattern_kind pattern)
{
	// Haiku's angles run counterclockwise, the Canvas's clockwise
	// (specification 17.2).
	fContext.canvas.StrokeArc(FromBRect(rect), -startAngle, -arcAngle, _Paint(pattern));
}


void
CompatView::FillRegion(const BRegion* region, pattern_kind pattern)
{
	if (region == nullptr)
		return;
	const gfx::Paint paint = _Paint(pattern);
	const gfx::Region& rects = region->Region();
	for (int32 i = 0; i < rects.CountRects(); i++)
		fContext.canvas.FillRect(rects.RectAt(i).ToRect(), paint);
}


void
CompatView::DrawString(const char* string, BPoint where)
{
	if (string == nullptr || string[0] == '\0')
		return;
	// Shaped by the text subsystem, scaled to the size of the current font,
	// drawn at the baseline point \a where (specification 17.2).
	ShapedText shaped = fState.font.Text().Shape(string, fState.font.Role());
	const float scale = fState.font.Scale();
	shaped.sizePx *= scale;
	for (gfx::Point& position : shaped.positions) {
		position.x *= scale;
		position.y *= scale;
	}
	shaped.Draw(fContext.canvas, {where.x, where.y}, _Paint(B_SOLID_HIGH));
	fState.pen = BPoint(where.x + shaped.advance * scale, where.y);
}


float
CompatView::StringWidth(const char* string) const
{
	return fState.font.StringWidth(string);
}


void
CompatView::DrawBitmap(gfx::ImageHandle bitmap, BPoint where)
{
	// The whole bitmap with its left top corner at \a where.
	const std::optional<gfx::Size> size = gfx::ImageRegistry::Default().PixelSize(bitmap);
	if (!size.has_value())
		return;
	fContext.canvas.DrawImage(bitmap, {0, 0, size->w, size->h},
		{where.x, where.y, where.x + size->w, where.y + size->h});
}


void
CompatView::DrawBitmap(const BBitmap* bitmap, BPoint where)
{
	if (bitmap != nullptr)
		DrawBitmap(bitmap->Handle(), where);
}


} // namespace ngui::compat
