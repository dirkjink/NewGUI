/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The Skia backend on a GPU surface (phase G, conformance level G): an
// OpenGL context of EGL over a pbuffer, without a window, as Mesa offers
// it on machines without a GPU (llvmpipe). Without EGL and OpenGL the tests
// are skipped.


#include <ngui/gfx/SkiaBackend.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gtest/gtest.h>

#include <ngui/BeLook.h>
#include <ngui/FlatLook.h>
#include <ngui/HaikuLook.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/gfx/NativeWindow.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>
#include <ngui/text/FontTextSystem.h>

#include "look/LookGallery.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


// An OpenGL context of \a width × \a height pixels over an EGL pbuffer,
// current on this thread while it lives; Valid() is false if EGL or
// desktop OpenGL is missing.
class PbufferContext {
public:
	PbufferContext(int width, int height)
	{
		// Mesa's surfaceless platform needs no display server
		const auto platformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
			eglGetProcAddress("eglGetPlatformDisplayEXT"));
		if (platformDisplay != nullptr)
			fDisplay = platformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
		if (fDisplay == EGL_NO_DISPLAY)
			fDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (fDisplay == EGL_NO_DISPLAY || eglInitialize(fDisplay, nullptr, nullptr) != EGL_TRUE
			|| eglBindAPI(EGL_OPENGL_API) != EGL_TRUE) {
			return;
		}
		const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
			EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8, EGL_STENCIL_SIZE, 8, EGL_NONE};
		EGLConfig config = nullptr;
		EGLint count = 0;
		if (eglChooseConfig(fDisplay, attributes, &config, 1, &count) != EGL_TRUE || count < 1)
			return;
		const EGLint size[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
		fSurface = eglCreatePbufferSurface(fDisplay, config, size);
		fContext = eglCreateContext(fDisplay, config, EGL_NO_CONTEXT, nullptr);
		fValid = fSurface != EGL_NO_SURFACE && fContext != EGL_NO_CONTEXT
			&& eglMakeCurrent(fDisplay, fSurface, fSurface, fContext) == EGL_TRUE;
	}

	~PbufferContext()
	{
		if (fDisplay == EGL_NO_DISPLAY)
			return;
		eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (fContext != EGL_NO_CONTEXT)
			eglDestroyContext(fDisplay, fContext);
		if (fSurface != EGL_NO_SURFACE)
			eglDestroySurface(fDisplay, fSurface);
		eglTerminate(fDisplay);
	}

	PbufferContext(const PbufferContext&) = delete;
	PbufferContext& operator=(const PbufferContext&) = delete;

	bool Valid() const
	{
		return fValid;
	}

	// The window a backend sees: no native handles, the pbuffer's size and
	// eglGetProcAddress() for the functions of OpenGL.
	static NativeWindow Window(float width, float height, float scale)
	{
		NativeWindow window;
		window.size = {width / scale, height / scale};
		window.scale = scale;
		window.glProcAddress = [](const char* name) {
			return reinterpret_cast<NativeWindow::GlFunction>(eglGetProcAddress(name));
		};
		return window;
	}

private:
	EGLDisplay fDisplay = EGL_NO_DISPLAY;
	EGLSurface fSurface = EGL_NO_SURFACE;
	EGLContext fContext = EGL_NO_CONTEXT;
	bool fValid = false;
};


// What the default framebuffer of the current OpenGL context holds, read
// with glReadPixels() past Skia, top row first: what a window would show.
Image
ReadFramebuffer(int width, int height)
{
	using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
	using Finish = void (*)();
	using BindFramebuffer = void (*)(unsigned, unsigned);
	using BindBuffer = void (*)(unsigned, unsigned);
	using PixelStore = void (*)(unsigned, int);
	constexpr unsigned kRgba = 0x1908;			// GL_RGBA
	constexpr unsigned kUnsignedByte = 0x1401;	// GL_UNSIGNED_BYTE
	constexpr unsigned kFramebuffer = 0x8D40;	// GL_FRAMEBUFFER
	constexpr unsigned kPixelPackBuffer = 0x88EB;	// GL_PIXEL_PACK_BUFFER
	constexpr unsigned kPackRowLength = 0x0D02;	// GL_PACK_ROW_LENGTH
	constexpr unsigned kPackAlignment = 0x0D05;	// GL_PACK_ALIGNMENT
	const auto read = reinterpret_cast<ReadPixels>(eglGetProcAddress("glReadPixels"));
	const auto finish = reinterpret_cast<Finish>(eglGetProcAddress("glFinish"));
	const auto bindFramebuffer = reinterpret_cast<BindFramebuffer>(
		eglGetProcAddress("glBindFramebuffer"));
	const auto bindBuffer = reinterpret_cast<BindBuffer>(eglGetProcAddress("glBindBuffer"));
	const auto pixelStore = reinterpret_cast<PixelStore>(eglGetProcAddress("glPixelStorei"));
	if (read == nullptr || finish == nullptr || bindFramebuffer == nullptr
		|| bindBuffer == nullptr || pixelStore == nullptr) {
		return {};
	}
	// the state Skia may have left
	bindFramebuffer(kFramebuffer, 0);
	bindBuffer(kPixelPackBuffer, 0);
	pixelStore(kPackRowLength, 0);
	pixelStore(kPackAlignment, 4);
	finish();
	const auto stride = static_cast<std::size_t>(width) * 4;
	std::vector<std::byte> bottomUp(stride * static_cast<std::size_t>(height));
	read(0, 0, width, height, kRgba, kUnsignedByte, bottomUp.data());
	std::vector<std::byte> topDown(bottomUp.size());
	for (int y = 0; y < height; y++) {
		std::copy_n(bottomUp.begin() + static_cast<std::ptrdiff_t>(stride)
			* (height - 1 - y), stride, topDown.begin() + static_cast<std::ptrdiff_t>(stride) * y);
	}
	return Image::FromCpuPixels({width, height, stride, PixelFormat::Rgba8Premul, topDown});
}


// \a list rendered by \a backend into its GPU surface \a surface: the
// framebuffer as a window would show it; with \a readBack what the surface
// reads back.
Image
RenderOnGpu(SkiaBackend& backend, Surface& surface, const DisplayList& list,
	Image* readBack = nullptr)
{
	backend.BeginFrame(surface, {1, 1, 1, 1});
	backend.Render(list, {-list.Bounds().x0, -list.Bounds().y0});
	backend.EndFrame(surface);
	if (readBack != nullptr) {
		const std::optional<CpuPixels> pixels = surface.Pixels();
		*readBack = pixels.has_value() ? Image::FromCpuPixels(*pixels) : Image();
	}
	const Size device = surface.DeviceSize();
	return ReadFramebuffer(static_cast<int>(device.w), static_cast<int>(device.h));
}


TEST(SkiaGpuTest, TheGalleriesLookAsOnTheCpu)
{
	// What is tested: The gallery of controls (test::LookGallery(), 240 ×
	// 136) in the BeOS, Haiku and flat looks, rendered by Skia on a GPU
	// surface over an OpenGL context of EGL (Mesa's llvmpipe in the CI) at
	// scale 1 and 2, its framebuffer read with glReadPixels() and through
	// the surface, against the same rendered on a CPU raster surface.
	// Success criteria: The backend reports GPU surfaces and makes one of
	// kind Gpu over the context's framebuffer, of the window's size in
	// device pixels. The framebuffer (as a window shows it, the right way
	// up) equals what the surface reads back (Pixels()); each image looks
	// like its CPU reference as two rasterizers' images do
	// (CompareRasterizers(), conformance level G, 21.2 and 21.4). Skipped
	// without EGL and OpenGL, or if Skia is built without OpenGL.
	// Why it matters: The GPU must draw what the reference images show;
	// differences are allowed only where rasterizers differ (edges,
	// gradients).
	//
	for (const float scale : {1.0f, 2.0f}) {
		// Given
		const int width = static_cast<int>(240 * scale);
		const int height = static_cast<int>(136 * scale);
		const PbufferContext context(width, height);
		if (!context.Valid())
			GTEST_SKIP() << "no OpenGL context of EGL";
		SkiaBackend backend;
		if (!backend.Capabilities().gpuSurfaces)
			GTEST_SKIP() << "Skia is built without OpenGL";
		SkiaBackend cpu;
		const SurfaceRef surface = backend.CreateWindowSurface(
			PbufferContext::Window(static_cast<float>(width), static_cast<float>(height),
				scale), SurfaceKind::Gpu);
		ASSERT_NE(surface, nullptr);

		for (const auto& look : {CreateBeLook(), CreateHaikuLook(), CreateFlatLook()}) {
			// When
			const DisplayListRef list = LookGallery(*look);
			Image readBack;
			const Image gpu = RenderOnGpu(backend, *surface, *list, &readBack);
			const auto reference = RenderToImage(cpu, *list, scale);

			// Then
			ASSERT_TRUE(reference.has_value()) << reference.error().message;
			EXPECT_TRUE(CompareImages(gpu, readBack, Tolerance::Exact()).passed)
				<< look->Info().id << " at scale " << scale;
			const RasterizerComparison alike = CompareRasterizers(*reference, gpu);
			EXPECT_TRUE(alike.passed) << look->Info().id << " at scale " << scale << ": "
				<< alike.Describe();
		}
		EXPECT_TRUE(backend.Capabilities().gpuSurfaces);
		EXPECT_EQ(surface->Kind(), SurfaceKind::Gpu);
		EXPECT_EQ(surface->DeviceSize(),
			(Size{static_cast<float>(width), static_cast<float>(height)}));
	}
}


TEST(SkiaGpuTest, TextAndImagesLookAsOnTheCpu)
{
	// What is tested: Two lines of text in the bundled Noto Sans (plain and
	// bold, shaped by the text subsystem) and an image of 32 × 32 pixels
	// (a color ramp, registered with ImageRegistry::Default()) drawn 1:1,
	// enlarged to 64 × 48 and at half alpha, on 240 × 100, rendered by Skia
	// on a GPU surface at scale 1 and 2 (its framebuffer read with
	// glReadPixels()) and on a CPU raster surface.
	// Success criteria: Each GPU image looks like its CPU reference as two
	// rasterizers' images do (CompareRasterizers()). Skipped without EGL
	// and OpenGL, or if Skia is built without OpenGL.
	// Why it matters: On the GPU, glyphs come from Skia's glyph atlas and
	// images from textures, paths of their own that the galleries do not
	// take.
	//
	const auto text = std::move(*ngui::text::FontTextSystem::CreateBundled());
	std::vector<std::uint8_t> ramp(32 * 32 * 4);
	for (std::size_t i = 0; i < 32 * 32; i++) {
		ramp[i * 4] = static_cast<std::uint8_t>(i % 32 * 8);
		ramp[i * 4 + 1] = static_cast<std::uint8_t>(i / 32 * 8);
		ramp[i * 4 + 2] = 128;
		ramp[i * 4 + 3] = 255;
	}
	ImageRegistry& registry = ImageRegistry::Default();
	const ImageHandle image = *registry.Register({32, 32, 32 * 4, ramp});
	Canvas canvas({0, 0, 240, 100});
	canvas.FillRect({0, 0, 240, 100}, {.color = {1, 1, 1, 1}});
	text->Shape("Haiku's Interface Kit on the GPU", FontRole::Plain).Draw(canvas, {4, 16},
		{.color = {0, 0, 0, 1}});
	text->Shape("Quick brown fox", FontRole::Bold).Draw(canvas, {4, 36},
		{.color = {0.2f, 0.2f, 0.6f, 1}});
	canvas.DrawImage(image, {0, 0, 32, 32}, {4, 48, 36, 80});
	canvas.DrawImage(image, {0, 0, 32, 32}, {44, 46, 108, 94});
	canvas.DrawImage(image, {0, 0, 32, 32}, {116, 48, 148, 80}, 0.5f);
	const DisplayListRef list = canvas.Finish();
	registry.Release(image);

	for (const float scale : {1.0f, 2.0f}) {
		// Given
		const int width = static_cast<int>(240 * scale);
		const int height = static_cast<int>(100 * scale);
		const PbufferContext context(width, height);
		if (!context.Valid())
			GTEST_SKIP() << "no OpenGL context of EGL";
		SkiaBackend backend;
		if (!backend.Capabilities().gpuSurfaces)
			GTEST_SKIP() << "Skia is built without OpenGL";
		SkiaBackend cpu;
		for (const FontFile& font : text->Fonts()) {
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
			ASSERT_TRUE(cpu.AddFont(font.id, font.data));
		}
		const SurfaceRef surface = backend.CreateWindowSurface(
			PbufferContext::Window(static_cast<float>(width), static_cast<float>(height),
				scale), SurfaceKind::Gpu);
		ASSERT_NE(surface, nullptr);

		// When
		const Image gpu = RenderOnGpu(backend, *surface, *list);
		const auto reference = RenderToImage(cpu, *list, scale);

		// Then
		ASSERT_TRUE(reference.has_value()) << reference.error().message;
		const RasterizerComparison alike = CompareRasterizers(*reference, gpu);
		EXPECT_TRUE(alike.passed) << "at scale " << scale << ": " << alike.Describe();
	}
}


TEST(SkiaGpuTest, TheGpuBlendsInLinearLightToo)
{
	// What is tested: Black at half opacity over white on a GPU surface over
	// an OpenGL context of EGL, with Blending::Linear, then Encoded, on the
	// same surface; the framebuffer read with glReadPixels() and through
	// the surface.
	// Success criteria: With Linear both show 188 (half the light in linear
	// light, sRGB-encoded), with Encoded 128; the surface stays of kind Gpu.
	// Skipped without EGL and OpenGL, or if Skia is built without OpenGL.
	// Why it matters: Windows that render on the GPU (phase G) must blend as
	// the application chose, as on the CPU (specification 24.1).
	//
	// Given
	const PbufferContext context(8, 8);
	if (!context.Valid())
		GTEST_SKIP() << "no OpenGL context of EGL";
	SkiaBackend backend;
	if (!backend.Capabilities().gpuSurfaces)
		GTEST_SKIP() << "Skia is built without OpenGL";
	const SurfaceRef surface = backend.CreateWindowSurface(PbufferContext::Window(8, 8, 1),
		SurfaceKind::Gpu);
	ASSERT_NE(surface, nullptr);
	Canvas canvas({0, 0, 8, 8});
	canvas.FillRect({0, 0, 8, 8}, {.color = {0, 0, 0, 0.5f}});
	const DisplayListRef list = canvas.Finish();

	// When
	backend.SetBlending(Blending::Linear);
	Image linearBack;
	const Image linear = RenderOnGpu(backend, *surface, *list, &linearBack);
	backend.SetBlending(Blending::Encoded);
	Image encodedBack;
	const Image encoded = RenderOnGpu(backend, *surface, *list, &encodedBack);

	// Then
	EXPECT_NEAR(linear.At(4, 4).r, 188, 2);
	EXPECT_NEAR(linearBack.At(4, 4).r, 188, 2);
	EXPECT_NEAR(encoded.At(4, 4).r, 128, 2);
	EXPECT_NEAR(encodedBack.At(4, 4).r, 128, 2);
	EXPECT_EQ(surface->Kind(), SurfaceKind::Gpu);
}


TEST(SkiaGpuTest, WithoutOpenGLThereIsNoGpuSurface)
{
	// What is tested: CreateWindowSurface() for SurfaceKind::Gpu with a
	// window without glProcAddress.
	// Success criteria: nullptr: the caller falls back to a CPU surface.
	// Why it matters: The GPU is optional (phase G); a window without an
	// OpenGL context must not get a surface it cannot draw into.
	//
	// Given
	SkiaBackend backend;
	NativeWindow window;
	window.size = {100, 50};

	// When / Then
	EXPECT_EQ(backend.CreateWindowSurface(window, SurfaceKind::Gpu), nullptr);
	EXPECT_NE(backend.CreateWindowSurface(window, SurfaceKind::CpuRaster), nullptr);
}


} // namespace
