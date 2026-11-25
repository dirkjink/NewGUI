/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BShape as far as the ported looks use it: straight lines only
// (specification 17.2); a shape with curves does not compile. The points
// are in view coordinates, offset by the pen location when drawn.


#include <vector>

#include "Point.h"


namespace ngui::compat {


class BShape {
public:
	struct Figure {
		std::vector<BPoint>	points;
		bool				closed = false;
	};

	// A MoveTo() right after a MoveTo() replaces its point, as in Haiku.
	void MoveTo(BPoint point)
	{
		if (!fFigures.empty() && fFigures.back().points.size() == 1
			&& !fFigures.back().closed) {
			fFigures.back().points.front() = point;
		} else
			fFigures.push_back({{point}, false});
	}

	// A line without a figure starts at the origin, one after Close() at
	// the start of the closed figure (AGG's path semantics).
	void LineTo(BPoint point)
	{
		if (fFigures.empty())
			fFigures.push_back({{BPoint(0, 0)}, false});
		else if (fFigures.back().closed)
			fFigures.push_back({{fFigures.back().points.front()}, false});
		fFigures.back().points.push_back(point);
	}

	void Close()
	{
		if (!fFigures.empty())
			fFigures.back().closed = true;
	}

	void Clear() { fFigures.clear(); }
	const std::vector<Figure>& Figures() const { return fFigures; }

private:
	std::vector<Figure>		fFigures;
};


} // namespace ngui::compat
