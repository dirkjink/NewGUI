/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <span>

#include <ngui/gfx/RRect.h>
#include <ngui/gfx/Rect.h>


namespace {


using ngui::gfx::Point;
using ngui::gfx::Radii;
using ngui::gfx::Rect;
using ngui::gfx::RRect;


// Reads the next float from the input; missing bytes count as zero.
class FloatReader {
public:
								FloatReader(std::span<const std::uint8_t> data);

			float				Next();

private:
			std::span<const std::uint8_t> fData;
};


FloatReader::FloatReader(std::span<const std::uint8_t> data)
	:
	fData(data)
{
}


float
FloatReader::Next()
{
	std::uint8_t bytes[sizeof(float)] = {};
	const std::size_t count = std::min(fData.size(), sizeof(float));
	std::copy_n(fData.begin(), count, bytes);
	fData = fData.subspan(count);

	float value;
	std::memcpy(&value, bytes, sizeof(value));
	return value;
}


} // namespace


NGUI_FUZZ_TARGET(Rect)
{
	// What is tested: The set operations of Rect (&, |, Contains, Intersects)
	// against each other for arbitrary rectangles and points, and the bounds of
	// RRect::Normalized() for arbitrary radii.
	// Success criteria: A point is in a & b exactly if it is in both; a | b
	// contains both operands; Intersects agrees with a non-empty intersection
	// and is symmetric; normalized radii are >= 0, at most half the shorter
	// side, and two adjacent radii never exceed their edge.
	// Why it matters: The clip region (M2) and every drawing operation build on
	// these operations; the half-open edges and the empty cases are where hand
	// written tests miss combinations.
	//
	// Given
	FloatReader reader(data);
	const Rect a = {reader.Next(), reader.Next(), reader.Next(), reader.Next()};
	const Rect b = {reader.Next(), reader.Next(), reader.Next(), reader.Next()};
	const Point point = {reader.Next(), reader.Next()};
	const Radii radii = {reader.Next(), reader.Next(), reader.Next(), reader.Next()};

	// The invariants hold for finite input; NaN and infinity only must not
	// crash (checked by running at all, with the sanitizers).
	if (!a.IsFinite() || !b.IsFinite() || !point.IsFinite())
		return;

	// When
	const Rect intersection = a & b;
	const Rect bounds = a | b;

	// Then
	NGUI_FUZZ_CHECK(intersection.Contains(point) == (a.Contains(point) && b.Contains(point)));
	NGUI_FUZZ_CHECK(a.Intersects(b) == !intersection.IsEmpty());
	NGUI_FUZZ_CHECK(a.Intersects(b) == b.Intersects(a));
	NGUI_FUZZ_CHECK(a.Contains(intersection) && b.Contains(intersection));
	NGUI_FUZZ_CHECK(bounds.Contains(a) && bounds.Contains(b));
	NGUI_FUZZ_CHECK(!a.Contains(point) || bounds.Contains(point));

	if (a.IsEmpty() || !std::isfinite(a.Width()) || !std::isfinite(a.Height()))
		return;

	const Radii fitted = RRect{a, radii}.Normalized().radii;
	const float limit = std::min(a.Width(), a.Height()) / 2.0f;
	for (float radius : {fitted.tl, fitted.tr, fitted.br, fitted.bl})
		NGUI_FUZZ_CHECK(radius >= 0.0f && radius <= limit);
	NGUI_FUZZ_CHECK(fitted.tl + fitted.tr <= a.Width());
	NGUI_FUZZ_CHECK(fitted.br + fitted.bl <= a.Width());
	NGUI_FUZZ_CHECK(fitted.tr + fitted.br <= a.Height());
	NGUI_FUZZ_CHECK(fitted.bl + fitted.tl <= a.Height());
}
