/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Rendering a DisplayList with a backend and reading its pixels, for the
// tests of the backends and of the looks.


#include <cstddef>
#include <cstdlib>
#include <ostream>
#include <span>

#include <ngui/TextSystem.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Backend.h>


namespace ngui::test {


using gfx::CpuPixels;
using gfx::DisplayListRef;
using gfx::PixelFormat;
using gfx::Point;
using gfx::Size;
using gfx::SurfaceRef;


struct Rgba {
			int					r;
			int					g;
			int					b;
			int					a;

			bool				operator==(const Rgba& other) const = default;
};


inline void
PrintTo(const Rgba& c, std::ostream* out)
{
	*out << "(" << c.r << " " << c.g << " " << c.b << " " << c.a << ")";
}


inline constexpr Rgba kWhite = {255, 255, 255, 255};
inline constexpr Rgba kRed = {255, 0, 0, 255};
inline constexpr Rgba kBlue = {0, 0, 255, 255};


// Renders a list with a fresh \a BackendT into a new offscreen surface on
// white and keeps it for reading pixels; \a fonts are added to the backend
// first.
template<typename BackendT>
class RenderedWith {
public:
								RenderedWith(const DisplayListRef& list, Size device,
									float scale = 1, Point offset = {0, 0},
									std::span<const FontFile> fonts = {});

			Rgba				At(int x, int y) const;

private:
			BackendT			fBackend;
			SurfaceRef			fSurface;
};


template<typename BackendT>
RenderedWith<BackendT>::RenderedWith(const DisplayListRef& list, Size device, float scale,
	Point offset, std::span<const FontFile> fonts)
	:
	fSurface(fBackend.CreateOffscreenSurface(device, scale))
{
	for (const FontFile& font : fonts)
		(void)fBackend.AddFont(font.id, font.data, font.faceIndex);
	fBackend.BeginFrame(*fSurface, {1, 1, 1, 1});
	fBackend.Render(*list, offset);
	fBackend.EndFrame(*fSurface);
}


template<typename BackendT>
Rgba
RenderedWith<BackendT>::At(int x, int y) const
{
	const CpuPixels pixels = *fSurface->Pixels();
	const std::byte* p = pixels.bytes.data() + static_cast<std::size_t>(y) * pixels.stride
		+ static_cast<std::size_t>(x) * 4;
	const auto byte = [&](int i) { return static_cast<int>(p[i]); };
	return pixels.format == PixelFormat::Bgra8Premul ? Rgba{byte(2), byte(1), byte(0), byte(3)}
		: Rgba{byte(0), byte(1), byte(2), byte(3)};
}


inline bool
Near(const Rgba& actual, const Rgba& expected, int tolerance)
{
	return std::abs(actual.r - expected.r) <= tolerance
		&& std::abs(actual.g - expected.g) <= tolerance
		&& std::abs(actual.b - expected.b) <= tolerance
		&& std::abs(actual.a - expected.a) <= tolerance;
}


} // namespace ngui::test
