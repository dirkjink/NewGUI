/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <string>

#include <ngui/gfx/ExportBackend.h>


namespace ngui {
class TextSystem;
}


namespace ngui::gfx {


/*!	The SVG export backend (specification 19.3, module M20): a DisplayList
	as an SVG document.

	- Axis-parallel rectangles and lines become `<rect>` with
	  `shape-rendering="crispEdges"` (rule A1), frames lie inside (6.1),
	  rounded rectangles, rings, arcs and paths become `<path>`.
	- Linear gradients and clip regions go into `<defs>`, once each; images
	  as PNG data: URIs, once each, drawn with `<use>`; nested lists become
	  `<g transform="translate(…)">`.
	- Glyph runs are the glyphs' outlines from the text system
	  (TextSystem::GlyphOutline()), each glyph once in `<defs>`, or `<text>`
	  with the font's family and per-character positions (ExportOptions::
	  TextMode).

	The output is deterministic (E1): the same list and options give the
	same bytes, with ids numbered in order of first use and numbers with at
	most three decimals. The viewBox is the bounds in logical pixels, width
	and height the bounds times the scale (E2); colors are `#rrggbb` with
	a separate opacity (E3).
*/
class SvgExportBackend final : public ExportBackend {
public:
	explicit					SvgExportBackend(const TextSystem* text = nullptr);
									/*!< Glyphs from \a text (outlines, family names);
										TextSystem::Default() if nullptr. */

			Caps				Capabilities() const override;
									/*!< The core profile, Bézier paths and concave polygons. */
			std::expected<void, Error> Export(const DisplayList& list, const Rect& bounds,
									std::ostream& out, const ExportOptions& options) override;
									/*!< Writes \a list as SVG. */

private:
			const TextSystem*	fText;
};


/*!	\a list as an SVG document of \a bounds, through SvgExportBackend with
	\a text (TextSystem::Default() if nullptr). */
std::expected<std::string, Error> ExportSvg(const DisplayList& list, const Rect& bounds,
	const ExportOptions& options = {}, const TextSystem* text = nullptr);


} // namespace ngui::gfx
