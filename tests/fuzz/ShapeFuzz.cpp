/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include <ngui/Shape.h>
#include <ngui/gfx/Path.h>


namespace {


using namespace ngui;


// The bytes of a fuzz input, taken one after the other; zeros at the end.
class Bytes {
public:
	explicit Bytes(std::span<const std::uint8_t> data)
		:
		fData(data)
	{
	}

	bool AtEnd() const
	{
		return fNext >= fData.size();
	}

	std::uint8_t Next()
	{
		return fNext < fData.size() ? fData[fNext++] : 0;
	}

	// A float from four bytes: any value, NaN and infinity included.
	float Float()
	{
		std::uint32_t bits = 0;
		for (int i = 0; i < 4; i++)
			bits = (bits << 8) | Next();
		float value = 0;
		std::memcpy(&value, &bits, sizeof value);
		return value;
	}

	// A coordinate of -1000 .. 1000 in steps of 1/16.
	float Small()
	{
		const auto high = static_cast<std::uint32_t>(Next());
		const std::uint32_t value = (high << 8) | Next();
		return static_cast<float>(static_cast<int>(value % 32001) - 16000) / 16.0f;
	}

	gfx::Point Point(bool wild)
	{
		if (wild) {
			const float x = Float();
			return {x, Float()};
		}
		const float x = Small();
		return {x, Small()};
	}

private:
	std::span<const std::uint8_t> fData;
	std::size_t fNext = 0;
};


// Counts the points the iterator hands out.
class CountingIterator : public BShapeIterator {
public:
	std::size_t points = 0;

	void IterateMoveTo(gfx::Point) override { points++; }
	void IterateLineTo(std::span<const gfx::Point> line) override { points += line.size(); }
	void IterateBezierTo(std::span<const gfx::Point> curves) override
	{
		NGUI_FUZZ_CHECK(curves.size() % 3 == 0);
		points += curves.size();
	}
	void IterateArcTo(float, float, float, bool, bool, gfx::Point) override { points += 3; }
};


} // namespace


NGUI_FUZZ_TARGET(Shape)
{
	// What is tested: A BShape built from arbitrary steps (MoveTo, LineTo,
	// BezierTo, ArcTo with any radii, angle and flags, Close), with small
	// coordinates or with any float values (NaN and infinity included) as
	// the first byte says; its iteration, ToPath() and Bounds().
	// Success criteria: No crash or sanitizer report. The iterator hands
	// out curves in threes. ToPath() is a valid sequence of verbs; with
	// small coordinates and radii it is finite, and an arc becomes at most
	// four curves; each step adds at most one Close.
	// Why it matters: Shapes come from applications and files (Haiku's
	// archived BShape, icons); the arc conversion computes with radii and
	// angles anyone may pass.
	//
	Bytes bytes(data);
	const bool wild = (bytes.Next() & 1) != 0;
	BShape shape;
	int arcs = 0;
	int others = 0;
	while (!bytes.AtEnd()) {
		switch (bytes.Next() % 6) {
			case 0:
				shape.MoveTo(bytes.Point(wild));
				others++;
				break;
			case 1:
				shape.LineTo(bytes.Point(wild));
				others++;
				break;
			case 2:
			{
				const gfx::Point control1 = bytes.Point(wild);
				const gfx::Point control2 = bytes.Point(wild);
				shape.BezierTo(control1, control2, bytes.Point(wild));
				others++;
				break;
			}
			case 3:
			case 4:
			{
				const float rx = wild ? bytes.Float() : bytes.Small();
				const float ry = wild ? bytes.Float() : bytes.Small();
				const float angle = wild ? bytes.Float() : bytes.Small();
				const std::uint8_t flags = bytes.Next();
				shape.ArcTo(rx, ry, angle, (flags & 1) != 0, (flags & 2) != 0,
					bytes.Point(wild));
				arcs++;
				break;
			}
			case 5:
				shape.Close();
				break;
		}
	}

	CountingIterator counter;
	counter.Iterate(shape);
	const gfx::Path path = shape.ToPath();
	NGUI_FUZZ_CHECK(gfx::Path::IsValidSequence(path.Verbs(), path.Points().size()));
	(void)shape.Bounds();
	if (!wild) {
		NGUI_FUZZ_CHECK(path.IsFinite());
		// every step but an arc gives at most one verb plus a Move, an arc
		// at most four curves plus a Move and a line; each step can be
		// followed by one Close (BShape drops a second one)
		NGUI_FUZZ_CHECK(path.Verbs().size()
			<= static_cast<std::size_t>(3 * others + 8 * arcs + 64));
	}
}
