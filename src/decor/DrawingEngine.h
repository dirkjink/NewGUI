/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The calls of Haiku's DrawingEngine that the decorators make, with a color
// each, on the compatibility layer, so that their code ports line by line.


#include "compat/CompatView.h"
#include "compat/Gradient.h"


namespace ngui::detail::decor {


inline void
StrokeLine(compat::CompatView& view, compat::BPoint start, compat::BPoint end,
	compat::rgb_color color)
{
	view.SetHighColor(color);
	view.StrokeLine(start, end);
}


inline void
StrokeRect(compat::CompatView& view, compat::BRect rect, compat::rgb_color color)
{
	view.SetHighColor(color);
	view.StrokeRect(rect);
}


inline void
FillRect(compat::CompatView& view, compat::BRect rect, compat::rgb_color color)
{
	view.SetHighColor(color);
	view.FillRect(rect);
}


inline void
FillRect(compat::CompatView& view, compat::BRect rect, const compat::BGradientLinear& gradient)
{
	view.FillRect(rect, gradient);
}


inline void
StrokePoint(compat::CompatView& view, compat::BPoint point, compat::rgb_color color)
{
	view.SetHighColor(color);
	view.FillRect(compat::BRect(point, point));
}


} // namespace ngui::detail::decor
