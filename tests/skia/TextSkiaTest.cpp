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
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/Menu.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/RadioButton.h>
#include <ngui/ScrollBar.h>
#include <ngui/ScrollView.h>
#include <ngui/Slider.h>
#include <ngui/SplitView.h>
#include <ngui/StatusBar.h>
#include <ngui/TabView.h>
#include <ngui/Window.h>
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


// A slider in \a kind ("Horizontal", "Triangle", "Vertical"), at its
// preferred size along the cross axis, drawn by itself.
DisplayListRef
SliderIn(const TextSystem& text, std::string_view kind, gfx::Size& size)
{
	std::unique_ptr<BSlider> slider;
	if (kind == "Vertical") {
		slider = std::make_unique<BSlider>("s", "Vol", nullptr, 0, 100,
			BOrientation::Vertical);
		slider->SetValue(25);
		size = {slider->PreferredSize().w, 120};
	} else if (kind == "Triangle") {
		slider = std::make_unique<BSlider>("s", "Balance", nullptr, 0, 100,
			BOrientation::Horizontal, BThumbStyle::Triangle);
		const gfx::Color fill = Palette::Default().Get(BColorWhich::KeyboardNavigation);
		slider->UseFillColor(true, &fill);
		slider->SetValue(70);
		size = {200, slider->PreferredSize().h};
	} else {
		slider = std::make_unique<BSlider>("s", "Volume", nullptr, 0, 100,
			BOrientation::Horizontal);
		slider->SetLimitLabels("Low", "High");
		slider->SetHashMarks(BHashMarkLocation::Both);
		slider->SetHashMarkCount(5);
		slider->SetValue(30);
		size = {200, slider->PreferredSize().h};
	}
	slider->ResizeTo(size.w, size.h);
	slider->SetValue(slider->Value() + 1);
	slider->SetValue(slider->Value() - 1);
	const gfx::Caps caps;
	Canvas canvas({0, 0, size.w, size.h});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	slider->Draw(context);
	return canvas.Finish();
}


TEST(TextSkiaTest, SlidersMatchTheirSnapshots)
{
	// What is tested: BSlider::Draw() of a horizontal slider with label,
	// limit labels and hash marks on both sides at 30; of a triangle thumb
	// with a fill color at 70; of a vertical slider at 25; with the bundled
	// font as the default text system, rendered by Skia at scale 1 and 2
	// against TextSkiaTest_Slider<Kind>.be.skia.*.
	// Success criteria: Within the text tolerance of 21.2: the BeOS bar
	// filled up to the thumb, the thumb at the value, hash marks, labels
	// where Haiku draws them.
	// Why it matters: The slider combines most of the look's slider methods
	// with its own geometry; both must match (21.3).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const char* kind : {"Horizontal", "Triangle", "Vertical"}) {
		gfx::Size size;
		const DisplayListRef list = SliderIn(*text, kind, size);
		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("Slider") + kind,
				{.look = "be", .backend = "skia", .scale = scale,
					.tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, StatusBarsMatchTheirSnapshots)
{
	// What is tested: BStatusBar::Draw() at 30 of 100 with label, text,
	// trailing text and trailing label, 200 wide ("StatusBar") and 120 wide
	// so that the texts are shortened ("StatusBarTruncated"); with the
	// bundled font, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the left text
	// and the right text above the BeOS bar, filled to 30 %; at 120 the
	// left text cut at its end, the right one whole.
	// Why it matters: The texts and the fill are where Haiku puts them.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const float width : {200.0f, 120.0f}) {
		BStatusBar bar("bar", "Copying: ", " of 10");
		bar.SetTo(30, "file.txt", "3");
		const gfx::Size size{width, bar.PreferredSize().h};
		bar.ResizeTo(size.w, size.h);
		const gfx::Caps caps;
		Canvas canvas({0, 0, size.w, size.h});
		DrawContext context{canvas, caps, Palette::Default(), *text, 1};
		bar.Draw(context);
		const DisplayListRef list = canvas.Finish();
		const std::string name = width > 150 ? "StatusBar" : "StatusBarTruncated";

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, name, {.look = "be", .backend = "skia",
				.scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, BoxesMatchTheirSnapshots)
{
	// What is tested: BBox::Draw() of a fancy box 140 × 60 with the label
	// "Options" ("BoxFancy"), a plain box 140 × 60 ("BoxPlain") and a fancy
	// box 140 × 2, a horizontal separator ("BoxSeparator"), each on its
	// view color; with the bundled font, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the BeOS group
	// frame starting half the label height down, interrupted by the bold
	// label at x 10; the plain light and shadow lines; one etched line.
	// Why it matters: Boxes frame most groups of controls; the label gap
	// is clipped out of the frame (Canvas::ClipOutRect()).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const char* kind : {"Fancy", "Plain", "Separator"}) {
		const std::string_view name = kind;
		BBox box(name, BViewFlag::WillDraw,
			name == "Plain" ? BBorderStyle::Plain : BBorderStyle::Fancy);
		if (name == "Fancy")
			box.SetLabel("Options");
		const gfx::Size size{140, name == "Separator" ? 2.0f : 60.0f};
		box.ResizeTo(size.w, size.h);
		const gfx::Caps caps;
		Canvas canvas({0, 0, size.w, size.h});
		DrawContext context{canvas, caps, Palette::Default(), *text, 1};
		// the view color, as the window fills it
		canvas.FillRect({0, 0, size.w, size.h}, gfx::Paint{.color = *box.ViewColor()});
		box.Draw(context);
		const DisplayListRef list = canvas.Finish();

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("Box") + kind, {.look = "be",
				.backend = "skia", .scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


// \a view and its children as the window records them: the view color,
// Draw(), then each child at its frame.
DisplayListRef
RecordTree(BView& view, const TextSystem& text)
{
	const gfx::Rect bounds = view.Bounds();
	const gfx::Caps caps;
	Canvas canvas({0, 0, bounds.Width(), bounds.Height()});
	if (view.ViewColor().has_value()) {
		canvas.FillRect({0, 0, bounds.Width(), bounds.Height()},
			gfx::Paint{.color = *view.ViewColor()});
	}
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	view.Draw(context);
	for (std::int32_t i = 0; i < view.CountChildren(); i++) {
		BView* child = view.ChildAt(i);
		canvas.DrawList(RecordTree(*child, text), {child->Frame().x0, child->Frame().y0});
	}
	return canvas.Finish();
}


TEST(TextSkiaTest, ScrollViewsMatchTheirSnapshots)
{
	// What is tested: A scroll view with both bars and a fancy border
	// around a target of 120 × 80, the vertical bar at 300 of 0 .. 1000 with
	// a proportion of 0.3 ("ScrollView"); and one with a plain border and
	// only a horizontal bar at 0 ("ScrollViewPlain"); recorded with their
	// children, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the BeOS frame
	// around target and bars, arrows at both ends of each bar, the thumbs
	// where the values put them, the corner between the bars.
	// Why it matters: The scroll bar draws itself from Haiku's geometry
	// with the look's pieces; both must fit (21.3).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const char* kind : {"", "Plain"}) {
		const bool plain = std::string_view(kind) == "Plain";
		BScrollView scrollView("scroll", std::make_unique<BView>(gfx::Rect{0, 0, 120, 80},
				"target", kFollowLeftTop, BViewFlag::WillDraw), kFollowLeftTop, {}, true,
			!plain, plain ? BBorderStyle::Plain : BBorderStyle::Fancy);
		scrollView.Target()->SetViewColor(gfx::Color::FromRgba8(255, 255, 255));
		if (!plain) {
			BScrollBar* vertical = scrollView.ScrollBar(BOrientation::Vertical);
			vertical->SetProportion(0.3f);
			vertical->SetValue(300);
		}
		scrollView.MoveTo({0, 0});
		const DisplayListRef list = RecordTree(scrollView, *text);

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("ScrollView") + kind, {.look = "be",
				.backend = "skia", .scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, SplitViewsMatchTheirSnapshots)
{
	// What is tested: A horizontal split view 160 × 60 with two white panes
	// ("SplitView") and a vertical one 80 × 100 whose splitter is being
	// dragged ("SplitViewVertical"), laid out and recorded with their
	// children, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the BeOS splitter
	// with its grip between the panes, pressed while dragged; the spacing
	// on both sides in the view color.
	// Why it matters: The splitter is drawn by the look in the frame the
	// split layout gives it; both must fit.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const char* kind : {"", "Vertical"}) {
		const bool vertical = std::string_view(kind) == "Vertical";
		BWindow window({0, 0, 300, 200}, "split");
		auto* split = static_cast<BSplitView*>(window.AddChild(std::make_unique<BSplitView>(
			vertical ? BOrientation::Vertical : BOrientation::Horizontal)));
		for (const char* name : {"first", "second"}) {
			auto pane = std::make_unique<BView>(name);
			pane->SetViewColor(gfx::Color::FromRgba8(255, 255, 255));
			split->AddChild(std::move(pane), 1.0f);
		}
		split->ResizeTo(vertical ? 80.0f : 160.0f, vertical ? 100.0f : 60.0f);
		split->Layout(true);
		if (vertical)
			split->MouseDown({40, 50});
		const DisplayListRef list = RecordTree(*split, *text);

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("SplitView") + kind, {.look = "be",
				.backend = "skia", .scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, TabViewsMatchTheirSnapshots)
{
	// What is tested: A tab view 260 × 110 for a layout with the tabs
	// "General", "Advanced" and "About", the second selected ("TabView");
	// and one with the tabs at the bottom, as wide as their labels, and a
	// plain border ("TabViewBottom"); with the bundled font, laid out and
	// recorded with their children, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the BeOS tabs
	// with the selected one in front and joined to the box, the labels
	// centered, the frame around the page.
	// Why it matters: The tab view combines the look's tab methods with
	// Haiku's tab geometry.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());

	for (const char* kind : {"", "Bottom"}) {
		const bool bottom = std::string_view(kind) == "Bottom";
		BWindow window({0, 0, 400, 300}, "tabs");
		auto* tabs = static_cast<BTabView*>(window.AddChild(std::make_unique<BTabView>(
			"tabs", bottom ? BButtonWidth::FromLabel : BButtonWidth::FromWidest)));
		if (bottom) {
			tabs->SetTabSide(BTabSide::Bottom);
			tabs->SetBorder(BBorderStyle::Plain);
		}
		for (const char* name : {"General", "Advanced", "About"})
			tabs->AddTab(std::make_unique<BView>(name));
		tabs->Select(bottom ? 0 : 1);
		tabs->ResizeTo(260, 110);
		tabs->Layout(true);
		const DisplayListRef list = RecordTree(*tabs, *text);

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("TabView") + kind, {.look = "be",
				.backend = "skia", .scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, MenusMatchTheirSnapshots)
{
	// What is tested: A column menu with "New" (Command+N), "Save as"
	// (Command+Shift+S), "Autosave" marked, "Revert" disabled, a separator,
	// a submenu "Recent" and "Quit" (Command+Q) ("MenuColumn"); and a row
	// with "File", "Edit" and "Help" with the triggers shown ("MenuRow");
	// with the bundled font, laid out and drawn, rendered by Skia at
	// scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the BeOS menu
	// background, the labels after the left margin, Haiku's check mark,
	// the disabled label lighter, the shortcut characters right aligned
	// with Haiku's "Alt" and "Shift" key symbols, the etched separator, the
	// submenu arrow; in the row the first letters underlined.
	// Why it matters: Menus draw most of their content themselves, from
	// Haiku's geometry and symbols.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	const auto item = [](std::string_view label, uint32 shortcut = 0,
			BModifierFlags modifiers = {}) {
		return std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(1), shortcut,
			modifiers);
	};

	for (const char* kind : {"Column", "Row"}) {
		const bool row = std::string_view(kind) == "Row";
		BMenu menu("menu", row ? BMenuLayout::InRow : BMenuLayout::InColumn);
		if (row) {
			menu.AddItem(item("File"));
			menu.AddItem(item("Edit"));
			menu.AddItem(item("Help"));
			menu.SetTriggersEnabled(true);
		} else {
			menu.AddItem(item("New", 'N', BModifier::Command));
			menu.AddItem(item("Save as", 'S', BModifier::Command | BModifier::Shift));
			menu.AddItem(item("Autosave"));
			menu.ItemAt(2)->SetMarked(true);
			menu.AddItem(item("Revert"));
			menu.ItemAt(3)->SetEnabled(false);
			menu.AddSeparatorItem();
			auto recent = std::make_unique<BMenu>("Recent");
			recent->AddItem(item("a.txt"));
			menu.AddItem(std::move(recent));
			menu.AddItem(item("Quit", 'Q', BModifier::Command));
		}
		menu.Layout(true);
		const DisplayListRef list = RecordTree(menu, *text);

		for (const float scale : {1.0f, 2.0f}) {
			// When
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);

			// Then
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, std::string("Menu") + kind, {.look = "be",
				.backend = "skia", .scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


} // namespace
