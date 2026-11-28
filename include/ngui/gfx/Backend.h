/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include <ngui/Error.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Size.h>


namespace ngui::gfx {


/*!	What a backend can do beyond the core profile (specification 12). */
struct Caps {
			bool				core = true;				/*!< Always true. */
			bool				gpuSurfaces = false;		/*!< SurfaceKind::Gpu (13.3). */
			bool				affineTransform = false;	/*!< Extended profile (16). */
			bool				pathClip = false;			/*!< Extended profile. */
			bool				bezierPaths = false;		/*!< Extended profile. */
			bool				concavePolygons = false;	/*!< Extended profile. */
			bool				radialGradient = false;		/*!< Extended profile. */
			bool				conicGradient = false;		/*!< Extended profile. */
			bool				blendModes = false;			/*!< Extended profile. */
			bool				offscreenTargets = false;	/*!< Extended profile. */
			bool				blurFilter = false;			/*!< Extended profile. */
			bool				linearBlending = false;		/*!< Blending::Linear
																(specification 24.1). */

			bool				operator==(const Caps& other) const = default;
									/*!< All flags equal. */
};


/*!	How a backend blends colors and interpolates gradients (specification
	4.3 and 24.1). */
enum class Blending : std::uint8_t {
	Encoded,	/*!< In the sRGB-encoded values, gamma-naive, as Haiku's app_server
					draws (the default). */
	Linear		/*!< In linear light: semi-transparent paint and antialiased edges as
					light physically adds up, only if Caps::linearBlending. The
					pixels stay sRGB-encoded. */
};


/*!	The kind of a window surface (specification 13.3). */
enum class SurfaceKind : std::uint8_t {
	CpuRaster,	/*!< Rendered by the CPU, shown by the CPU presenter. */
	Gpu			/*!< Rendered by the GPU, only if Caps::gpuSurfaces. */
};


/*!	An image as handed to a backend (specification 9): RGBA, 8 bits per
	component, **not** premultiplied, sRGB-encoded, rows from top to bottom.
	The backend converts it to its own format. Borrowed: whoever passes it
	keeps the bytes alive for the duration of the call.
*/
struct PixelData {
			std::int32_t		width;	/*!< Width in pixels, > 0. */
			std::int32_t		height;	/*!< Height in pixels, > 0. */
			std::size_t			stride;	/*!< Bytes per row, at least `width * 4`. */
			std::span<const std::uint8_t> bytes;
									/*!< `height` rows of `stride` bytes; the last row may
										end after `width * 4` bytes. */

			bool				IsValid() const;
									/*!< Positive extent, a sufficient stride, enough bytes. */
};


/*!	The pixel layout of a CPU surface, as the CPU presenter reads it. */
enum class PixelFormat : std::uint8_t {
	Rgba8Premul,	/*!< R, G, B, A bytes, premultiplied alpha. */
	Bgra8Premul		/*!< B, G, R, A bytes, premultiplied alpha (Skia's N32 on x86). */
};


/*!	Read access to the pixels of a CPU surface. */
struct CpuPixels {
			std::int32_t		width;	/*!< Width in device pixels. */
			std::int32_t		height;	/*!< Height in device pixels. */
			std::size_t			stride;	/*!< Bytes per row. */
			PixelFormat			format;	/*!< Byte order and alpha. */
			std::span<const std::byte> bytes;
									/*!< The pixels, valid until the next frame or resize. */
};


/*!	Where a backend renders: a window or an offscreen buffer
	(specification 12 and 13.3). The device size is in physical pixels; the
	scale is applied by the backend, a DisplayList does not know it
	(specification 4.2).
*/
class Surface {
public:
	virtual						~Surface() = default;

	virtual	Size				DeviceSize() const = 0;
									/*!< Size in device pixels. */
	virtual	float				Scale() const = 0;
									/*!< Device pixels per logical pixel, > 0. */
	virtual	SurfaceKind			Kind() const = 0;
									/*!< CPU or GPU. */
	virtual	std::optional<CpuPixels> Pixels() const = 0;
									/*!< The pixels of a CPU surface; nothing for a GPU
										surface or a backend that does not rasterize. */

			Size				LogicalSize() const;
									/*!< DeviceSize() divided by Scale(). */
};


/*!	A shared surface. */
using SurfaceRef = std::shared_ptr<Surface>;


/*!	The window a surface shows in. Defined by the platform layer (SDL3,
	GLFW, native handles; specification 12); the Canvas layer only passes it
	through. */
struct NativeWindow;


/*!	Renders DisplayLists (specification 12). A backend implements the whole
	core profile and passes the conformance tests (21); it skips operations
	it does not know and must not crash on them.

	Images: CreateImage() makes a resource of this backend and returns its
	own handle for it. The handles a View uses are the frontend-wide ones of
	ImageRegistry, which maps them to the handles of each backend; a backend
	looks them up with ImageRegistry::BackendImage(). A backend calls
	ImageRegistry::DetachBackend() in its destructor.

	Fonts: AddFont() gives the backend the font file behind a FontId, the
	same file the text subsystem shapes with (13.4); a glyph run in a font
	the backend does not know is skipped. BWindow adds the fonts of
	TextSystem::Default() before its first frame, and those the text system
	loads later (a fallback) before the frame that uses them.

	Threads: CreateImage(), ReleaseImage() and AddFont() are thread-safe;
	everything else runs on the render thread.
*/
class Backend {
public:
	virtual						~Backend() = default;

	virtual	Caps				Capabilities() const = 0;
									/*!< What this backend supports. */

	virtual	ImageHandle			CreateImage(const PixelData& pixels) = 0;
									/*!< A resource for \a pixels; 0 if it cannot be created.
										Thread-safe. */
	virtual	void				ReleaseImage(ImageHandle image) = 0;
									/*!< Frees the resource \a image. Thread-safe. */

	virtual	bool				AddFont(FontId font, std::span<const std::byte> fontFile,
									std::uint32_t faceIndex = 0);
									/*!< Makes face \a faceIndex of \a fontFile (0 unless it
										is a collection) the font of the glyph runs in
										\a font, replacing an earlier one; false if the
										backend cannot read it. The bytes are only borrowed
										for the call. By default false: a backend that does
										not draw text keeps it. Thread-safe. */
	virtual	std::expected<std::vector<std::byte>, Error> EncodePng(const PixelData& pixels);
									/*!< \a pixels as a PNG file, by the backend's encoder
										(specification 19.1, S4); NotSupported by default.
										Thread-safe. */

	virtual	void				SetBlending(Blending blending);
									/*!< How the frames begun from now on blend; a surface
										made for the other way is adapted at BeginFrame().
										Ignored by default: a backend without
										Caps::linearBlending blends Encoded. Render thread
										only. */
	virtual	SurfaceRef			CreateWindowSurface(const NativeWindow& window,
									SurfaceKind kind) = 0;
									/*!< A surface for \a window; nullptr if \a kind is not
										supported. Render thread only. */
	virtual	SurfaceRef			CreateOffscreenSurface(Size device, float scale) = 0;
									/*!< A CPU surface without a window, for tests. */
	virtual	void				ResizeSurface(Surface& surface, Size device, float scale) = 0;
									/*!< Changes size and scale of \a surface. */

	virtual	void				BeginFrame(Surface& surface, const Color& clear) = 0;
									/*!< Starts a frame on \a surface, cleared to \a clear. */
	virtual	void				Render(const DisplayList& list, Point offset) = 0;
									/*!< Renders \a list at \a offset into the current frame. */
	virtual	void				EndFrame(Surface& surface) = 0;
									/*!< Ends the frame and presents it. */
};


} // namespace ngui::gfx
