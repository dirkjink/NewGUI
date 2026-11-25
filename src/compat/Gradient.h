/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BGradientLinear as far as the ported looks use it: stops with
// offsets 0 to 255, kept sorted the way BGradient::AddColor() keeps them,
// start and end in view coordinates. The CompatView turns it into a
// gfx::LinearGradient (specification 8, 17.2).


#include <cstddef>
#include <vector>

#include <ngui/SupportDefs.h>

#include "InterfaceDefs.h"
#include "Point.h"


namespace ngui::compat {


class BGradientLinear {
public:
	struct ColorStop {
		rgb_color	color;
		float		offset;
	};

	// Inserted before the first stop with a larger offset; -1 for an offset
	// outside 0 to 255.
	int32 AddColor(const rgb_color& color, float offset)
	{
		if (offset < 0.f || offset > 255.f)
			return -1;
		std::size_t index = 0;
		while (index < fStops.size() && fStops[index].offset <= offset)
			index++;
		fStops.insert(fStops.begin() + static_cast<std::ptrdiff_t>(index), {color, offset});
		return static_cast<int32>(index);
	}

	bool SetColor(int32 index, const rgb_color& color)
	{
		if (index < 0 || static_cast<std::size_t>(index) >= fStops.size())
			return false;
		fStops[static_cast<std::size_t>(index)].color = color;
		return true;
	}

	void MakeEmpty() { fStops.clear(); }
	int32 CountColorStops() const { return static_cast<int32>(fStops.size()); }
	const std::vector<ColorStop>& ColorStops() const { return fStops; }

	void SetStart(const BPoint& start) { fStart = start; }
	BPoint Start() const { return fStart; }
	void SetEnd(const BPoint& end) { fEnd = end; }
	BPoint End() const { return fEnd; }

private:
	std::vector<ColorStop>	fStops;
	BPoint					fStart;
	BPoint					fEnd;
};


} // namespace ngui::compat
