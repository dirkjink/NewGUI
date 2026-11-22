/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <ngui/gfx/DisplayList.h>


namespace {


using ngui::gfx::DisplayList;
using ngui::gfx::Op;
using ngui::gfx::OpCode;


// Reads every array of every operation, so that the sanitizers see any
// access the backends would make; returns a value that depends on all of
// it, so that nothing is optimized away.
std::uint64_t
Touch(const DisplayList& list)
{
	std::uint64_t sum = list.OpCount();
	for (std::uint32_t i = 0; i < list.ClipCount(); i++) {
		for (const auto& rect : list.Clip(i))
			sum += static_cast<std::uint64_t>(rect.x0 ^ rect.y1);
	}
	for (const Op& op : list.Ops()) {
		sum += op.ClipIndex() + op.Payload().size();
		switch (op.Code()) {
			case OpCode::FillConvexPolygon:
			case OpCode::StrokePolyline:
				for (const auto& point : op.Points())
					sum += static_cast<std::uint64_t>(point.x != point.y);
				break;
			case OpCode::DrawLines:
				for (const auto& line : op.Lines())
					sum += static_cast<std::uint64_t>(line.color.a > 0.5f);
				break;
			case OpCode::DrawGlyphRun:
				for (std::uint32_t glyph : op.Glyphs())
					sum += glyph;
				for (const auto& position : op.Positions())
					sum += static_cast<std::uint64_t>(position.x > 0);
				for (std::uint32_t cluster : op.Clusters())
					sum += cluster;
				for (char8_t c : op.Text())
					sum += c;
				break;
			default:
				break;
		}
	}
	for (const auto& child : list.Children())
		sum += Touch(*child);
	return sum;
}


} // namespace


NGUI_FUZZ_TARGET(DisplayList)
{
	// What is tested: Deserialize() of arbitrary bytes, and Serialize() and
	// the iteration of every list it accepts.
	// Success criteria: No input crashes or reads out of bounds (checked by
	// the sanitizers); every accepted input serializes back to exactly the
	// same bytes.
	// Why it matters: A serialized DisplayList may come from outside, for
	// remote rendering (specification 11.3); Deserialize() is the boundary
	// that must hold for anything, and the canonical form is what makes
	// snapshots comparable.
	//
	// When
	const auto list = DisplayList::Deserialize(std::as_bytes(data));
	if (!list.has_value())
		return;

	// Then
	const std::vector<std::byte> again = (*list)->Serialize();
	NGUI_FUZZ_CHECK(again.size() == data.size()
		&& std::equal(again.begin(), again.end(), std::as_bytes(data).begin()));
	NGUI_FUZZ_CHECK(Touch(**list) >= (*list)->OpCount());
}
