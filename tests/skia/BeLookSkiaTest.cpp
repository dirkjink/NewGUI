/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/BeLook.h>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>

#include "Rendered.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;


// A button of 100 × 24 in the BeOS look, frame and background, rendered
// with Skia at scale 1 on white.
DisplayListRef
Button(ControlStateFlags flags)
{
	const auto look = CreateBeLook();
	const FixedTextSystem text;
	const gfx::Caps caps;
	gfx::Canvas canvas({0, 0, 100, 24});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	const gfx::Color panel = Palette::Default().Get(BColorWhich::PanelBackground);
	gfx::Rect rect{0, 0, 100, 24};
	look->DrawButtonFrame(context, rect, panel, panel, flags);
	look->DrawButtonBackground(context, rect, panel, flags);
	return canvas.Finish();
}


Rgba
Gray(int value)
{
	return {value, value, value, 255};
}


TEST(BeLookSkiaTest, ButtonPixelsAreHaikus)
{
	// What is tested: A released BeOS button rendered by the Skia backend,
	// read pixel by pixel across its left edge and its top edge.
	// Success criteria: Exactly Haiku's bevel, without antialiasing: white
	// outside, the corner of the background ring (216), the dark border (96),
	// the light bevel (232), white, the inner shadow on the right (152 at
	// x = 97, 216 at x = 96), and the face in 231.
	// Why it matters: The 1 px lines of the line arrays must land on whole
	// pixels (rules A1 and A3) for the look to match Haiku pixel for pixel
	// (specification 21, conformance level C1).
	//
	// When
	const Rendered rendered(Button({}), {100, 24});

	// Then
	EXPECT_EQ(rendered.At(0, 10), kWhite);
	EXPECT_EQ(rendered.At(1, 0), Gray(216));
	EXPECT_EQ(rendered.At(1, 10), Gray(96));
	EXPECT_EQ(rendered.At(50, 0), Gray(96));
	EXPECT_EQ(rendered.At(2, 10), Gray(232));
	EXPECT_EQ(rendered.At(50, 1), Gray(232));
	EXPECT_EQ(rendered.At(3, 10), Gray(255));
	EXPECT_EQ(rendered.At(98, 10), Gray(96));
	EXPECT_EQ(rendered.At(97, 10), Gray(152));
	EXPECT_EQ(rendered.At(96, 10), Gray(216));
	EXPECT_EQ(rendered.At(50, 23), Gray(96));
	EXPECT_EQ(rendered.At(50, 10), Gray(231));
}


TEST(BeLookSkiaTest, PressedButtonIsDarkened)
{
	// What is tested: The pressed (activated) BeOS button, rendered by Skia.
	// Success criteria: The face is 152 over 231 at alpha 128, about 191;
	// the darkening reaches 3 px beyond the face, over the bevel, but not
	// the outer dark border at x = 1.
	// Why it matters: The replacement of B_OP_INVERT (specification 16) must
	// look pressed and stay inside the button.
	//
	// When
	const Rendered rendered(Button(ControlState::Activated), {100, 24});

	// Then
	EXPECT_TRUE(Near(rendered.At(50, 10), Gray(191), 1)) << ::testing::PrintToString(
		rendered.At(50, 10));
	EXPECT_TRUE(Near(rendered.At(2, 10), Gray(192), 1)) << ::testing::PrintToString(
		rendered.At(2, 10));
	EXPECT_EQ(rendered.At(1, 10), Gray(96));
}


} // namespace
