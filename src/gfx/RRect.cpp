/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/RRect.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>


namespace ngui::gfx {


namespace {


// A negative, NaN or infinite radius means "no rounding", as in Skia.
float
_SanitizedRadius(float radius)
{
	return std::isfinite(radius) && radius > 0.0f ? radius : 0.0f;
}


// The largest factor <= 1 for which two adjacent radii fit their edge. In
// double, so that the sum of two large radii cannot overflow.
double
_FitFactor(float edge, float first, float second)
{
	const double sum = static_cast<double>(first) + second;
	return sum > edge ? edge / sum : 1.0;
}


} // namespace


RRect
RRect::Normalized() const
{
	if (rect.IsEmpty())
		return {rect, {0.0f, 0.0f, 0.0f, 0.0f}};

	Radii fitted = {_SanitizedRadius(radii.tl), _SanitizedRadius(radii.tr),
		_SanitizedRadius(radii.br), _SanitizedRadius(radii.bl)};

	const float width = rect.Width();
	const float height = rect.Height();

	const double factor = std::min({_FitFactor(width, fitted.tl, fitted.tr),
		_FitFactor(height, fitted.tr, fitted.br),
		_FitFactor(width, fitted.br, fitted.bl),
		_FitFactor(height, fitted.bl, fitted.tl)});
	// Halving a subnormal extent rounds and may round up; then two radii at
	// the limit would exceed their edge (found by the fuzz target Rect).
	const float shorter = std::min(width, height);
	float limit = shorter / 2.0f;
	if (limit + limit > shorter)
		limit = std::nextafter(limit, 0.0f);

	for (float* radius : {&fitted.tl, &fitted.tr, &fitted.br, &fitted.bl})
		*radius = std::min(static_cast<float>(*radius * factor), limit);

	return {rect, fitted};
}


} // namespace ngui::gfx
