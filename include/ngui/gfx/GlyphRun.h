/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <span>
#include <string_view>

#include <ngui/gfx/Point.h>


namespace ngui::gfx {


/*!	Identifies an image registered with the backends (specification 9);
	unique for the whole frontend. 0 is invalid. */
using ImageHandle = std::uint32_t;

/*!	Identifies a font file provided by the text subsystem (specification 10). */
using FontId = std::uint32_t;


/*!	A shaped run of glyphs, ready to draw (specification 10). Shaping, BiDi,
	kerning, line breaking and truncation happen before, in the text
	subsystem; a backend draws exactly these glyphs at these positions.

	The spans are borrowed: recording the run copies them into the
	DisplayList, so the caller need not keep them alive afterwards.
*/
struct GlyphRun {
			FontId				font;		/*!< Font, assigned by the text subsystem. */
			float				sizePx;		/*!< Font size in logical pixels. */
			std::span<const std::uint32_t> glyphs;
									/*!< Glyph indices of the font file, not code points. */
			std::span<const Point> positions;
									/*!< Baseline position of each glyph, relative to the
										origin; as many as #glyphs. */
			std::u8string_view	text;		/*!< Optional source text, UTF-8. */
			std::span<const std::uint32_t> clusters;
									/*!< Optional: per glyph the byte offset of its cluster in
										#text; empty or as many as #glyphs. */
};


} // namespace ngui::gfx
