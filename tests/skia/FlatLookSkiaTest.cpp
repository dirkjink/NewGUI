/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/FlatLook.h>

#include <gtest/gtest.h>

#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Render.h>

#include "look/LookGallery.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


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
