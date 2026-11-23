/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Render.h>

#include <cmath>
#include <optional>


namespace ngui::test {


std::expected<Image, Error>
RenderToImage(gfx::Backend& backend, const gfx::DisplayList& list, float scale,
	const gfx::Color& clear)
{
	const gfx::Rect bounds = list.Bounds();
	if (!(scale > 0) || bounds.Width() <= 0 || bounds.Height() <= 0)
		return Failure(ErrorCode::InvalidArgument, "empty bounds or scale");
	const gfx::Size device = {std::round(bounds.Width() * scale),
		std::round(bounds.Height() * scale)};
	const gfx::SurfaceRef surface = backend.CreateOffscreenSurface(device, scale);
	if (surface == nullptr)
		return Failure(ErrorCode::NotSupported, "no offscreen surface");
	backend.BeginFrame(*surface, clear);
	backend.Render(list, {-bounds.x0, -bounds.y0});
	backend.EndFrame(*surface);
	const std::optional<gfx::CpuPixels> pixels = surface->Pixels();
	if (!pixels.has_value())
		return Failure(ErrorCode::NotSupported, "the surface has no CPU pixels");
	Image image = Image::FromCpuPixels(*pixels);
	if (image.IsEmpty())
		return Failure(ErrorCode::OutOfRange, "surface too large");
	return image;
}


} // namespace ngui::test
