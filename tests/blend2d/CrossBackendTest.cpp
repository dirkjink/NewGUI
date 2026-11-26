/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Blend2DBackend.h>
#include <ngui/gfx/SkiaBackend.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/BeLook.h>
#include <ngui/FlatLook.h>
#include <ngui/HaikuLook.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>
#include <ngui/text/FontTextSystem.h>

#include "look/LookCalls.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


// \a list rendered by Skia (first) and by Blend2D (second) at \a scale.
std::pair<Image, Image>
RenderBoth(const DisplayList& list, float scale, const std::vector<FontFile>& fonts = {})
{
	SkiaBackend skia;
	Blend2DBackend blend2d;
	for (const FontFile& font : fonts) {
		(void)skia.AddFont(font.id, font.data, font.faceIndex);
		(void)blend2d.AddFont(font.id, font.data, font.faceIndex);
	}
	auto reference = RenderToImage(skia, list, scale);
	auto actual = RenderToImage(blend2d, list, scale);
	if (!reference.has_value() || !actual.has_value())
		return {};
	return {std::move(*reference), std::move(*actual)};
}


ImageComparison
Compare(const DisplayList& list, float scale, const Tolerance& tolerance)
{
	const auto [reference, actual] = RenderBoth(list, scale);
	return CompareImages(reference, actual, tolerance);
}


TEST(CrossBackendTest, AxisParallelPrimitivesArePixelIdentical)
{
	// What is tested: Rectangles with fractional edges, frames of width 1
	// to 3, axis-parallel lines with butt and square caps through pixel
	// centers and on pixel edges, a gradient-free nested list under a clip
	// region with a hole, rendered by Skia and Blend2D at scales 1, 1.5 and
	// 2.
	// Success criteria: Both images are the same, pixel for pixel.
	// Why it matters: Conformance level C1× (21.4): the axis-parallel
	// primitives the BeOS look is made of must not depend on the backend;
	// rules A1 and A3 fix every pixel of them.
	//
	// Given
	Canvas child({0, 0, 20, 20});
	child.FillRect({0, 0, 20, 20}, {.color = {0.2f, 0.4f, 0.6f, 1}});
	child.StrokeRect({2, 2, 18, 18}, {.color = {1, 1, 1, 1}, .strokeWidth = 2});
	const DisplayListRef nested = child.Finish();
	Canvas canvas({0, 0, 100, 60});
	canvas.FillRect({1.3f, 1.6f, 20.5f, 10.4f}, {.color = {1, 0, 0, 1}});
	canvas.FillRect({22, 2, 40, 12}, {.color = {0, 0.5f, 0, 0.5f}});
	for (int width = 1; width <= 3; width++) {
		const float x = 42.0f + 18.0f * static_cast<float>(width - 1);
		canvas.StrokeRect({x, 2, x + 16, 16}, {.color = {0, 0, 1, 1},
			.strokeWidth = static_cast<float>(width)});
	}
	const std::array<Line, 4> lines = {Line{{2.5f, 20.5f}, {30.5f, 20.5f}, {1, 0, 0, 1}},
		Line{{2, 24}, {30, 24}, {0, 0, 1, 1}}, Line{{34.5f, 20.5f}, {34.5f, 40.5f},
			{0, 0.6f, 0, 1}}, Line{{38, 20}, {38, 40}, {0.5f, 0, 0.5f, 1}}};
	canvas.DrawLines(lines, 1, Paint::Cap::Butt);
	canvas.DrawLines(lines, 2, Paint::Cap::Square);
	canvas.Save();
	canvas.ClipRect({50, 20, 90, 58});
	canvas.ClipOutRect({60, 30, 70, 40});
	canvas.DrawList(nested, {55, 25});
	canvas.Restore();
	const DisplayListRef list = canvas.Finish();

	for (const float scale : {1.0f, 1.5f, 2.0f}) {
		// When
		const ImageComparison comparison = Compare(*list, scale, Tolerance::Exact());

		// Then
		EXPECT_TRUE(comparison.passed) << "scale " << scale << ": " << comparison.Describe();
	}
}


// Every drawing method of \a look in every state, recorded on 100 × 40 and
// rendered by both backends: pixel for pixel the same, except the methods
// in \a antialiased, which must look alike.
void
ExpectLookAlike(const ControlLook& look, const std::set<std::string>& antialiased)
{
	const FixedTextSystem text;
	const gfx::Caps caps;
	for (const LookCall& call : EveryLookCall(look)) {
		for (const ControlStateFlags state : LookStates()) {
			Canvas canvas({0, 0, 100, 40});
			canvas.FillRect({0, 0, 100, 40}, {.color = {1, 1, 1, 1}});
			DrawContext context{canvas, caps, Palette::Default(), text, 1};
			gfx::Rect rect{0, 0, 100, 40};
			call.draw(context, rect, state);
			const DisplayListRef list = canvas.Finish();
			const auto [reference, actual] = RenderBoth(*list, 1);
			if (!antialiased.contains(call.name)) {
				const ImageComparison comparison = CompareImages(reference, actual,
					Tolerance::Exact());
				EXPECT_TRUE(comparison.passed) << call.name << " flags " << state.Raw()
					<< ": " << comparison.Describe();
				continue;
			}
			const RasterizerComparison alike = CompareRasterizers(reference, actual);
			EXPECT_TRUE(alike.passed) << call.name << " flags " << state.Raw() << ": "
				<< alike.Describe();
		}
	}
}


TEST(CrossBackendTest, TheBeOSLookLooksTheSame)
{
	// What is tested: Every drawing method of the BeOS look in every state
	// (look/LookCalls.h), rendered by Skia and Blend2D.
	// Success criteria: Pixel for pixel the same, except the five methods
	// with antialiased shapes (arrows, the check mark, radio buttons,
	// slider triangles, scroll bar arrows), which look alike: on average at
	// most 3/255 apart, at most 0.5 % of the pixels more than 64/255.
	// Why it matters: Conformance level C1× (21.4): every axis-parallel
	// primitive of the BeOS corpus is pixel-identical on both backends;
	// antialiased edges are where two rasterizers may differ.
	//
	// When / Then
	ExpectLookAlike(*CreateBeLook(), {"DrawArrowShape", "DrawCheckBox", "DrawRadioButton",
		"DrawSliderTriangle", "DrawScrollBarButton"});
}


TEST(CrossBackendTest, TheHaikuLookLooksTheSame)
{
	// What is tested: Every drawing method of the Haiku look in every
	// state, rendered by Skia and Blend2D.
	// Success criteria: The methods without gradients or rounded corners
	// are pixel for pixel the same; the others look alike as in
	// TheBeOSLookLooksTheSame (their gradients are at most 1/255 apart).
	// Why it matters: Conformance level C2 on all backends (21.4): the
	// Haiku look's gradients and rounded corners must not depend on the
	// backend beyond its rasterizer.
	//
	// When / Then
	ExpectLookAlike(*CreateHaikuLook(), {"DrawArrowShape", "DrawButtonBackground",
		"DrawButtonFrame", "DrawButtonWithPopUpBackground", "DrawCheckBox",
		"DrawMenuBarBackground", "DrawMenuFieldBackground", "DrawMenuFieldFrame",
		"DrawMenuItemBackground", "DrawRadioButton", "DrawScrollBarButton",
		"DrawScrollBarThumb", "DrawSliderBar", "DrawSliderTriangle", "DrawStatusBar"});
}


TEST(CrossBackendTest, TheFlatLookLooksTheSame)
{
	// What is tested: Every drawing method of the flat look in every state,
	// rendered by Skia and Blend2D.
	// Success criteria: Pixel for pixel the same, except the methods the
	// Haiku look draws with gradients or rounded corners and three more
	// the flat look draws so (the gradient of the scroll bar background,
	// the rounded slider thumb and text control border), which look alike
	// as in TheBeOSLookLooksTheSame.
	// Why it matters: Conformance level C2 on all backends (21.4), also for
	// the look that derives from the Haiku look.
	//
	// When / Then
	ExpectLookAlike(*CreateFlatLook(), {"DrawArrowShape", "DrawButtonBackground",
		"DrawButtonFrame", "DrawButtonWithPopUpBackground", "DrawCheckBox",
		"DrawMenuBarBackground", "DrawMenuFieldBackground", "DrawMenuFieldFrame",
		"DrawMenuItemBackground", "DrawRadioButton", "DrawScrollBarBackground",
		"DrawScrollBarButton", "DrawScrollBarThumb", "DrawSliderBar", "DrawSliderThumb",
		"DrawSliderTriangle", "DrawStatusBar", "DrawTextControlBorder"});
}


TEST(CrossBackendTest, TextLooksTheSame)
{
	// What is tested: A line of text in the bundled Noto Sans and one in
	// its bold face, 12 px, shaped by the text subsystem, rendered by Skia
	// (FreeType) and Blend2D (its own rasterizer).
	// Success criteria: The text looks alike, as the antialiased shapes of
	// the looks do; the glyphs are at the same places, only their
	// antialiasing differs.
	// Why it matters: Both backends draw the glyph runs of the same font
	// files (specification 10, 14.1); text must not move or change size
	// with the backend.
	//
	// Given
	const auto text = text::FontTextSystem::CreateBundled(12);
	ASSERT_TRUE(text.has_value());
	Canvas canvas({0, 0, 220, 50});
	canvas.FillRect({0, 0, 220, 50}, {.color = {1, 1, 1, 1}});
	(*text)->Shape("Haiku's Interface Kit", FontRole::Plain).Draw(canvas, {4, 16},
		{.color = {0, 0, 0, 1}});
	(*text)->Shape("Quick brown fox", FontRole::Bold).Draw(canvas, {4, 40},
		{.color = {0.2f, 0.2f, 0.6f, 1}});
	const DisplayListRef list = canvas.Finish();

	// When
	const auto [reference, actual] = RenderBoth(*list, 1, (*text)->Fonts());

	// Then
	const RasterizerComparison alike = CompareRasterizers(reference, actual);
	EXPECT_TRUE(alike.passed) << alike.Describe();
}


} // namespace
