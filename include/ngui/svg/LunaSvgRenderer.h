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
	Dashed strokes are drawn solid: plutovg's dashes can take unbounded
	memory (an icon needs none; Haiku's HVIF icons have none).

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
										SVG lunasvg cannot read or without size. */
};


} // namespace ngui::svg
