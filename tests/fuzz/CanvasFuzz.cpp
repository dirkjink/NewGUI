/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/IntRect.h>


namespace {


using namespace ngui::gfx;


// Reads values from the input; missing bytes count as zero.
class Reader {
public:
								Reader(std::span<const std::uint8_t> data);

			bool				AtEnd() const;
			std::uint8_t		Byte();
			float				Float();
			Point				NextPoint();
			Rect				NextRect();
			Color				NextColor();
			Paint				NextPaint();

private:
			std::span<const std::uint8_t> fData;
};


Reader::Reader(std::span<const std::uint8_t> data)
	:
	fData(data)
{
}


bool
Reader::AtEnd() const
{
	return fData.empty();
}


std::uint8_t
Reader::Byte()
{
	if (fData.empty())
		return 0;
	const std::uint8_t value = fData.front();
	fData = fData.subspan(1);
	return value;
}


float
Reader::Float()
{
	// Mostly small whole numbers, so that clips and rectangles overlap; now
	// and then raw bits, for NaN, infinity and extreme values.
	const std::uint8_t kind = Byte();
	if (kind < 200)
		return static_cast<float>(static_cast<int>(Byte()) - 64) / ((kind % 4) + 1);
	std::uint8_t bytes[sizeof(float)] = {Byte(), Byte(), Byte(), Byte()};
	float value;
	std::memcpy(&value, bytes, sizeof(value));
	return value;
}


Point
Reader::NextPoint()
{
	const float x = Float();
	const float y = Float();
	return {x, y};
}


Rect
Reader::NextRect()
{
	const float x0 = Float();
	const float y0 = Float();
	const float x1 = Float();
	const float y1 = Float();
	return {x0, y0, x1, y1};
}


Color
Reader::NextColor()
{
	// Byte / 200 also gives values above 1, which are invalid on purpose.
	const float r = Byte() / 200.0f;
	const float g = Byte() / 200.0f;
	const float b = Byte() / 200.0f;
	const float a = Byte() / 200.0f;
	return {r, g, b, a};
}


Paint
Reader::NextPaint()
{
	Paint paint;
	paint.color = NextColor();
	paint.strokeWidth = Float();
	paint.cap = static_cast<Paint::Cap>(Byte() % 3);
	paint.join = static_cast<Paint::Join>(Byte() % 3);
	paint.antialias = Byte() % 2 == 0;
	if (Byte() % 4 == 0) {
		const Point p0 = NextPoint();
		const Point p1 = NextPoint();
		const std::array<GradientStop, 2> stops = {{{0, NextColor()}, {1, NextColor()}}};
		const auto gradient = LinearGradient::Create(p0, p1, stops);
		if (gradient.has_value())
			paint.gradient = *gradient;
	}
	return paint;
}


bool
WithinBounds(const DisplayList& list)
{
	const IntRect bounds = IntRect::RoundedFrom(list.Bounds());
	for (std::uint32_t i = 0; i < list.ClipCount(); i++) {
		for (const IntRect& rect : list.Clip(i)) {
			if (rect.x0 < bounds.x0 || rect.y0 < bounds.y0 || rect.x1 > bounds.x1
				|| rect.y1 > bounds.y1) {
				return false;
			}
		}
	}
	return true;
}


} // namespace


NGUI_FUZZ_TARGET(Canvas)
{
	// What is tested: Arbitrary sequences of Canvas calls (state, clip and
	// every drawing operation) with arbitrary arguments, NaN, infinity and
	// extreme values included.
	// Success criteria: No call crashes (checked by the sanitizers); the
	// finished list is accepted by DisplayList::Deserialize() and serializes
	// back to the same bytes; every clip lies inside the rounded bounds.
	// Why it matters: The Canvas is where specification 6.2 is enforced; if
	// it recorded anything Deserialize() rejects, a list would not survive
	// its own binary form, and backends would see input no check covered.
	//
	// Given
	Reader reader(data);
	Canvas canvas(reader.NextRect());
	std::vector<Point> points;
	std::vector<Line> lines;
	const std::array<std::uint32_t, 3> glyphs = {1, 2, 3};
	const std::array<std::uint32_t, 3> clusters = {0, 1, 1};
	Canvas childCanvas({0, 0, 8, 8});
	childCanvas.FillRect({0, 0, 8, 8}, Paint{});
	const DisplayListRef child = childCanvas.Finish();
	// Restore() without a Save() asserts in debug builds, and the regression
	// run also happens in debug builds; CanvasTest covers that case.
	int saved = 0;

	// When
	for (int step = 0; step < 48 && !reader.AtEnd(); step++) {
		switch (reader.Byte() % 20) {
			case 0:
				canvas.Save();
				saved++;
				break;
			case 1:
				if (saved > 0) {
					canvas.Restore();
					saved--;
				}
				break;
			case 2:
			{
				const Point delta = reader.NextPoint();
				canvas.Translate(delta.x, delta.y);
				break;
			}
			case 3:
				canvas.ClipRect(reader.NextRect());
				break;
			case 4:
				canvas.ClipOutRect(reader.NextRect());
				break;
			case 5:
				canvas.FillRect(reader.NextRect(), reader.NextPaint());
				break;
			case 6:
			{
				const Rect rect = reader.NextRect();
				const Radii radii = {reader.Float(), reader.Float(), reader.Float(),
					reader.Float()};
				canvas.FillRRect({rect, radii}, reader.NextPaint());
				break;
			}
			case 7:
			{
				const Rect outer = reader.NextRect();
				const Rect inner = reader.NextRect();
				canvas.FillRing({outer, Radii::Uniform(reader.Float())},
					{inner, Radii::Uniform(reader.Float())}, reader.NextPaint());
				break;
			}
			case 8:
				canvas.FillEllipse(reader.NextRect(), reader.NextPaint());
				break;
			case 9:
			case 10:
			{
				points.resize(reader.Byte() % 6);
				for (Point& point : points)
					point = reader.NextPoint();
				if (step % 2 == 0)
					canvas.FillConvexPolygon(points, reader.NextPaint());
				else
					canvas.StrokePolyline(points, reader.Byte() % 2 == 0, reader.NextPaint());
				break;
			}
			case 11:
				canvas.StrokeRect(reader.NextRect(), reader.NextPaint());
				break;
			case 12:
			{
				const Rect rect = reader.NextRect();
				canvas.StrokeRRect({rect, Radii::Uniform(reader.Float())}, reader.NextPaint());
				break;
			}
			case 13:
			{
				const Rect oval = reader.NextRect();
				const float start = reader.Float();
				const float sweep = reader.Float();
				canvas.StrokeArc(oval, start, sweep, reader.NextPaint());
				break;
			}
			case 14:
			{
				lines.resize(reader.Byte() % 4);
				for (Line& line : lines) {
					line.p0 = reader.NextPoint();
					line.p1 = reader.NextPoint();
					line.color = reader.NextColor();
				}
				const float width = reader.Float();
				canvas.DrawLines(lines, width, static_cast<Paint::Cap>(reader.Byte() % 4));
				break;
			}
			case 15:
			{
				const ImageHandle image = reader.Byte() % 3;
				const Rect src = reader.NextRect();
				const Rect dst = reader.NextRect();
				canvas.DrawImage(image, src, dst, reader.Byte() / 200.0f);
				break;
			}
			case 16:
			{
				points.resize(3);
				for (Point& point : points)
					point = reader.NextPoint();
				const std::size_t count = reader.Byte() % 4;
				const bool withText = reader.Byte() % 2 == 0;
				const GlyphRun run = {reader.Byte(), reader.Float(),
					std::span(glyphs).first(count), std::span(points).first(count),
					withText ? std::u8string_view(u8"ab") : std::u8string_view(),
					withText ? std::span(clusters).first(count) : std::span<const std::uint32_t>()};
				const Point origin = reader.NextPoint();
				canvas.DrawGlyphRun(run, origin, reader.NextPaint());
				break;
			}
			case 17:
				canvas.DrawList(reader.Byte() % 2 == 0 ? child : nullptr, reader.NextPoint());
				break;
			case 18:
				canvas.SetRecordGlyphText(reader.Byte() % 2 == 0);
				break;
			case 19:
				canvas.Save();
				saved++;
				canvas.ClipRect(reader.NextRect());
				break;
		}
	}
	const DisplayListRef list = canvas.Finish();

	// Then
	NGUI_FUZZ_CHECK(list != nullptr);
	NGUI_FUZZ_CHECK(WithinBounds(*list));
	const std::vector<std::byte> bytes = list->Serialize();
	const auto restored = DisplayList::Deserialize(bytes);
	NGUI_FUZZ_CHECK(restored.has_value());
	NGUI_FUZZ_CHECK(restored.has_value() && (*restored)->Serialize() == bytes);
}
