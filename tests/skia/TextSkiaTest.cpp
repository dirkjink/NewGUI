/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/BeLook.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Render.h>

#include "Rendered.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


// "OK" in the bundled plain font at 12 px, black, its baseline starting at
// (4, 16) in a list of 40 × 24.
DisplayListRef
Ok(const TextSystem& text)
{
	const ShapedText shaped = text.Shape("OK", FontRole::Plain);
	Canvas canvas({0, 0, 40, 24});
	canvas.DrawGlyphRun(shaped.Run(), {4, 16}, Paint{.color = {0, 0, 0, 1}});
	return canvas.Finish();
}


// The darkness (255 minus the green channel) of each pixel.
struct Ink {
			int					total = 0;
			int					darkest = 0;
			int					left = 1000;
			int					right = -1;
			int					top = 1000;
			int					bottom = -1;
};


Ink
Measure(const Rendered& rendered, int width, int height)
{
	Ink ink;
	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			const int dark = 255 - rendered.At(x, y).g;
			if (dark < 16)
				continue;
			ink.total += dark;
			ink.darkest = std::max(ink.darkest, dark);
			ink.left = std::min(ink.left, x);
			ink.right = std::max(ink.right, x);
			ink.top = std::min(ink.top, y);
			ink.bottom = std::max(ink.bottom, y);
		}
	}
	return ink;
}


TEST(TextSkiaTest, GlyphRunsAreDrawnInTheTextSystemsFont)
{
	// What is tested: "OK" shaped by the bundled text system and rendered by
	// Skia once with the text system's fonts added to the backend and once
	// without; AddFont() with a real font file and with bytes that are none.
	// Success criteria: With the fonts the glyphs are drawn: full black in
	// their stems, inside the advance from x = 4 to 4 + 16.8, from about the
	// cap height (12 · 0.714 above the baseline) down to the baseline at
	// y = 16 (overshoot of the O at most a pixel). Without the fonts the run
	// is skipped and everything stays white. AddFont() takes the font file
	// and refuses the other bytes.
	// Why it matters: The backend must draw with the same font file the text
	// subsystem shaped with (specification 13.4), and its baseline is where
	// the look put it (21.2: "positions of the baseline identical").
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	const DisplayListRef list = Ok(*text);
	SkiaBackend backend;
	const std::array<std::byte, 4> junk{};

	// When
	const Rendered withFonts(list, {40, 24}, 1, {0, 0}, text->Fonts());
	const Rendered withoutFonts(list, {40, 24});

	// Then
	const Ink ink = Measure(withFonts, 40, 24);
	EXPECT_GE(ink.darkest, 240);
	EXPECT_GE(ink.left, 4);
	EXPECT_LE(ink.right, 21);
	EXPECT_GE(ink.top, 6);
	EXPECT_LE(ink.top, 8);
	EXPECT_GE(ink.bottom, 15);
	EXPECT_LE(ink.bottom, 16);
	EXPECT_EQ(Measure(withoutFonts, 40, 24).total, 0);
	EXPECT_TRUE(backend.AddFont(1, text->Fonts()[0].data));
	EXPECT_FALSE(backend.AddFont(2, junk));
}


TEST(TextSkiaTest, TwiceTheScaleIsTwiceTheText)
{
	// What is tested: The run of "OK" rendered at scale 2.
	// Success criteria: The ink spans about twice the device pixels of
	// scale 1: right edge at most 2 · 21 + 1, top between 12 and 16, the
	// baseline at device row 32.
	// Why it matters: Glyph runs are in logical pixels like everything
	// else; the backend applies the scale (specification 4.2), so text stays
	// sharp on HiDPI screens instead of being scaled as pixels.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());

	// When
	const Rendered rendered(Ok(*text), {80, 48}, 2, {0, 0}, text->Fonts());

	// Then
	const Ink ink = Measure(rendered, 80, 48);
	EXPECT_GE(ink.left, 8);
	EXPECT_LE(ink.right, 43);
	EXPECT_GE(ink.top, 12);
	EXPECT_LE(ink.top, 16);
	EXPECT_GE(ink.bottom, 31);
	EXPECT_LE(ink.bottom, 32);
}


// A BeOS button of 100 × 24 labeled "Click me", drawn as Haiku's
// BButton::Draw(): frame, background, then the label centered in the whole
// button in the control text color.
DisplayListRef
LabeledButton(const TextSystem& text, ControlStateFlags flags)
{
	const auto look = CreateBeLook();
	const gfx::Caps caps;
	Canvas canvas({0, 0, 100, 24});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	const Palette& palette = Palette::Default();
	const Color base = palette.Get(BColorWhich::ControlBackground);
	const Color background = palette.Get(BColorWhich::PanelBackground);
	const Color textColor = palette.Get(BColorWhich::ControlText);
	gfx::Rect rect{0, 0, 100, 24};
	look->DrawButtonFrame(context, rect, base, background, flags);
	look->DrawButtonBackground(context, rect, base, flags);
	look->DrawLabel(context, "Click me", 0, rect, base, flags,
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle}, &textColor);
	return canvas.Finish();
}


TEST(TextSkiaTest, BeButtonLabelMatchesItsSnapshot)
{
	// What is tested: A BeOS button labeled "Click me" in the bundled plain
	// font, released and pressed, rendered by Skia at scale 1 and 2 against
	// the snapshots TextSkiaTest_LabeledButton*.be.skia.{1,2}x.png.
	// Success criteria: Within the text tolerance of specification 21.2
	// (at most 3 % differing pixels); the label is centered as Haiku's
	// DrawLabel() does it.
	// Why it matters: This is the first control with text: look, text
	// subsystem, BFont port, glyph runs and Skia's typefaces together. The
	// snapshots pin it down until reference screenshots from Haiku exist
	// (21.3).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());

	for (const bool pressed : {false, true}) {
		const DisplayListRef list = LabeledButton(*text,
			pressed ? ControlStateFlags(ControlState::Activated) : ControlStateFlags());
		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, pressed ? "LabeledButtonPressed" : "LabeledButton",
				{.look = "be", .backend = "skia", .scale = scale,
					.tolerance = Tolerance::Text()});
		}
	}
}


} // namespace
