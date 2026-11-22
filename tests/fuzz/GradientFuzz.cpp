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

#include <ngui/gfx/LinearGradient.h>


namespace {


using ngui::gfx::Color;
using ngui::gfx::GradientStop;
using ngui::gfx::LinearGradient;
using ngui::gfx::Point;


// Reads the next float from the input; missing bytes count as zero.
float
NextFloat(std::span<const std::uint8_t>& data)
{
	std::uint8_t bytes[sizeof(float)] = {};
	const std::size_t count = std::min(data.size(), sizeof(float));
	std::copy_n(data.begin(), count, bytes);
	data = data.subspan(count);

	float value;
	std::memcpy(&value, bytes, sizeof(value));
	return value;
}


} // namespace


NGUI_FUZZ_TARGET(Gradient)
{
	// What is tested: Create() and ColorAt() of LinearGradient for arbitrary
	// points and stops.
	// Success criteria: Whenever Create() succeeds, ColorAt() gives a valid
	// color for every finite point, and exactly the first stop's color before
	// p0 and the last stop's after p1.
	// Why it matters: The gradient formula (specification 8) runs for every
	// pixel of a gradient fill; extreme but finite coordinates must not turn
	// into NaN or colors outside [0, 1].
	//
	// Given
	std::span<const std::uint8_t> input = data;
	const Point p0 = {NextFloat(input), NextFloat(input)};
	const Point p1 = {NextFloat(input), NextFloat(input)};
	const Point q = {NextFloat(input), NextFloat(input)};
	std::array<GradientStop, 4> stops;
	for (GradientStop& stop : stops) {
		stop.offset = NextFloat(input);
		stop.color = {NextFloat(input), NextFloat(input), NextFloat(input), NextFloat(input)};
	}
	const std::size_t count = 2 + (input.empty() ? 0 : input.front() % 3);

	// When
	const auto gradient = LinearGradient::Create(p0, p1, std::span(stops).first(count));
	if (!gradient.has_value() || !q.IsFinite())
		return;

	// Then
	NGUI_FUZZ_CHECK(gradient->ColorAt(q).IsValid());
	const Point before = p0 - (p1 - p0);
	const Point after = p1 + (p1 - p0);
	if (p0 != p1 && before.IsFinite() && after.IsFinite() && before != p0 && after != p1) {
		NGUI_FUZZ_CHECK(gradient->ColorAt(before) == stops[0].color);
		NGUI_FUZZ_CHECK(gradient->ColorAt(after) == stops[count - 1].color);
	}
}
