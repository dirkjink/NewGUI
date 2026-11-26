/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// GPU windows of the SDL platform (phase G): the main thread renders the
// windows' lists with Skia through each window's OpenGL context. SDL's
// offscreen driver makes its contexts with EGL, Mesa's llvmpipe in the CI;
// where there is no OpenGL the windows draw on the CPU and the tests that
// need a GPU window are skipped.


#include <ngui/platform/SdlPlatform.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Box.h>
#include <ngui/FlatLook.h>
#include <ngui/HaikuLook.h>
#include <ngui/Looper.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>

#include "look/LookGallery.h"
#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


// The pixels \a window shows, as an Image.
Image
Shown(PlatformWindow& window)
{
	const auto shown = window.ReadShownFrame();
	if (!shown.has_value())
		return {};
	return Image::FromCpuPixels({shown->width, shown->height,
		static_cast<std::size_t>(shown->width) * 4, gfx::PixelFormat::Rgba8Premul, shown->rgba});
}


TEST(SdlGpuTest, GpuWindowsShowTheirLists)
{
	// What is tested: With GPU rendering on and Skia registered: a window of
	// 240 × 136, the gallery of controls in the Haiku look handed over as a
	// list (PresentList()), then the one in the flat look; what the window
	// shows, read from its framebuffer (ReadShownFrame()).
	// Success criteria: The window draws lists (DrawsLists()); it shows
	// each gallery at its size in device pixels, looking like the same
	// rendered on the CPU (CompareRasterizers()), the second replacing the
	// first. Skipped where there is no OpenGL.
	// Why it matters: This is the path of every frame of a GPU window
	// (phase G): the window thread records, the main thread renders and
	// swaps.
	//
	// Given
	(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	app.Platform()->SetGpuRendering(true);
	WindowSpec spec;
	spec.title = "gpu";
	spec.frame = {10, 20, 250, 156};
	auto created = app.Platform()->CreateWindow(spec);
	ASSERT_TRUE(created.has_value()) << created.error().message;
	std::unique_ptr<PlatformWindow> window = std::move(*created);
	if (!window->DrawsLists())
		GTEST_SKIP() << "no OpenGL for the offscreen driver";
	window->Show();
	gfx::SkiaBackend cpu;

	for (const auto& look : {CreateHaikuLook(), CreateFlatLook()}) {
		// When
		const gfx::DisplayListRef gallery = LookGallery(*look);
		ASSERT_TRUE(window->PresentList(gallery, {1, 1, 1, 1}).has_value());
		const Image shown = Shown(*window);
		const auto reference = RenderToImage(cpu, *gallery, window->Scale());

		// Then
		ASSERT_TRUE(reference.has_value()) << reference.error().message;
		EXPECT_EQ(shown.Width(), reference->Width());
		EXPECT_EQ(shown.Height(), reference->Height());
		const RasterizerComparison alike = CompareRasterizers(*reference, shown);
		EXPECT_TRUE(alike.passed) << look->Info().id << ": " << alike.Describe();
	}
	window.reset();
}


TEST(SdlGpuTest, WithoutOpenGLWindowsDrawOnTheCpu)
{
	// What is tested: GPU rendering asked for with NGUI_RENDERER=gpu while
	// SDL is told to load OpenGL and EGL from files that do not exist; a
	// window made then and a frame presented to it.
	// Success criteria: The platform takes the variable; the window draws
	// on the CPU (no lists), Present() of pixels works, PresentList() is
	// NotSupported.
	// Why it matters: The GPU is a request, not a requirement: a program
	// started with it on a machine without OpenGL must still show its
	// windows.
	//
	// Given
	const ScopedEnvironment renderer("NGUI_RENDERER", "gpu");
	const ScopedEnvironment gl("SDL_OPENGL_LIBRARY", "/nonexistent/libGL.so");
	const ScopedEnvironment egl("SDL_EGL_LIBRARY", "/nonexistent/libEGL.so");
	(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	WindowSpec spec;
	spec.title = "cpu";
	spec.frame = {10, 20, 40, 30};

	// When
	auto created = app.Platform()->CreateWindow(spec);
	ASSERT_TRUE(created.has_value()) << created.error().message;
	std::unique_ptr<PlatformWindow> window = std::move(*created);
	window->Show();
	std::vector<std::byte> pixels(30 * 10 * 4, std::byte{255});
	const std::array<gfx::IntRect, 1> all = {{{0, 0, 30, 10}}};
	const auto presented = window->Present({30, 10, 120, gfx::PixelFormat::Bgra8Premul, pixels},
		all);
	const auto list = window->PresentList(nullptr, {1, 1, 1, 1});

	// Then
	EXPECT_TRUE(app.Platform()->GpuRendering());
	EXPECT_FALSE(window->DrawsLists());
	EXPECT_TRUE(presented.has_value());
	ASSERT_FALSE(list.has_value());
	EXPECT_EQ(list.error().code, ErrorCode::NotSupported);
	window.reset();
}


// A window of 200 × 100 with a box around a red and a blue area, for
// CaptureFrame().
class GpuWindow : public BWindow {
public:
	GpuWindow()
		:
		BWindow({30, 40, 230, 140}, "gpu", BWindowLook::Titled)
	{
		AddChild(std::make_unique<BBox>(gfx::Rect{10, 10, 190, 90}, "box"));
		auto red = std::make_unique<BView>(gfx::Rect{20, 20, 100, 80}, "red", kFollowLeftTop,
			BViewFlag::WillDraw);
		red->SetViewColor(gfx::Color{1, 0, 0, 1});
		AddChild(std::move(red));
		auto blue = std::make_unique<BView>(gfx::Rect{110, 20, 180, 80}, "blue", kFollowLeftTop,
			BViewFlag::WillDraw);
		blue->SetViewColor(gfx::Color{0, 0, 1, 1});
		AddChild(std::move(blue));
	}
};


TEST(SdlGpuTest, AWindowShowsOnTheGpuWhatItDraws)
{
	// What is tested: A BWindow with a red and a blue area in a box, in an
	// application on the SDL platform with GPU rendering on: what it shows
	// (CaptureFrame(): read from the framebuffer) against what it draws
	// (Snapshot(): rendered anew on the CPU).
	// Success criteria: Both have the window's size in device pixels and
	// look alike as two rasterizers' images (CompareRasterizers()).
	// Skipped where there is no OpenGL.
	// Why it matters: The window hands its frames to the main thread instead
	// of rendering them; what reaches the screen must be what the window
	// drew.
	//
	// Given
	(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	app.Platform()->SetGpuRendering(true);
	{
		WindowSpec probe;
		probe.frame = {0, 0, 10, 10};
		auto created = app.Platform()->CreateWindow(probe);
		ASSERT_TRUE(created.has_value()) << created.error().message;
		if (!(*created)->DrawsLists())
			GTEST_SKIP() << "no OpenGL for the offscreen driver";
	}
	auto owned = std::make_unique<GpuWindow>();
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	// a frame shown first: the window draws when it is shown
	auto shown = BWindow::CaptureFrame(window).get();
	for (int i = 0; i < 100 && !shown.has_value(); i++) {
		std::this_thread::sleep_for(10ms);
		shown = BWindow::CaptureFrame(window).get();
	}
	const auto drawn = BWindow::Snapshot(window, {}).get();
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);

	// Then
	ASSERT_TRUE(shown.has_value()) << shown.error().message;
	ASSERT_TRUE(drawn.has_value()) << drawn.error().message;
	const Image onScreen = Image::FromBitmap(*shown);
	const Image rendered = Image::FromBitmap(*drawn);
	EXPECT_EQ(onScreen.Width(), rendered.Width());
	EXPECT_EQ(onScreen.Height(), rendered.Height());
	const RasterizerComparison alike = CompareRasterizers(rendered, onScreen);
	EXPECT_TRUE(alike.passed) << alike.Describe();
}


} // namespace
