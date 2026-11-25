/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Blend2DBackend.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Polygon.h>
#include <ngui/Shape.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/ImageRegistry.h>

#include <ngui/test/Image.h>

#include "support/RenderedWith.h"


namespace {


using namespace ngui::gfx;
using namespace ngui::test;

using Rendered = RenderedWith<Blend2DBackend>;


const Paint kRedPaint = {.color = {1, 0, 0, 1}};


TEST(Blend2DBackendTest, RectanglesFollowRuleA1)
{
	// What is tested: FillRect at scale 1 and at scale 1.5, with fractional
	// edges, through Canvas, DisplayList and the Blend2D backend.
	// Success criteria: Exactly the pixels between the rounded device edges
	// are red, without antialiasing: at scale 1.5 the rectangle [1, 3) maps
	// to device [2, 5) (1.5 rounds up to 2, 4.5 up to 5).
	// Why it matters: Rule A1 (specification 4.4): the bevels of the ported
	// looks are 1 px lines, which antialiasing or a different rounding would
	// blur or shift.
	//
	// Given
	Canvas canvas({0, 0, 10, 10});
	canvas.FillRect({1, 1, 3, 3}, kRedPaint);
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered plain(list, {10, 10});
	const Rendered scaled(list, {15, 15}, 1.5f);

	// Then
	EXPECT_EQ(plain.At(1, 1), kRed);
	EXPECT_EQ(plain.At(2, 2), kRed);
	EXPECT_EQ(plain.At(3, 3), kWhite);
	EXPECT_EQ(plain.At(0, 0), kWhite);
	EXPECT_EQ(scaled.At(1, 1), kWhite);
	EXPECT_EQ(scaled.At(2, 2), kRed);
	EXPECT_EQ(scaled.At(4, 4), kRed);
	EXPECT_EQ(scaled.At(5, 5), kWhite);
}


TEST(Blend2DBackendTest, FramesLieInsideTheRectangle)
{
	// What is tested: StrokeRect with width 1 and width 2.
	// Success criteria: The frame covers exactly the outermost 1 or 2 pixel
	// rows and columns of the rectangle; inside it stays white.
	// Why it matters: Specification 6.1: the frame lies inside, flush with
	// the outer edge; Blend2D strokes centered, so the backend draws the
	// frame as four device rectangles (14.1).
	//
	// Given
	Canvas canvas({0, 0, 20, 10});
	canvas.StrokeRect({0, 0, 10, 10}, kRedPaint);
	canvas.StrokeRect({10, 0, 20, 10}, {.color = {0, 0, 1, 1}, .strokeWidth = 2});
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered rendered(list, {20, 10});

	// Then
	EXPECT_EQ(rendered.At(0, 0), kRed);
	EXPECT_EQ(rendered.At(9, 5), kRed);
	EXPECT_EQ(rendered.At(5, 9), kRed);
	EXPECT_EQ(rendered.At(1, 1), kWhite);
	EXPECT_EQ(rendered.At(8, 8), kWhite);
	EXPECT_EQ(rendered.At(11, 1), kBlue);
	EXPECT_EQ(rendered.At(12, 2), kWhite);
	EXPECT_EQ(rendered.At(18, 8), kBlue);
}


TEST(Blend2DBackendTest, AxisParallelLinesFollowRuleA3)
{
	// What is tested: DrawLines with a horizontal line through pixel centers
	// (as the compatibility layer passes Haiku's StrokeLine) with Butt and
	// with Square caps.
	// Success criteria: Width 1 at y = 2.5 covers exactly row 2; Butt ends at
	// the end points, Square extends half the width beyond both.
	// Why it matters: Rule A3 makes Haiku's 1 px lines pixel-exact; the
	// bevels of the BeOS look consist of them.
	//
	// Given
	const std::array<Line, 1> butt = {{{{2.5f, 2.5f}, {6.5f, 2.5f}, {1, 0, 0, 1}}}};
	const std::array<Line, 1> square = {{{{2.5f, 5.5f}, {6.5f, 5.5f}, {0, 0, 1, 1}}}};
	Canvas canvas({0, 0, 10, 10});
	canvas.DrawLines(butt, 1, Paint::Cap::Butt);
	canvas.DrawLines(square, 1, Paint::Cap::Square);
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered rendered(list, {10, 10});

	// Then
	EXPECT_EQ(rendered.At(3, 2), kRed);
	EXPECT_EQ(rendered.At(5, 2), kRed);
	EXPECT_EQ(rendered.At(3, 1), kWhite);
	EXPECT_EQ(rendered.At(3, 3), kWhite);
	EXPECT_EQ(rendered.At(2, 5), kBlue);
	EXPECT_EQ(rendered.At(6, 5), kBlue);
	EXPECT_EQ(rendered.At(7, 5), kWhite);
}


TEST(Blend2DBackendTest, ClipRegionsAreRespected)
{
	// What is tested: A fill under a clip with a hole (ClipOutRect), and a
	// nested list drawn at an offset under a narrower clip of the parent.
	// Success criteria: Nothing is drawn in the hole; the child appears at
	// its offset only where the parent's clip allows.
	// Why it matters: Blend2D clips to one rectangle only; the region with
	// a hole is four rectangles, each drawn in its own pass (fan-out, 14.2);
	// nested lists are how windows compose their views (11.4).
	//
	// Given
	Canvas child({0, 0, 10, 10});
	child.FillRect({0, 0, 10, 10}, {.color = {0, 0, 1, 1}});
	const DisplayListRef childList = child.Finish();
	Canvas canvas({0, 0, 30, 10});
	canvas.Save();
	canvas.ClipOutRect({4, 4, 6, 6});
	canvas.FillRect({0, 0, 10, 10}, kRedPaint);
	canvas.Restore();
	canvas.ClipRect({12, 0, 16, 10});
	canvas.DrawList(childList, {10, 0});
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered rendered(list, {30, 10});

	// Then
	EXPECT_EQ(rendered.At(3, 3), kRed);
	EXPECT_EQ(rendered.At(4, 4), kWhite);
	EXPECT_EQ(rendered.At(5, 5), kWhite);
	EXPECT_EQ(rendered.At(6, 6), kRed);
	EXPECT_EQ(rendered.At(11, 5), kWhite);
	EXPECT_EQ(rendered.At(12, 5), kBlue);
	EXPECT_EQ(rendered.At(15, 5), kBlue);
	EXPECT_EQ(rendered.At(16, 5), kWhite);
}


TEST(Blend2DBackendTest, GradientsInterpolateWithoutPremultiplying)
{
	// What is tested: A vertical gradient from opaque black to opaque white,
	// and one from transparent red to opaque blue, both over white.
	// Success criteria: The middle row of the first is mid-gray; the second
	// gives (191, 128, 191) in the middle, the result of interpolating the
	// unpremultiplied colors, not (128, 128, 255) as premultiplied
	// interpolation would; within 3/255 (specification 21.2).
	// Why it matters: Specification 8 interpolates not premultiplied;
	// Blend2D interpolates premultiplied, so the backend subdivides stops of
	// different opacity (14.1: to be checked against section 8).
	//
	// Given
	const std::array<GradientStop, 2> gray = {{{0, {0, 0, 0, 1}}, {1, {1, 1, 1, 1}}}};
	const std::array<GradientStop, 2> fade = {{{0, {1, 0, 0, 0}}, {1, {0, 0, 1, 1}}}};
	Canvas canvas({0, 0, 20, 11});
	canvas.FillRect({0, 0, 10, 11},
		{.gradient = *LinearGradient::Create({0, 0}, {0, 11}, gray)});
	canvas.FillRect({10, 0, 20, 11},
		{.gradient = *LinearGradient::Create({0, 0}, {0, 11}, fade)});
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered rendered(list, {20, 11});

	// Then
	EXPECT_TRUE(Near(rendered.At(5, 5), {128, 128, 128, 255}, 3)) << ::testing::PrintToString(
		rendered.At(5, 5));
	EXPECT_TRUE(Near(rendered.At(15, 5), {191, 128, 191, 255}, 3)) << ::testing::PrintToString(
		rendered.At(15, 5));
}


TEST(Blend2DBackendTest, ShapesAreAntialiased)
{
	// What is tested: FillEllipse with and without antialiasing in the paint.
	// Success criteria: The center is fully red and the corners white in both;
	// both have blended edge pixels.
	// Why it matters: Rule A2: non-axis-parallel geometry is antialiased.
	// Blend2D always rasterizes analytically with antialiasing (14.1), so
	// Paint::antialias false has no effect; this test pins that difference
	// to the Skia backend.
	//
	// Given
	Canvas canvas({0, 0, 40, 20});
	canvas.FillEllipse({0, 0, 20, 20}, kRedPaint);
	canvas.FillEllipse({20, 0, 40, 20}, {.color = {1, 0, 0, 1}, .antialias = false});
	const DisplayListRef list = canvas.Finish();

	// When
	const Rendered rendered(list, {40, 20});
	bool blendedWithAa = false;
	bool blendedWithoutAa = false;
	for (int y = 0; y < 20; y++) {
		for (int x = 0; x < 20; x++) {
			const Rgba left = rendered.At(x, y);
			const Rgba right = rendered.At(20 + x, y);
			blendedWithAa |= left != kRed && left != kWhite;
			blendedWithoutAa |= right != kRed && right != kWhite;
		}
	}

	// Then
	EXPECT_EQ(rendered.At(10, 10), kRed);
	EXPECT_EQ(rendered.At(0, 0), kWhite);
	EXPECT_EQ(rendered.At(30, 10), kRed);
	EXPECT_TRUE(blendedWithAa);
	EXPECT_TRUE(blendedWithoutAa);
}


TEST(Blend2DBackendTest, PathsAreDrawnNatively)
{
	// What is tested: The extended profile's paths: a five-pointed star
	// (one self-crossing contour) filled with the nonzero rule at the left,
	// with the even-odd rule at the right; a curve (one cubic) stroked 3
	// wide below them.
	// Success criteria: The upper arm and the center of the nonzero star
	// are red; the even-odd star is red in its arm but white in its center
	// (the pentagon crossed twice). The stroked curve is red in its middle,
	// white above and below it.
	// Why it matters: Blend2D reports Caps::bezierPaths and
	// concavePolygons; BShape and BPolygon draw with them as Haiku's
	// FillShape() does.
	//
	// Given
	const auto star = [](float cx, FillRule rule) {
		Path path;
		path.SetFillRule(rule);
		for (int i = 0; i < 5; i++) {
			// every second corner of a pentagon of radius 18
			const double angle = -1.5707963 + i * 2 * 2 * 3.14159265 / 5;
			const Point corner = {cx + 18 * static_cast<float>(std::cos(angle)),
				20 + 18 * static_cast<float>(std::sin(angle))};
			if (i == 0)
				path.MoveTo(corner);
			else
				path.LineTo(corner);
		}
		path.Close();
		return path;
	};
	Path curve;
	curve.MoveTo({4, 50});
	curve.CubicTo({24, 40}, {56, 60}, {76, 50});
	Canvas canvas({0, 0, 80, 60});

	// When
	canvas.FillPath(star(20, FillRule::NonZero), kRedPaint);
	canvas.FillPath(star(60, FillRule::EvenOdd), kRedPaint);
	canvas.StrokePath(curve, {.color = {1, 0, 0, 1}, .strokeWidth = 3});
	const DisplayListRef list = canvas.Finish();
	const Rendered rendered(list, {80, 60});

	// Then
	EXPECT_EQ(rendered.At(20, 20), kRed);
	EXPECT_EQ(rendered.At(20, 8), kRed);
	EXPECT_EQ(rendered.At(60, 20), kWhite);
	EXPECT_EQ(rendered.At(60, 8), kRed);
	EXPECT_EQ(rendered.At(40, 50), kRed);
	EXPECT_EQ(rendered.At(40, 55), kWhite);
	EXPECT_EQ(rendered.At(40, 45), kWhite);
}


TEST(Blend2DBackendTest, ShapesAndPolygonsAreDrawn)
{
	// What is tested: A BShape of two half circle arcs (radius 10 around
	// (15, 15)), filled; an L-shaped BPolygon (concave) filled at the right.
	// Success criteria: The circle is red in its middle and white in the
	// corner of its square; the L is red in both arms and white in the
	// notch between them.
	// Why it matters: Haiku's FillShape() and FillPolygon() become
	// FillPath() of ToPath(); arcs and concave polygons need the extended
	// profile.
	//
	// Given
	ngui::BShape circle;
	circle.MoveTo({5, 15});
	circle.ArcTo(10, 10, 0, false, false, {25, 15});
	circle.ArcTo(10, 10, 0, false, false, {5, 15});
	circle.Close();
	const std::array<Point, 6> corners = {{{40, 5}, {50, 5}, {50, 20}, {65, 20}, {65, 30},
		{40, 30}}};
	const ngui::BPolygon l(corners);
	Canvas canvas({0, 0, 70, 35});

	// When
	canvas.FillPath(circle.ToPath(), kRedPaint);
	canvas.FillPath(l.ToPath(), kRedPaint);
	const DisplayListRef list = canvas.Finish();
	const Rendered rendered(list, {70, 35});

	// Then
	EXPECT_EQ(rendered.At(15, 15), kRed);
	EXPECT_EQ(rendered.At(15, 6), kRed);
	EXPECT_EQ(rendered.At(6, 6), kWhite);
	EXPECT_EQ(rendered.At(44, 10), kRed);
	EXPECT_EQ(rendered.At(60, 25), kRed);
	EXPECT_EQ(rendered.At(60, 10), kWhite);
}


TEST(Blend2DBackendTest, ImagesComeFromTheRegistry)
{
	// What is tested: DrawImage of a 2 × 2 image registered with
	// ImageRegistry::Default(), drawn 1:1 and with half alpha.
	// Success criteria: The four pixels appear exactly (nearest sampling at
	// 1:1); with alpha 0.5 the red pixel blends with white to (255, 128,
	// 128); the backend's resource is freed with the last reference.
	// Why it matters: Images are frontend-wide handles mapped to each
	// backend's resources (specification 9); the backend must find them and
	// sample them as 6.1 says.
	//
	// Given
	const std::uint8_t pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0,
		255};
	ImageRegistry& registry = ImageRegistry::Default();
	const ImageHandle image = *registry.Register({2, 2, 8, pixels});
	Canvas canvas({0, 0, 10, 4});
	canvas.DrawImage(image, {0, 0, 2, 2}, {0, 0, 2, 2});
	canvas.DrawImage(image, {0, 0, 2, 2}, {4, 0, 6, 2}, 0.5f);
	DisplayListRef list = canvas.Finish();
	registry.Release(image);

	// When
	{
		const Rendered rendered(list, {10, 4});

		// Then
		EXPECT_EQ(rendered.At(0, 0), kRed);
		EXPECT_EQ(rendered.At(1, 0), (Rgba{0, 255, 0, 255}));
		EXPECT_EQ(rendered.At(0, 1), kBlue);
		EXPECT_EQ(rendered.At(1, 1), (Rgba{0, 0, 0, 255}));
		EXPECT_TRUE(Near(rendered.At(4, 0), {255, 128, 128, 255}, 1));
	}
	list.reset();
	EXPECT_EQ(registry.ReferenceCount(image), 0u);
}


TEST(Blend2DBackendTest, SurfacesAndRegistration)
{
	// What is tested: Offscreen surfaces (size, scale, pixel access, invalid
	// arguments), resizing, Register(), and EncodePng() of opaque pixels.
	// Success criteria: A surface has the rounded device size, its pixels are
	// readable as premultiplied 8-bit data; invalid sizes or scales give no
	// surface; "blend2d" registers once with a higher priority than "dump";
	// it draws the paths of the extended profile (Caps::bezierPaths,
	// concavePolygons); the PNG decodes to the same pixels.
	// Why it matters: Tests and the CPU presenter read these pixels
	// (specification 13.3); the registry decides which backend a program
	// starts with.
	//
	// Given
	Blend2DBackend backend;
	BackendRegistry registry;
	registry.Register("dump", 0, [] { return nullptr; });
	const std::uint8_t pixels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 10, 20, 30,
		255};

	// When
	const SurfaceRef surface = backend.CreateOffscreenSurface({30.4f, 20}, 2);
	backend.ResizeSurface(*surface, {12, 8}, 1);
	const auto png = backend.EncodePng({2, 2, 8, pixels});

	// Then
	ASSERT_NE(surface, nullptr);
	EXPECT_EQ(surface->DeviceSize(), (Size{12, 8}));
	EXPECT_EQ(surface->Scale(), 1.0f);
	ASSERT_TRUE(surface->Pixels().has_value());
	EXPECT_EQ(surface->Pixels()->width, 12);
	EXPECT_GE(surface->Pixels()->stride, 48u);
	EXPECT_EQ(backend.CreateOffscreenSurface({0, 10}, 1), nullptr);
	EXPECT_EQ(backend.CreateOffscreenSurface({10, 10}, 0), nullptr);
	EXPECT_TRUE(Blend2DBackend::Register(registry));
	EXPECT_FALSE(Blend2DBackend::Register(registry));
	EXPECT_EQ(registry.Names().front(), "blend2d");
	ASSERT_TRUE(png.has_value()) << png.error().message;
	const auto decoded = DecodePng(*png);
	ASSERT_TRUE(decoded.has_value());
	EXPECT_TRUE(std::ranges::equal(decoded->Bytes(), pixels));
	EXPECT_EQ(backend.Capabilities(), (Caps{.bezierPaths = true, .concavePolygons = true}));
}


} // namespace
