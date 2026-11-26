/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/HaikuLook.h>

#include <cstdlib>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Render.h>

#include "Rendered.h"
#include "look/LookGallery.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;


DisplayListRef
Gallery()
{
	return LookGallery(*CreateHaikuLook());
}


Rgba
Gray(int value)
{
	return {value, value, value, 255};
}


TEST(HaikuLookSkiaTest, ButtonEdgesAreHaikus)
{
	// What is tested: The normal button of the gallery ([8, 58) × [8, 32))
	// rendered by Skia, read across its left edge and down its face.
	// Success criteria: The frame and bevel colors of Haiku's
	// _DrawButtonFrame() at the left edge, the dark frame 140 at x = 9; the
	// face is lighter at its top than at its bottom, as Haiku's glossy
	// gradient (tints 0.49 to 0.90 of 216) makes it.
	// Why it matters: The Haiku look draws with lines and gradients
	// (conformance level C2); the lines must land on whole pixels and the
	// gradient must run the right way.
	//
	// When
	const Rendered rendered(Gallery(), {240, 136});

	// Then
	EXPECT_EQ(rendered.At(9, 20), Gray(140));
	const Rgba top = rendered.At(33, 11);
	const Rgba bottom = rendered.At(33, 28);
	EXPECT_GT(top.r, bottom.r);
	EXPECT_EQ(top.r, top.b);
	EXPECT_EQ(bottom.r, bottom.b);
}


TEST(HaikuLookSkiaTest, GalleryMatchesItsSnapshot)
{
	// What is tested: The gallery of controls in the Haiku look rendered by
	// Skia at scale 1 and 2 against HaikuLookSkiaTest_Gallery.haiku.skia.
	// {1,2}x.png.
	// Success criteria: Equal within the anti-aliasing tolerance (21.2):
	// the look has rounded corners, circles and gradients.
	// Why it matters: The look is a port of 3,900 lines; the reference,
	// checked by eye against Haiku, pins every control in every change to
	// the port, the compatibility layer or the backend.
	//
	// Given
	const DisplayListRef list = Gallery();

	for (const float scale : {1.0f, 2.0f}) {
		// When
		SkiaBackend backend;
		const auto image = RenderToImage(backend, *list, scale);

		// Then
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "Gallery", {.look = "haiku", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::AntiAliased()});
	}
}


} // namespace
