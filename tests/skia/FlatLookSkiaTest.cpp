/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/FlatLook.h>

#include <gtest/gtest.h>

#include <ngui/Palette.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Render.h>

#include "Rendered.h"
#include "look/LookGallery.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


TEST(FlatLookSkiaTest, TheSliderIsFilledLeftOfItsThumb)
{
	// What is tested: DrawSliderBar() of the flat look with the left fill
	// color red and the right one green at the value 0.5, horizontal on
	// [10, 110) × [10, 16) and vertical on [130, 136) × [10, 110), rendered
	// by Skia.
	// Success criteria: Horizontally red left of the middle and green right
	// of it; vertically red below the middle and green above it, as in the
	// Haiku look.
	// Why it matters: The fill shows how far a slider is set; Haiku's flat
	// look swaps the two colors, the port puts them where the Haiku look
	// does (a documented deviation).
	//
	// Given
	const auto look = CreateFlatLook();
	const FixedTextSystem text;
	const gfx::Caps caps;
	gfx::Canvas canvas({0, 0, 140, 120});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	const gfx::Color panel = Palette::Default().Get(BColorWhich::PanelBackground);
	canvas.FillRect({0, 0, 140, 120}, {.color = panel});
	const gfx::Color red = gfx::Color::FromRgba8(255, 0, 0, 255);
	const gfx::Color green = gfx::Color::FromRgba8(0, 255, 0, 255);

	// When
	look->DrawSliderBar(context, {10, 10, 110, 16}, panel, red, green, 0.5f, {},
		BOrientation::Horizontal);
	look->DrawSliderBar(context, {130, 10, 136, 110}, panel, red, green, 0.5f, {},
		BOrientation::Vertical);
	const Rendered rendered(canvas.Finish(), {140, 120});

	// Then
	const auto reddish = [](Rgba pixel) { return pixel.r > 150 && pixel.g < 100; };
	const auto greenish = [](Rgba pixel) { return pixel.g > 150 && pixel.r < 100; };
	EXPECT_TRUE(reddish(rendered.At(30, 13)));
	EXPECT_TRUE(greenish(rendered.At(90, 13)));
	EXPECT_TRUE(reddish(rendered.At(133, 90)));
	EXPECT_TRUE(greenish(rendered.At(133, 30)));
}


TEST(FlatLookSkiaTest, GalleryMatchesItsSnapshot)
{
	// What is tested: The gallery of controls (test::LookGallery()) in the
	// flat look rendered by Skia at scale 1 and 2 against
	// FlatLookSkiaTest_Gallery.flat.skia.{1,2}x.png.
	// Success criteria: Equal within the anti-aliasing tolerance (21.2):
	// buttons, menu field, scroll bar, slider and frames flatter than the
	// Haiku look's (HaikuLookSkiaTest_Gallery), check boxes and radio
	// buttons the same.
	// Why it matters: The look is a port of 2,200 lines; the reference,
	// checked by eye against Haiku's flat look, pins every control in every
	// change to the port, the Haiku look it derives from, the compatibility
	// layer or the backend.
	//
	// Given
	const gfx::DisplayListRef list = LookGallery(*CreateFlatLook());

	for (const float scale : {1.0f, 2.0f}) {
		// When
		SkiaBackend backend;
		const auto image = RenderToImage(backend, *list, scale);

		// Then
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "Gallery", {.look = "flat", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::AntiAliased()});
	}
}


} // namespace
