/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Blending in linear light with the Skia backend (specification 24.1):
// kSRGBA_8888 surfaces in linear sRGB, the CPU presenter's format read back,
// and the application's choice reaching the windows.


#include <ngui/gfx/SkiaBackend.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/LinearGradient.h>
#include <ngui/test/Driver.h>
#include <ngui/test/Image.h>

#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;
using namespace std::chrono_literals;


// White, black at half opacity over it (left), a gradient from black to
// white (right, its middle in the middle row): 20 × 11.
DisplayListRef
HalfBlackAndGradient()
{
	const std::array<GradientStop, 2> gray = {{{0, {0, 0, 0, 1}}, {1, {1, 1, 1, 1}}}};
	Canvas canvas({0, 0, 20, 11});
	canvas.FillRect({0, 0, 10, 11}, {.color = {0, 0, 0, 0.5f}});
	canvas.FillRect({10, 0, 20, 11},
		{.gradient = *LinearGradient::Create({0, 0}, {0, 11}, gray)});
	return canvas.Finish();
}


// \a list drawn by \a backend into \a surface on \a clear, its pixels read
// as the CPU presenter reads them.
Image
Draw(SkiaBackend& backend, Surface& surface, const DisplayList& list, const Color& clear)
{
	backend.BeginFrame(surface, clear);
	backend.Render(list, {0, 0});
	backend.EndFrame(surface);
	const std::optional<CpuPixels> pixels = surface.Pixels();
	return pixels.has_value() ? Image::FromCpuPixels(*pixels) : Image();
}


bool
Near(const Rgba8& pixel, int r, int g, int b, int a, int tolerance = 2)
{
	return std::abs(pixel.r - r) <= tolerance && std::abs(pixel.g - g) <= tolerance
		&& std::abs(pixel.b - b) <= tolerance && std::abs(pixel.a - a) <= tolerance;
}


TEST(SkiaBlendingTest, LinearLightAddsUpAsLightDoes)
{
	// What is tested: Black at half opacity over white and a gradient from
	// black to white, drawn by Skia with Blending::Encoded and with
	// Blending::Linear; an opaque mid gray (0.5) and red on their own.
	// Success criteria: Encoded gives 128 for both, as Haiku's app_server;
	// Linear gives 188, half the light in linear light, sRGB-encoded (Haiku's
	// 8-bit icon tables give 186); opaque colors keep their values either
	// way (0.5 is 128, red is 255, 0, 0). Skia reports linear blending.
	// Why it matters: Blending in linear light changes how paint adds up,
	// not the colors an application asks for (specification 24.1).
	//
	// Given
	const DisplayListRef list = HalfBlackAndGradient();
	Canvas opaque({0, 0, 2, 1});
	opaque.FillRect({0, 0, 1, 1}, {.color = {0.5f, 0.5f, 0.5f, 1}});
	opaque.FillRect({1, 0, 2, 1}, {.color = {1, 0, 0, 1}});
	const DisplayListRef colors = opaque.Finish();
	SkiaBackend backend;

	// When
	const SurfaceRef encodedSurface = backend.CreateOffscreenSurface({20, 11}, 1);
	const Image encoded = Draw(backend, *encodedSurface, *list, {1, 1, 1, 1});
	backend.SetBlending(Blending::Linear);
	const SurfaceRef linearSurface = backend.CreateOffscreenSurface({20, 11}, 1);
	const Image linear = Draw(backend, *linearSurface, *list, {1, 1, 1, 1});
	const SurfaceRef colorSurface = backend.CreateOffscreenSurface({2, 1}, 1);
	const Image linearColors = Draw(backend, *colorSurface, *colors, {1, 1, 1, 1});

	// Then
	EXPECT_TRUE(backend.Capabilities().linearBlending);
	EXPECT_TRUE(Near(encoded.At(5, 5), 128, 128, 128, 255));
	EXPECT_TRUE(Near(encoded.At(15, 5), 128, 128, 128, 255));
	EXPECT_TRUE(Near(linear.At(5, 5), 188, 188, 188, 255));
	EXPECT_TRUE(Near(linear.At(15, 5), 188, 188, 188, 255));
	EXPECT_TRUE(Near(linearColors.At(0, 0), 128, 128, 128, 255, 1));
	EXPECT_EQ(linearColors.At(1, 0), (Rgba8{255, 0, 0, 255}));
}


TEST(SkiaBlendingTest, ThePresenterGetsItsFormat)
{
	// What is tested: White at half opacity on a transparent surface (as on
	// a window whose frame NewGUI draws), with Blending::Linear, read through
	// Pixels().
	// Success criteria: 128, 128, 128 at alpha 128: premultiplied in the
	// sRGB-encoded values, the CPU presenter's format, although the surface
	// premultiplies in linear light (its bytes would say 188).
	// Why it matters: The presenter and the window system composite these
	// pixels; they must be in the format every other surface hands them.
	//
	// Given
	Canvas canvas({0, 0, 4, 4});
	canvas.FillRect({0, 0, 4, 4}, {.color = {1, 1, 1, 0.5f}});
	const DisplayListRef list = canvas.Finish();
	SkiaBackend backend;
	backend.SetBlending(Blending::Linear);
	const SurfaceRef surface = backend.CreateOffscreenSurface({4, 4}, 1);

	// When
	backend.BeginFrame(*surface, {0, 0, 0, 0});
	backend.Render(*list, {0, 0});
	backend.EndFrame(*surface);
	const std::optional<CpuPixels> pixels = surface->Pixels();

	// Then: the bytes themselves (an Image would unpremultiply them, and
	// 188 at alpha 128 would come out white as well)
	ASSERT_TRUE(pixels.has_value());
	const std::size_t at = pixels->stride + 4;
	const auto byte = [&pixels, at](std::size_t i) {
		return static_cast<int>(pixels->bytes[at + i]);
	};
	EXPECT_NEAR(byte(0), 128, 1);
	EXPECT_NEAR(byte(1), 128, 1);
	EXPECT_NEAR(byte(2), 128, 1);
	EXPECT_NEAR(byte(3), 128, 1);
}


TEST(SkiaBlendingTest, ASurfaceFollowsTheBlending)
{
	// What is tested: One offscreen surface made with Blending::Encoded,
	// drawn into after SetBlending(Linear), then after SetBlending(Encoded).
	// Success criteria: The half black is 188 in the second frame and 128
	// in the third; the surface keeps its size.
	// Why it matters: Windows keep their surfaces; switching the way of
	// blending at run time must reach them at their next frame.
	//
	// Given
	const DisplayListRef list = HalfBlackAndGradient();
	SkiaBackend backend;
	const SurfaceRef surface = backend.CreateOffscreenSurface({20, 11}, 1);

	// When
	backend.SetBlending(Blending::Linear);
	const Image linear = Draw(backend, *surface, *list, {1, 1, 1, 1});
	backend.SetBlending(Blending::Encoded);
	const Image encoded = Draw(backend, *surface, *list, {1, 1, 1, 1});

	// Then
	EXPECT_TRUE(Near(linear.At(5, 5), 188, 188, 188, 255));
	EXPECT_TRUE(Near(encoded.At(5, 5), 128, 128, 128, 255));
	EXPECT_EQ(surface->DeviceSize(), (Size{20, 11}));
}


// A view of white with black at half opacity over its left half.
class HalfBlackView : public BView {
public:
	HalfBlackView()
		:
		BView({0, 0, 40, 20}, "half", kFollowLeftTop, BViewFlag::WillDraw)
	{
	}

	void Draw(DrawContext& context) override
	{
		context.canvas.FillRect({0, 0, 40, 20}, {.color = {1, 1, 1, 1}});
		context.canvas.FillRect({0, 0, 20, 20}, {.color = {0, 0, 0, 0.5f}});
	}
};


class HalfBlackWindow : public BWindow {
public:
	HalfBlackWindow()
		:
		BWindow({100, 100, 140, 120}, "Half", BWindowLook::Titled)
	{
		(void)UseBackend(std::make_unique<SkiaBackend>());
		AddChild(std::make_unique<HalfBlackView>());
	}
};


// The red of the window's last frame at (10, 10), as the headless platform
// has it.
std::optional<int>
ShownRed(Driver& driver)
{
	for (platform::HeadlessWindow* window : driver.Platform().Windows()) {
		const auto frame = window->LastFrame();
		if (window->IsPopup() || frame == nullptr)
			continue;
		const Image image = Image::FromCpuPixels({frame->width, frame->height, frame->stride,
			frame->format, frame->bytes});
		return image.At(10, 10).r;
	}
	return std::nullopt;
}


TEST(SkiaBlendingTest, WindowsFollowTheApplication)
{
	// What is tested: An application started with NGUI_BLENDING=linear, a
	// window drawing black at half opacity over white with Skia on the
	// headless platform; then BApplication::SetBlending(Encoded) while it
	// is open.
	// Success criteria: Blending() is Linear and the window shows 188; after
	// SetBlending(Encoded) it shows 128 without being touched otherwise,
	// and Blending() is Encoded.
	// Why it matters: The choice is the application's (process-wide), from
	// the environment or the API, and switching it reaches every open
	// window at once.
	//
	// Given
	const ScopedEnvironment variable("NGUI_BLENDING", "linear");
	BApplication app("application/x-vnd.ngui-blending-test");
	Driver driver(app);
	auto window = std::make_unique<HalfBlackWindow>();
	window->Show();
	const BMessenger shown = *BLooper::Run(std::move(window));
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// When
	const gfx::Blending started = BApplication::Blending();
	const std::optional<int> linear = ShownRed(driver);
	BApplication::SetBlending(gfx::Blending::Encoded);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::optional<int> encoded = ShownRed(driver);

	// Then
	EXPECT_EQ(started, gfx::Blending::Linear);
	ASSERT_TRUE(linear.has_value() && encoded.has_value());
	EXPECT_NEAR(*linear, 188, 2);
	EXPECT_NEAR(*encoded, 128, 2);
	EXPECT_EQ(BApplication::Blending(), gfx::Blending::Encoded);
	(void)shown.SendMessage(kQuitRequested);
	(void)shown.WaitForLooperQuit(5s);
}


} // namespace
