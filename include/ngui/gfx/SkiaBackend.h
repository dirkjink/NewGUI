/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <memory>
#include <span>

#include <ngui/gfx/Backend.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/GlyphRun.h>


namespace ngui::gfx {


/*!	The Skia backend (specification 13), the target backend: renders
	DisplayLists with Skia into CPU raster surfaces (N32, premultiplied,
	without a color space, so blending happens in the encoded space, 4.3).
	Part of the library ngui_skia, built with NGUI_WITH_SKIA; this header
	includes no Skia header (13.5).

	- **A1:** axis-parallel rectangles, rectangle frames and axis-parallel
	  lines are drawn without antialiasing, their edges rounded to whole
	  device pixels after scaling (`floor(v + 0.5)`); frames as four
	  rectangles, so they are exact at every scale.
	- **A2:** everything else is antialiased if the paint says so.
	- **Clip:** the clip region of each operation, scaled to device pixels
	  and rounded the same way, via `SkRegion` (13.2); nested lists are
	  clipped by their own regions and by the one of their DrawList.
	- **Images** come from ImageRegistry::Default(); **fonts** must be
	  added with AddFont() before a glyph run can be drawn (13.4); a run in
	  an unknown font is skipped.

	Window surfaces are CPU raster surfaces of the window's size; the CPU
	presenter of the platform layer shows them (specification 12).
*/
class SkiaBackend : public Backend {
public:
								SkiaBackend();
									/*!< A backend without surfaces, images or fonts. */
								~SkiaBackend() override;
									/*!< Detaches from ImageRegistry::Default(). */

	static	bool				Register(BackendRegistry& registry, int priority = 100);
									/*!< Adds "skia" to \a registry; false if it is there. */

			bool				AddFont(FontId font, std::span<const std::byte> fontFile,
									std::uint32_t faceIndex = 0) override;
			std::expected<std::vector<std::byte>, Error> EncodePng(const PixelData& pixels)
									override;
									/*!< SkPngEncoder with its default options. */
									/*!< Makes a typeface for \a font from the font file the
										text subsystem uses (SkFontMgr::makeFromData, a copy
										of the bytes); false if Skia cannot read it.
										Thread-safe. */

			Caps				Capabilities() const override;
									/*!< The core profile, paths, and GPU surfaces if
										Skia is built with OpenGL (phase G). */
			ImageHandle			CreateImage(const PixelData& pixels) override;
									/*!< An SkImage copy of \a pixels; 0 if invalid. */
			void				ReleaseImage(ImageHandle image) override;
									/*!< Frees the image. */
			SurfaceRef			CreateWindowSurface(const NativeWindow& window,
									SurfaceKind kind) override;
									/*!< A CPU raster surface of the window's size, or for
										SurfaceKind::Gpu one over the default framebuffer
										of the window's OpenGL context
										(NativeWindow::glProcAddress, current on this
										thread, which must also be current when the
										surface is resized, drawn and destroyed); nullptr
										if that fails or Skia has no OpenGL. */
			SurfaceRef			CreateOffscreenSurface(Size device, float scale) override;
									/*!< A CPU raster surface of \a device pixels, rounded. */
			void				ResizeSurface(Surface& surface, Size device,
									float scale) override;
									/*!< A new raster of the given size; the content is lost. */
			void				BeginFrame(Surface& surface, const Color& clear) override;
									/*!< Clears \a surface to \a clear. */
			void				Render(const DisplayList& list, Point offset) override;
									/*!< Draws \a list at \a offset (logical pixels). */
			void				EndFrame(Surface& surface) override;
									/*!< Ends the frame; a GPU surface's commands go to the
										GPU, the caller swaps the buffers. */

private:
			struct Data;

			std::unique_ptr<Data> fData;
};


} // namespace ngui::gfx
