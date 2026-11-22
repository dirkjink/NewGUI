/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>

#include <ngui/gfx/DisplayList.h>


namespace ngui::gfx {


/*!	The DisplayList \a list as deterministic text (specification 15), for
	snapshot tests and debugging: one line per operation with its name and
	arguments, a `clip` line whenever the clip region changes (with all its
	rectangles), nested lists indented below their DrawList. Numbers are
	rounded to three decimals with trailing zeros removed, so the text is
	the same on every platform.

	\code
	DisplayList bounds=[0 0 100 100] ops=2
	clip [0 0 100 100]
	FillRect rect=[0 0 50 50] color=(1 0 0 1) width=1 cap=Butt join=Miter aa=1
	StrokeArc oval=[0 0 20 20] start=0 sweep=270 color=(0 0 0 1) width=1 …
	\endcode
*/
std::string						DumpText(const DisplayList& list);


/*!	The DisplayList \a list as deterministic JSON (specification 15): an
	object with `bounds` and `ops`; every operation is an object with `op`
	(its name), `clip` (the resolved rectangles) and its arguments; a
	DrawList carries the nested list as `list`. Numbers are rounded like in
	DumpText(); text is valid UTF-8 (invalid bytes become U+FFFD).
*/
std::string						DumpJson(const DisplayList& list);


} // namespace ngui::gfx
