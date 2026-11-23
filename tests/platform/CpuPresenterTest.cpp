/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/CpuPresenter.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/DumpBackend.h>
#include <ngui/gfx/NativeWindow.h>


namespace {


using namespace ngui;
using namespace ngui::platform;


// 4 × 3 pixels, each (x, y, 10 * x + y, 255) in its byte order.
std::vector<std::byte>
Numbered(std::size_t stride)
{
	std::vector<std::byte> bytes(stride * 3, std::byte{0xee});
	for (std::size_t y = 0; y < 3; y++) {
		for (std::size_t x = 0; x < 4; x++) {
			std::byte* p = bytes.data() + y * stride + x * 4;
			p[0] = static_cast<std::byte>(x);
			p[1] = static_cast<std::byte>(y);
			p[2] = static_cast<std::byte>(10 * x + y);
			p[3] = std::byte{255};
		}
	}
	return bytes;
}


TEST(CpuPresenterTest, OnlyTheDirtyRegionsAreCopied)
{
	// What is tested: CopyPixels() from a 4 × 3 source with padded rows to a
	// target of another byte order and stride, with one region inside, one
	// reaching over the edge and one empty.
	// Success criteria: Exactly the pixels of the regions, clipped to the
	// images, arrive, red and blue swapped; all others keep their old
	// value; the count of copied pixels is returned.
	// Why it matters: The CPU presenter copies only the union of the
	// invalidated areas into the window (specification 12); a wrong clip
	// would write outside the window's buffer.
	//
	// Given
	const std::vector<std::byte> sourceBytes = Numbered(20);
	const gfx::CpuPixels source{4, 3, 20, gfx::PixelFormat::Bgra8Premul, sourceBytes};
	std::vector<std::byte> targetBytes(16 * 3, std::byte{0});
	const PixelTarget target{4, 3, 16, gfx::PixelFormat::Rgba8Premul, targetBytes};
	const std::array<gfx::IntRect, 3> regions = {{{1, 0, 2, 1}, {2, 2, 9, 9}, {3, 1, 3, 2}}};

	// When
	const std::int64_t copied = CopyPixels(source, target, regions);

	// Then
	EXPECT_EQ(copied, 3);
	const auto at = [&](int x, int y) {
		const std::byte* p = targetBytes.data() + y * 16 + x * 4;
		return std::array<int, 4>{static_cast<int>(p[0]), static_cast<int>(p[1]),
			static_cast<int>(p[2]), static_cast<int>(p[3])};
	};
	EXPECT_EQ(at(1, 0), (std::array<int, 4>{10, 0, 1, 255}));
	EXPECT_EQ(at(2, 2), (std::array<int, 4>{22, 2, 2, 255}));
	EXPECT_EQ(at(3, 2), (std::array<int, 4>{32, 2, 3, 255}));
	EXPECT_EQ(at(0, 0), (std::array<int, 4>{0, 0, 0, 0}));
	EXPECT_EQ(at(3, 1), (std::array<int, 4>{0, 0, 0, 0}));
}


TEST(CpuPresenterTest, TooSmallBuffersAreRefused)
{
	// What is tested: CopyPixels() with a source and a target whose bytes do
	// not cover their size and stride, and with the same byte order.
	// Success criteria: Nothing is copied from or into a short buffer; with
	// matching byte orders the bytes are copied unchanged.
	// Why it matters: The buffers come from a window system that may have
	// resized the window meanwhile; a short buffer must not be overrun.
	//
	// Given
	const std::vector<std::byte> sourceBytes = Numbered(16);
	const gfx::CpuPixels source{4, 3, 16, gfx::PixelFormat::Bgra8Premul, sourceBytes};
	std::vector<std::byte> shortBytes(16 * 2);
	std::vector<std::byte> targetBytes(16 * 3);
	const std::array<gfx::IntRect, 1> all = {{{0, 0, 4, 3}}};

	// When / Then
	EXPECT_EQ(CopyPixels(source, {4, 3, 16, gfx::PixelFormat::Bgra8Premul, shortBytes}, all), 0);
	const gfx::CpuPixels shortSource{4, 3, 16, gfx::PixelFormat::Bgra8Premul,
		std::span(sourceBytes).first(30)};
	EXPECT_EQ(CopyPixels(shortSource, {4, 3, 16, gfx::PixelFormat::Bgra8Premul, targetBytes},
		all), 0);
	EXPECT_EQ(CopyPixels(source, {4, 3, 16, gfx::PixelFormat::Bgra8Premul, targetBytes}, all),
		12);
	EXPECT_EQ(targetBytes, sourceBytes);
}


TEST(CpuPresenterTest, WindowSurfacesHaveTheWindowsDevicePixels)
{
	// What is tested: Backend::CreateWindowSurface() of the dump backend
	// for a window of 100 × 50 logical pixels at scale 2, and for a GPU
	// surface.
	// Success criteria: A CPU surface of 200 × 100 device pixels at scale
	// 2; nullptr for SurfaceKind::Gpu.
	// Why it matters: The surface a window renders into must match the
	// pixels the presenter copies into the window.
	//
	// Given
	gfx::DumpBackend backend;
	gfx::NativeWindow window;
	window.size = {100, 50};
	window.scale = 2;

	// When
	const gfx::SurfaceRef surface = backend.CreateWindowSurface(window,
		gfx::SurfaceKind::CpuRaster);

	// Then
	ASSERT_NE(surface, nullptr);
	EXPECT_EQ(surface->DeviceSize(), (gfx::Size{200, 100}));
	EXPECT_EQ(surface->Scale(), 2);
	EXPECT_EQ(backend.CreateWindowSurface(window, gfx::SurfaceKind::Gpu), nullptr);
}


} // namespace
