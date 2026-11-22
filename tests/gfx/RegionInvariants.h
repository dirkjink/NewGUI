/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <vector>

#include <ngui/gfx/Region.h>


namespace ngui::test {


/*!	Checks the canonical form a Region promises (the y-x banding of X11,
	described in RegionSupport.cpp), so that two regions covering the same
	area always have the same rectangles:

	- every rectangle is non-empty,
	- the rectangles are sorted by top, then by left,
	- rectangles with the same top have the same bottom (a band), and bands
	  do not overlap,
	- two rectangles in a band neither overlap nor touch,
	- two bands that touch vertically differ in their rectangles (otherwise
	  they would have been merged into one),
	- Frame() is the bounding box of all rectangles, `{0, 0, 0, 0}` if empty.

	Only this canonical form makes Region::operator== (a comparison of the
	rectangle lists, as in Haiku) a comparison of areas.

	Returns false at the first violation.
*/
inline bool
IsCanonical(const gfx::Region& region)
{
	const std::int32_t count = region.CountRects();
	if (count == 0)
		return region.Frame() == gfx::IntRect{0, 0, 0, 0};

	gfx::IntRect bounds = region.RectAt(0);
	std::vector<gfx::IntRect> previousBand;
	std::vector<gfx::IntRect> band;
	const auto sameSpans = [](const std::vector<gfx::IntRect>& a,
		const std::vector<gfx::IntRect>& b) {
		if (a.size() != b.size())
			return false;
		for (std::size_t i = 0; i < a.size(); i++) {
			if (a[i].x0 != b[i].x0 || a[i].x1 != b[i].x1)
				return false;
		}
		return true;
	};

	for (std::int32_t i = 0; i < count; i++) {
		const gfx::IntRect rect = region.RectAt(i);
		if (rect.IsEmpty())
			return false;
		if (rect.x0 < bounds.x0)
			bounds.x0 = rect.x0;
		if (rect.y0 < bounds.y0)
			bounds.y0 = rect.y0;
		if (rect.x1 > bounds.x1)
			bounds.x1 = rect.x1;
		if (rect.y1 > bounds.y1)
			bounds.y1 = rect.y1;

		if (!band.empty() && rect.y0 == band.back().y0) {
			// Same band: same height, ordered, with a gap in between.
			if (rect.y1 != band.back().y1 || rect.x0 <= band.back().x1)
				return false;
			band.push_back(rect);
			continue;
		}
		if (!band.empty()) {
			// A new band starts at or below the bottom of the current one.
			if (rect.y0 < band.back().y1)
				return false;
			if (!previousBand.empty() && previousBand.back().y1 == band.back().y0
				&& sameSpans(previousBand, band)) {
				return false;
			}
			previousBand = band;
		}
		band.clear();
		band.push_back(rect);
	}
	if (!previousBand.empty() && previousBand.back().y1 == band.back().y0
		&& sameSpans(previousBand, band)) {
		return false;
	}
	return region.Frame() == bounds;
}


} // namespace ngui::test
