/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <atomic>
#include <cstdint>
#include <optional>
#include <string>

#include <ngui/gfx/Backend.h>


namespace ngui::gfx {


/*!	A surface of the DumpBackend: it keeps the text of the last frame
	instead of pixels. */
class DumpSurface : public Surface {
public:
								DumpSurface(Size device, float scale);
									/*!< A surface of \a device pixels at \a scale. */

			Size				DeviceSize() const override;
									/*!< Size in device pixels. */
			float				Scale() const override;
									/*!< Device pixels per logical pixel. */
			SurfaceKind			Kind() const override;
									/*!< Always SurfaceKind::CpuRaster. */
			std::optional<CpuPixels> Pixels() const override;
									/*!< Nothing: the dump backend does not rasterize. */

			const std::string&	Text() const;
									/*!< The text of the last finished frame. */

private:
	friend class DumpBackend;

			Size				fDevice;
			float				fScale;
			std::string			fFrame;
			std::string			fText;
};


/*!	The dump backend (specification 15) behind the Backend interface:
	renders nothing, but writes every frame as text, a `frame` line with
	size, scale and clear color, then each rendered list with DumpText()
	after a `render` line with its offset. For snapshot tests without a GPU
	and for debugging; selectable as "dump" in BackendRegistry.
*/
class DumpBackend : public Backend {
public:
								DumpBackend() = default;
									/*!< A backend without surfaces. */
								~DumpBackend() override;
									/*!< Detaches from ImageRegistry::Default(). */

			Caps				Capabilities() const override;
									/*!< The core profile only. */
			ImageHandle			CreateImage(const PixelData& pixels) override;
									/*!< A new handle; the pixels are not kept. */
			void				ReleaseImage(ImageHandle image) override;
									/*!< Nothing to free. */
			SurfaceRef			CreateWindowSurface(const NativeWindow& window,
									SurfaceKind kind) override;
									/*!< nullptr: the dump backend has no windows. */
			SurfaceRef			CreateOffscreenSurface(Size device, float scale) override;
									/*!< A DumpSurface. */
			void				ResizeSurface(Surface& surface, Size device,
									float scale) override;
									/*!< Changes size and scale of a DumpSurface. */
			void				BeginFrame(Surface& surface, const Color& clear) override;
									/*!< Starts the text of a frame. */
			void				Render(const DisplayList& list, Point offset) override;
									/*!< Appends the dump of \a list. */
			void				EndFrame(Surface& surface) override;
									/*!< Makes the frame the surface's Text(). */

private:
			std::atomic<ImageHandle> fLastImage = 0;
			DumpSurface*		fCurrent = nullptr;
};


} // namespace ngui::gfx
