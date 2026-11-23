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
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/RadioButton.h>
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


// A BButton "Cancel" of 100 × 30 in \a state drawn by itself (Draw()), as
// in a window, with the bundled font.
DisplayListRef
ButtonIn(const TextSystem& text, const char* state)
{
	BButton button({0, 0, 100, 30}, "button", "Cancel", nullptr);
	if (std::string_view(state) == "Default")
		button.MakeDefault(true);
	else if (std::string_view(state) == "Disabled")
		button.SetEnabled(false);
	else if (std::string_view(state) == "Pressed")
		button.SetValue(kControlOn);
	button.ResizeTo(100, 30);
	button.MoveTo({0, 0});
	const gfx::Caps caps;
	Canvas canvas({0, 0, 100, 30});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	button.Draw(context);
	return canvas.Finish();
}


TEST(TextSkiaTest, ButtonStatesMatchTheirSnapshots)
{
	// What is tested: BButton::Draw() of a button "Cancel" of 100 × 30 as
	// an ordinary, a default, a disabled and a pressed button, rendered by
	// Skia at scale 1 and 2 against TextSkiaTest_Button<State>.be.skia.*.
	// Success criteria: Within the text tolerance of 21.2: the BeOS frame
	// (with the default button's extra ring), the background darkened when
	// pressed, the label centered, in the disabled color when disabled.
	// Why it matters: The first real control: its flags (ControlLook::
	// Flags()) and drawing calls must give the look's pictures in every
	// state (21.3: all states at scale 1 and 2).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());

	for (const char* state : {"Normal", "Default", "Disabled", "Pressed"}) {
		const DisplayListRef list = ButtonIn(*text, state);
		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("Button") + state,
				{.look = "be", .backend = "skia", .scale = scale,
					.tolerance = Tolerance::Text()});
		}
	}
}


// A check box or radio button of 120 × 24 labeled "Option" in \a state.
DisplayListRef
ChoiceIn(const TextSystem& text, std::string_view kind, std::string_view state)
{
	std::unique_ptr<BControl> control;
	if (kind == "CheckBox")
		control = std::make_unique<BCheckBox>(gfx::Rect{0, 0, 120, 24}, "c", "Option", nullptr);
	else
		control = std::make_unique<BRadioButton>(gfx::Rect{0, 0, 120, 24}, "r", "Option",
			nullptr);
	if (state == "On")
		control->SetValue(kControlOn);
	else if (state == "Partial")
		control->SetValue(kControlPartiallyOn);
	else if (state == "Disabled")
		control->SetEnabled(false);
	const gfx::Caps caps;
	Canvas canvas({0, 0, 120, 24});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	control->Draw(context);
	return canvas.Finish();
}


TEST(TextSkiaTest, ChoiceStatesMatchTheirSnapshots)
{
	// What is tested: BCheckBox::Draw() off, on, partially on and disabled,
	// and BRadioButton::Draw() off and on, labeled "Option", 120 × 24, with
	// the bundled font as the default text system; rendered by Skia at
	// scale 1 and 2 against TextSkiaTest_<Kind><State>.be.skia.*.
	// Success criteria: Within the text tolerance of 21.2: the BeOS box or
	// knob 13 px from the top left at (0, 2), its mark for on and partially
	// on, the label left aligned after the label spacing, in the disabled
	// color when disabled.
	// Why it matters: The look's check box and radio button pictures in
	// every state (21.3), placed by the controls as in Haiku.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	const std::pair<const char*, const char*> cases[] = {{"CheckBox", "Off"},
		{"CheckBox", "On"}, {"CheckBox", "Partial"}, {"CheckBox", "Disabled"},
		{"RadioButton", "Off"}, {"RadioButton", "On"}};
	for (const auto& [kind, state] : cases) {
		const DisplayListRef list = ChoiceIn(*text, kind, state);
		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string(kind) + state,
				{.look = "be", .backend = "skia", .scale = scale,
					.tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


} // namespace
