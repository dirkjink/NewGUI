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
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;


const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);


// Controls of the Haiku look without labels on a panel of 240 × 136: a
// row of buttons (normal, default, pressed, disabled), check boxes and
// radio buttons (off, on, partially on), a slider and a scroll bar, a menu
// field, a text control and a group frame.
DisplayListRef
Gallery()
{
	const auto look = CreateHaikuLook();
	const FixedTextSystem text;
	const gfx::Caps caps;
	gfx::Canvas canvas({0, 0, 240, 136});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	canvas.FillRect({0, 0, 240, 136}, {.color = kPanel});

	const auto button = [&](gfx::Rect rect, ControlStateFlags flags) {
		look->DrawButtonFrame(context, rect, kPanel, kPanel, flags);
		look->DrawButtonBackground(context, rect, kPanel, flags);
	};
	button({8, 8, 58, 32}, {});
	button({64, 5, 120, 35}, ControlState::DefaultButton);
	button({126, 8, 176, 32}, ControlState::Activated);
	button({182, 8, 232, 32}, ControlState::Disabled);

	const ControlStateFlags marks[] = {{}, ControlState::Activated,
		ControlState::PartiallyActivated};
	float x = 8;
	for (const ControlStateFlags flags : marks) {
		gfx::Rect box{x, 42, x + 14, 56};
		look->DrawCheckBox(context, box, kPanel, flags);
		gfx::Rect radio{x + 20, 42, x + 34, 56};
		look->DrawRadioButton(context, radio, kPanel, flags);
		x += 44;
	}
	gfx::Rect focused{140, 42, 154, 56};
	look->DrawCheckBox(context, focused, kPanel, ControlState::Activated
		| ControlState::Focused);

	gfx::Rect bar{8, 70, 120, 76};
	look->DrawSliderBar(context, bar, kPanel, Palette::Default().Get(
		BColorWhich::ControlHighlight), look->SliderBarColor(Palette::Default(), kPanel),
		0.4f, {}, BOrientation::Horizontal);
	gfx::Rect thumb{48, 64, 64, 82};
	look->DrawSliderThumb(context, thumb, kPanel, {}, BOrientation::Horizontal);

	gfx::Rect scroll{130, 66, 232, 80};
	gfx::Rect scrollBorder = scroll;
	look->DrawScrollBarBorder(context, scrollBorder, kPanel, {}, BOrientation::Horizontal);
	gfx::Rect track{144, 67, 218, 79};
	look->DrawScrollBarBackground(context, track, kPanel, {}, BOrientation::Horizontal);
	gfx::Rect knob{160, 67, 190, 79};
	look->DrawScrollBarThumb(context, knob, kPanel, {}, BOrientation::Horizontal,
		KnobStyle::Lines);
	look->DrawScrollBarButton(context, {131, 67, 144, 79}, kPanel,
		Palette::Default().Get(BColorWhich::ControlText), {}, ArrowDirection::Left,
		BOrientation::Horizontal, false);
	look->DrawScrollBarButton(context, {218, 67, 231, 79}, kPanel,
		Palette::Default().Get(BColorWhich::ControlText), {}, ArrowDirection::Right,
		BOrientation::Horizontal, false);

	gfx::Rect menuField{8, 92, 110, 114};
	look->DrawMenuFieldFrame(context, menuField, kPanel, kPanel);
	look->DrawMenuFieldBackground(context, menuField, kPanel, true);

	gfx::Rect textControl{118, 92, 232, 114};
	look->DrawTextControlBorder(context, textControl, kPanel);

	gfx::Rect group{4, 120, 236, 132};
	look->DrawGroupFrame(context, group, kPanel);
	return canvas.Finish();
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
