/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <span>

#include <ngui/Error.h>
#include <ngui/IconUtils.h>


namespace ngui::svg {


/*!	The vector icon renderer for SVG (library ngui_svg, vcpkg feature svg):
	lunasvg reads the icon, its rasterizer plutovg draws it, independent of
	the drawing backend. Install it with VectorIconRenderer::SetDefault().
	It blends in linear light as Haiku's IconRenderer does: the icon's colors
	go through a gamma table of 2.2 before drawing, the pixels through its
	inverse after, so semi-transparent shadows and edges look as in Haiku.
	Dashed strokes are drawn solid: plutovg's dashes can take unbounded
	memory (an icon needs none; Haiku's HVIF icons have none). Media queries
	on the width are resolved for the width drawn (ResolveMediaQueries()), so
	Haiku's icons show their level of detail. An SVG whose
	content reaches more than 65536 pixels out of the bitmap is BadData:
	plutovg's rasterizer hangs on such coordinates where long has 32 bits
	(Windows).

	\code
	ngui::svg::LunaSvgRenderer icons;
	ngui::VectorIconRenderer::SetDefault(&icons);
	\endcode
*/
class LunaSvgRenderer : public VectorIconRenderer {
public:
			std::expected<void, Error> Render(std::span<const std::uint8_t> icon,
									BBitmap& result) const override;
									/*!< Draws the SVG \a icon into \a result (RGBA32), fitted
										by its width and height (or view box), centered;
										InvalidArgument for another bitmap, BadData for an
										SVG lunasvg cannot read, without size or reaching
										too far. */
};


} // namespace ngui::svg
