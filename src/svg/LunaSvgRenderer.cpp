/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/svg/LunaSvgRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <lunasvg.h>

#include <ngui/Bitmap.h>
#include <ngui/svg/MediaQueries.h>

#include "svg/IconGamma.h"


namespace ngui::svg {


std::expected<void, Error>
LunaSvgRenderer::Render(std::span<const std::uint8_t> icon, BBitmap& result) const
{
	if (!result.IsValid() || result.ColorSpace() != BColorSpace::RGBA32)
		return Failure(ErrorCode::InvalidArgument, "the bitmap is not a valid RGBA32 one");

	if (icon.empty())
		return Failure(ErrorCode::BadData, "no SVG");
	// Blended in linear light as Haiku's IconRenderer: the colors into it
	// before drawing, the pixels back after.
	const std::string linear = svg::detail::LinearizeColors(
		std::string_view(reinterpret_cast<const char*>(icon.data()), icon.size()));
	const std::string_view text = linear;
	std::unique_ptr<lunasvg::Document> document
		= lunasvg::Document::loadFromData(text.data(), text.size());
	if (document == nullptr)
		return Failure(ErrorCode::BadData, "not an SVG lunasvg can read");
	const float iconWidth = document->width();
	const float iconHeight = document->height();
	if (!(iconWidth > 0) || !(iconHeight > 0) || !std::isfinite(iconWidth)
		|| !std::isfinite(iconHeight)) {
		return Failure(ErrorCode::BadData, "the SVG has no size");
	}

	// Fitted evenly, centered (Haiku's icons are square and fill the bitmap).
	const gfx::Rect bounds = result.Bounds();
	const auto width = static_cast<int>(bounds.Width());
	const auto height = static_cast<int>(bounds.Height());
	const float scale = std::min(static_cast<float>(width) / iconWidth,
		static_cast<float>(height) / iconHeight);
	const float x = (static_cast<float>(width) - iconWidth * scale) / 2;
	const float y = (static_cast<float>(height) - iconHeight * scale) / 2;

	// The level of detail of Haiku's icons: media queries on the width drawn,
	// which lunasvg does not evaluate, resolved before it reads the icon anew.
	if (text.find("@media") != std::string_view::npos) {
		const std::string resolved = ResolveMediaQueries(text, iconWidth * scale);
		document = lunasvg::Document::loadFromData(resolved.data(), resolved.size());
		if (document == nullptr)
			return Failure(ErrorCode::BadData, "not an SVG lunasvg can read");
		if (document->width() != iconWidth || document->height() != iconHeight)
			return Failure(ErrorCode::BadData, "the SVG's size depends on its media queries");
	}
	// Dashed strokes are drawn solid: plutovg makes every dash a path of its
	// own, so a long line with short dashes takes gigabytes and can crash
	// (found by the fuzz target VectorIcon). Icons need no dashes; Haiku's
	// HVIF has none.
	document->applyStyleSheet("*{stroke-dasharray:none !important}");

	// Nothing may reach far out of the bitmap: plutovg's rasterizer (from
	// FreeType) computes with long, which has 32 bits on Windows, and hung
	// on a line to x 23 333 330 there (found by the fuzz target VectorIcon).
	// Its bounding box includes strokes and transforms.
	const lunasvg::Box box = document->boundingBox();
	if (box.w >= 0 && box.h >= 0) {
		constexpr float kLimit = 65536;
		const float left = box.x * scale + x;
		const float top = box.y * scale + y;
		const float right = (box.x + box.w) * scale + x;
		const float bottom = (box.y + box.h) * scale + y;
		for (const float edge : {left, top, right, bottom}) {
			if (!(std::fabs(edge) <= kLimit))
				return Failure(ErrorCode::BadData, "the SVG reaches too far out");
		}
	}
	lunasvg::Bitmap canvas(width, height);
	if (canvas.isNull())
		return Failure(ErrorCode::OutOfMemory, "no canvas");
	canvas.clear(0x00000000);
	document->render(canvas, lunasvg::Matrix(scale, 0, 0, scale, x, y));

	// lunasvg's ARGB32 lies in memory as B, G, R, A like B_RGBA32, but
	// premultiplied; B_RGBA32 is not. Out of linear light as Haiku's
	// apply_gamma_inv(), on the colors that are not premultiplied.
	const svg::detail::IconGamma& gamma = svg::detail::IconGamma::Haiku();
	const std::span<std::uint8_t> bits = result.Bits();
	const auto stride = static_cast<std::size_t>(canvas.stride());
	const auto bytesPerRow = static_cast<std::size_t>(result.BytesPerRow());
	for (int row = 0; row < height; row++) {
		const std::uint8_t* from = canvas.data() + static_cast<std::size_t>(row) * stride;
		std::uint8_t* to = bits.data() + static_cast<std::size_t>(row) * bytesPerRow;
		for (int column = 0; column < width; column++) {
			const std::uint32_t alpha = from[3];
			for (int component = 0; component < 3; component++) {
				const std::uint32_t value = alpha == 0 ? 0
					: std::min<std::uint32_t>(255, (from[component] * 255u + alpha / 2) / alpha);
				to[component] = gamma.inv[value];
			}
			to[3] = static_cast<std::uint8_t>(alpha);
			from += 4;
			to += 4;
		}
	}
	return {};
}


} // namespace ngui::svg
