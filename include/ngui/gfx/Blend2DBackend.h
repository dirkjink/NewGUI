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


/*!	The Blend2D backend (specification 14): a second, independent CPU
	backend that proves the canvas layer is exchangeable (conformance level
	C1×, 21.4); renders DisplayLists with Blend2D into PRGB32 images
	(premultiplied, blending in the encoded space, 4.3). Part of the library
	ngui_blend2d, built with NGUI_WITH_BLEND2D; this header includes no
	Blend2D header.

	- **A1:** axis-parallel rectangles, rectangle frames and axis-parallel
	  lines are filled in device pixels with edges rounded to whole pixels
	  after scaling (`floor(v + 0.5)`), so Blend2D's analytic antialiasing
	  covers them fully; frames as four rectangles. They are pixel for pixel
	  the same as with the Skia backend.
	- **A2:** everything else is antialiased (Blend2D always is, so
	  Paint::antialias false is not honoured).
	- **Clip:** Blend2D clips to one rectangle only; an operation is drawn
	  once for each rectangle of its clip region (fan-out, 14.2).
	- **Gradients** are interpolated without premultiplication
	  (specification 8): stops of different opacity are subdivided, since
	  Blend2D interpolates premultiplied.
	- **Images** come from ImageRegistry::Default(); **fonts** must be
	  added with AddFont() before a glyph run can be drawn; a run in an
	  unknown font is skipped.

	Window surfaces are CPU images of the window's size; the CPU presenter
	of the platform layer shows them (specification 12).
*/
class Blend2DBackend : public Backend {
public:
								Blend2DBackend();
									/*!< A backend without surfaces, images or fonts. */
								~Blend2DBackend() override;
									/*!< Detaches from ImageRegistry::Default(). */

	static	bool				Register(BackendRegistry& registry, int priority = 50);
									/*!< Adds "blend2d" to \a registry, after Skia (100) by
										default; false if it is there. */

			bool				AddFont(FontId font, std::span<const std::byte> fontFile,
									std::uint32_t faceIndex = 0) override;
									/*!< Makes a font face for \a font from the font file the
										text subsystem uses (a copy of the bytes); false if
										Blend2D cannot read it. Thread-safe. */
			std::expected<std::vector<std::byte>, Error> EncodePng(const PixelData& pixels)
									override;
									/*!< Blend2D's PNG codec. */

			Caps				Capabilities() const override;
									/*!< The core profile, Bézier paths and concave polygons. */
			ImageHandle			CreateImage(const PixelData& pixels) override;
									/*!< A premultiplied copy of \a pixels; 0 if invalid. */
			void				ReleaseImage(ImageHandle image) override;
									/*!< Frees the image. */
			SurfaceRef			CreateWindowSurface(const NativeWindow& window,
									SurfaceKind kind) override;
									/*!< A CPU image of the window's size; nullptr for
										SurfaceKind::Gpu. */
			SurfaceRef			CreateOffscreenSurface(Size device, float scale) override;
									/*!< A CPU image of \a device pixels, rounded. */
			void				ResizeSurface(Surface& surface, Size device,
									float scale) override;
									/*!< A new image of the given size; the content is lost. */
			void				BeginFrame(Surface& surface, const Color& clear) override;
									/*!< Clears \a surface to \a clear. */
			void				Render(const DisplayList& list, Point offset) override;
									/*!< Draws \a list at \a offset (logical pixels). */
			void				EndFrame(Surface& surface) override;
									/*!< Ends the frame. */

private:
			struct Data;

			std::unique_ptr<Data> fData;
};


} // namespace ngui::gfx
