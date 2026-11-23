/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/DumpBackend.h>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/Canvas.h>


namespace {


using namespace ngui::gfx;


TEST(DumpBackendTest, FramesBecomeText)
{
	// What is tested: A frame on an offscreen DumpSurface with two rendered
	// lists, then a second frame after ResizeSurface().
	// Success criteria: The surface's text is the last finished frame: a
	// frame line with size, scale and clear color, then each list with its
	// offset; the second frame shows the new size.
	// Why it matters: The dump backend is how tests look at a whole frame
	// without a GPU (specification 15); it runs through the same Backend
	// interface as Skia will.
	//
	// Given
	DumpBackend backend;
	const SurfaceRef surface = backend.CreateOffscreenSurface({200, 100}, 2);
	Canvas canvas({0, 0, 10, 10});
	canvas.FillRect({0, 0, 10, 10}, Paint{});
	const DisplayListRef list = canvas.Finish();

	// When
	backend.BeginFrame(*surface, {1, 1, 1, 1});
	backend.Render(*list, {0, 0});
	backend.Render(*list, {12.5f, 0});
	backend.EndFrame(*surface);
	const std::string first = dynamic_cast<DumpSurface&>(*surface).Text();
	backend.ResizeSurface(*surface, {300, 100}, 1.5f);
	backend.BeginFrame(*surface, {0, 0, 0, 1});
	backend.EndFrame(*surface);

	// Then
	EXPECT_EQ(first,
		"frame device=[200 100] scale=2 clear=(1 1 1 1)\n"
		"render offset=(0 0)\n"
		"DisplayList bounds=[0 0 10 10] ops=1\n"
		"clip [0 0 10 10]\n"
		"FillRect rect=[0 0 10 10] color=(0 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"render offset=(12.5 0)\n"
		"DisplayList bounds=[0 0 10 10] ops=1\n"
		"clip [0 0 10 10]\n"
		"FillRect rect=[0 0 10 10] color=(0 0 0 1) width=1 cap=Butt join=Miter aa=1\n");
	EXPECT_EQ(dynamic_cast<DumpSurface&>(*surface).Text(),
		"frame device=[300 100] scale=1.5 clear=(0 0 0 1)\n");
	EXPECT_EQ(surface->LogicalSize(), (Size{200, 66.6666641f}));
	EXPECT_FALSE(surface->Pixels().has_value());
}


TEST(DumpBackendTest, CoreProfileOnly)
{
	// What is tested: Capabilities(), window surfaces, invalid offscreen
	// sizes, image creation and AddFont() of the dump backend.
	// Success criteria: Only the core profile; no window surfaces; empty or
	// non-finite sizes and non-positive scales give no surface; images get
	// distinct non-zero handles; AddFont() is Backend's default, false: the
	// dump writes glyph runs as numbers and reads no font files.
	// Why it matters: The dump backend is the fallback that runs everywhere
	// (specification 15); it must report honestly what it does not do.
	//
	// Given
	DumpBackend backend;
	const std::uint8_t pixel[4] = {1, 2, 3, 4};

	// When / Then
	EXPECT_EQ(backend.Capabilities(), Caps{});
	EXPECT_EQ(backend.CreateOffscreenSurface({0, 10}, 1), nullptr);
	EXPECT_EQ(backend.CreateOffscreenSurface({10, 10}, 0), nullptr);
	const ImageHandle a = backend.CreateImage({1, 1, 4, pixel});
	const ImageHandle b = backend.CreateImage({1, 1, 4, pixel});
	EXPECT_NE(a, 0u);
	EXPECT_NE(a, b);
	EXPECT_EQ(backend.CreateImage({0, 1, 4, pixel}), 0u);
	EXPECT_FALSE(backend.AddFont(1, std::as_bytes(std::span(pixel))));
}


TEST(BackendRegistryTest, SelectsByNameAndPriority)
{
	// What is tested: The default registry, registering backends with
	// priorities, Create() by name and CreatePreferred() with a known, an
	// unknown and an empty preference.
	// Success criteria: "dump" is always there; names come highest priority
	// first; a preferred known backend wins, otherwise the highest priority;
	// unknown names give ErrorCode::NotFound; names cannot be taken twice.
	// Why it matters: Choosing the backend is part of M11; a program must
	// start with the best backend built in and fall back to one that works.
	//
	// Given
	BackendRegistry registry;
	int created = 0;
	const auto factory = [&created] {
		created++;
		return std::make_unique<DumpBackend>();
	};

	// When
	registry.Register("dump", 0, factory);
	registry.Register("fast", 100, factory);
	registry.Register("medium", 50, factory);

	// Then
	EXPECT_EQ(registry.Names(), (std::vector<std::string>{"fast", "medium", "dump"}));
	EXPECT_FALSE(registry.Register("fast", 1, factory));
	EXPECT_FALSE(registry.Register("", 1, factory));
	EXPECT_TRUE(registry.Create("medium").has_value());
	EXPECT_EQ(registry.Create("missing").error().code, ngui::ErrorCode::NotFound);
	EXPECT_TRUE(registry.CreatePreferred("dump").has_value());
	EXPECT_TRUE(registry.CreatePreferred("missing").has_value());
	EXPECT_TRUE(registry.CreatePreferred("").has_value());
	EXPECT_EQ(created, 4);
	const std::vector<std::string> defaults = BackendRegistry::Default().Names();
	EXPECT_NE(std::find(defaults.begin(), defaults.end(), "dump"), defaults.end());
}


} // namespace
