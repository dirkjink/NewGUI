/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/LinearGradient.h>

#include <algorithm>
#include <cmath>


namespace ngui::gfx {


std::expected<LinearGradient, Error>
LinearGradient::Create(Point p0, Point p1, std::span<const GradientStop> stops)
{
	if (stops.size() < 2 || stops.size() > 4)
		return Failure(ErrorCode::InvalidArgument, "a gradient has 2 to 4 stops");

	LinearGradient gradient = {p0, p1, {}, static_cast<std::uint8_t>(stops.size())};
	std::ranges::copy(stops, gradient.stops.begin());
	if (!gradient.IsValid()) {
		return Failure(ErrorCode::InvalidArgument,
			"gradient stops must ascend in [0, 1] with valid colors, points must be finite");
	}
	return gradient;
}


bool
LinearGradient::operator==(const LinearGradient& other) const
{
	return p0 == other.p0 && p1 == other.p1 && std::ranges::equal(Stops(), other.Stops());
}


std::span<const GradientStop>
LinearGradient::Stops() const
{
	return std::span(stops).first(std::min<std::size_t>(count, stops.size()));
}


bool
LinearGradient::IsValid() const
{
	if (count < 2 || count > 4 || !p0.IsFinite() || !p1.IsFinite())
		return false;

	float previous = 0.0f;
	for (const GradientStop& stop : Stops()) {
		if (!(stop.offset >= previous && stop.offset <= 1.0f) || !stop.color.IsValid())
			return false;
		previous = stop.offset;
	}
	return true;
}


Color
LinearGradient::ColorAt(Point point) const
{
	const std::span<const GradientStop> used = Stops();

	// In double: for finite input the products cannot overflow, so t is
	// never NaN.
	const double dx = static_cast<double>(p1.x) - p0.x;
	const double dy = static_cast<double>(p1.y) - p0.y;
	const double lengthSquared = dx * dx + dy * dy;
	if (lengthSquared == 0.0)
		return used.back().color;

	const double projection = (static_cast<double>(point.x) - p0.x) * dx
		+ (static_cast<double>(point.y) - p0.y) * dy;
	const double unclamped = projection / lengthSquared;

	// Before the first and after the last stop are decided on the unclamped
	// value: with all stops at offset 1, a point beyond p1 clamps to t = 1 and
	// would otherwise get the first stop's color (found by the fuzz target
	// Gradient).
	if (unclamped < used.front().offset)
		return used.front().color;
	if (unclamped > used.back().offset)
		return used.back().color;

	const float t = static_cast<float>(std::clamp(unclamped, 0.0, 1.0));
	for (std::size_t i = 1; i < used.size(); i++) {
		const GradientStop& from = used[i - 1];
		const GradientStop& to = used[i];
		if (t >= to.offset)
			continue;

		// from.offset <= t < to.offset, so the interval is not empty. At a
		// hard edge (two stops at one offset) the later stop's color applies.
		const float u = (t - from.offset) / (to.offset - from.offset);
		return {std::lerp(from.color.r, to.color.r, u), std::lerp(from.color.g, to.color.g, u),
			std::lerp(from.color.b, to.color.b, u), std::lerp(from.color.a, to.color.a, u)};
	}
	return used.back().color;
}


} // namespace ngui::gfx
