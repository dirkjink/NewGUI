/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/text/FontTextSystem.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/BeLook.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Font.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/ListView.h>
#include <ngui/Alert.h>
#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/OutlineListView.h>
#include <ngui/Palette.h>
#include <ngui/PopUpMenu.h>
#include <ngui/RadioButton.h>
#include <ngui/ScrollBar.h>
#include <ngui/ScrollView.h>
#include <ngui/Slider.h>
#include <ngui/SplitView.h>
#include <ngui/StatusBar.h>
#include <ngui/StringItem.h>
#include <ngui/TabView.h>
#include <ngui/TextControl.h>
#include <ngui/TextView.h>
#include <ngui/ToolTip.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Render.h>

#include "support/FakePlatform.h"

#include "Rendered.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;
using namespace std::chrono_literals;


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


// \a view and its children as the window records them (BView::
// RecordTree()): the view color, Draw() in its bounds, then each child
// clipped to its frame, scrolled by its bounds' origin.
DisplayListRef
RecordTree(BView& view, const TextSystem& text)
{
	const gfx::Rect bounds = view.Bounds();
	const gfx::Caps caps;
	Canvas canvas(bounds);
	if (view.ViewColor().has_value())
		canvas.FillRect(bounds, gfx::Paint{.color = *view.ViewColor()});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	view.Draw(context);
	for (std::int32_t i = 0; i < view.CountChildren(); i++) {
		BView* child = view.ChildAt(i);
		const gfx::Rect frame = child->Frame();
		const gfx::Rect childBounds = child->Bounds();
		canvas.Save();
		canvas.ClipRect(frame);
		canvas.DrawList(RecordTree(*child, text),
			{frame.x0 - childBounds.x0, frame.y0 - childBounds.y0});
		canvas.Restore();
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


TEST(TextSkiaTest, OpenMenusMatchTheirSnapshots)
{
	// What is tested: A window with a menu bar "File" (New, Open, Recent >
	// a.txt, Quit) and "Edit": the mouse pressed on "File" and moved onto
	// "Recent"; the bar ("MenuBarOpen") and the File menu's popup
	// ("MenuPopup") recorded, with the bundled font, rendered by Skia at
	// scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the bar with
	// "File" highlighted; the popup with Haiku's one pixel border, the menu
	// background and "Recent" highlighted with its submenu arrow.
	// Why it matters: The tracking selects items, and the selection is
	// drawn by the look in the bar and in the popups.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	const auto item = [](std::string_view label) {
		return std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(1));
	};
	BWindow window({0, 0, 300, 200}, "menus");
	auto* bar = static_cast<BMenuBar*>(window.AddChild(std::make_unique<BMenuBar>(
		gfx::Rect{0, 0, 300, 20}, "bar")));
	auto file = std::make_unique<BMenu>("File");
	file->AddItem(item("New"));
	file->AddItem(item("Open"));
	auto recent = std::make_unique<BMenu>("Recent");
	recent->AddItem(item("a.txt"));
	file->AddItem(std::move(recent));
	file->AddItem(item("Quit"));
	bar->AddItem(std::move(file));
	bar->AddItem(std::make_unique<BMenu>("Edit"));
	window.Layout(false);
	const auto mouse = [&](std::uint32_t what, gfx::Point where) {
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", 1);
		window.DispatchMessage(&message, &window);
	};

	// When
	mouse(kMouseDown, {10, 8});
	const gfx::Point recentOnScreen = bar->SubmenuAt(0)->ConvertToScreen(
		{20, bar->SubmenuAt(0)->ItemAt(2)->Frame().y0 + 5});
	mouse(kMouseMoved, recentOnScreen);
	ASSERT_EQ(window.CountPopups(), 2);
	const DisplayListRef barList = RecordTree(*bar, *text);
	const DisplayListRef popupList = RecordTree(*window.PopupAt(0), *text);

	// Then
	for (const auto& [name, list] : {std::pair{"MenuBarOpen", barList},
			std::pair{"MenuPopup", popupList}}) {
		for (const float scale : {1.0f, 2.0f}) {
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, name, {.look = "be", .backend = "skia",
				.scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	mouse(kMouseUp, {290, 190});
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, MenuFieldsMatchTheirSnapshots)
{
	// What is tested: Menu fields "Size:" at a frame of 180 × 24 with a
	// fixed bar showing "Medium": as is ("MenuField"), disabled
	// ("MenuFieldDisabled") and with its menu open after a click
	// ("MenuFieldOpen"); each recorded with its bar on the panel
	// background, with the bundled font, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the label left
	// of the divider; the bar in its sunken BeOS frame with "Medium" and
	// the pop-up marker at its right; disabled, all of it dimmed; open, the
	// label on the menu selection color and "Medium" highlighted.
	// Why it matters: The menu field is drawn by the field, its bar and the
	// look together; all three must fit.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	const auto menu = [] {
		auto popUp = std::make_unique<BPopUpMenu>("size");
		for (const char* label : {"Small", "Medium", "Large"})
			popUp->AddItem(std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(1)));
		popUp->ItemAt(1)->SetMarked(true);
		return popUp;
	};
	BWindow window({0, 0, 300, 200}, "fields");
	std::array<BMenuField*, 3> fields{};
	for (std::size_t i = 0; i < fields.size(); i++) {
		const float top = 10 + 40 * static_cast<float>(i);
		fields[i] = static_cast<BMenuField*>(window.AddChild(std::make_unique<BMenuField>(
			gfx::Rect{10, top, 190, top + 24}, "field", "Size:", menu(), true)));
		fields[i]->SetDivider(40);
	}
	fields[1]->SetEnabled(false);
	window.Layout(false);
	BMessage down(kMouseDown);
	(void)down.AddPoint("where", gfx::Point{120, 100});
	(void)down.AddInt32("buttons", 1);
	window.DispatchMessage(&down, &window);
	ASSERT_TRUE(fields[2]->MenuBar()->IsTracking());

	// When / Then
	const std::array<const char*, 3> names = {"MenuField", "MenuFieldDisabled",
		"MenuFieldOpen"};
	for (std::size_t i = 0; i < fields.size(); i++) {
		const gfx::Rect bounds = fields[i]->Bounds();
		const gfx::Caps caps;
		Canvas canvas({0, 0, bounds.Width(), bounds.Height()});
		canvas.FillRect({0, 0, bounds.Width(), bounds.Height()},
			gfx::Paint{.color = Palette::Default().Get(BColorWhich::PanelBackground)});
		canvas.DrawList(RecordTree(*fields[i], *text), {0, 0});
		const DisplayListRef list = canvas.Finish();
		for (const float scale : {1.0f, 2.0f}) {
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, names[i], {.look = "be", .backend = "skia",
				.scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	BMessage up(kMouseUp);
	(void)up.AddPoint("where", gfx::Point{290, 190});
	(void)up.AddInt32("buttons", 0);
	window.DispatchMessage(&up, &window);
	window.DispatchMessage(&down, &window);
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, MenuFieldFormMatchesItsSnapshot)
{
	// What is tested: A view of 240 × 60 in a window, with a grid (spacing
	// 5, insets 5) built with AddMenuField(): "Size:" showing "Medium" and
	// "Orientation:" showing "Portrait"; laid out and recorded with its
	// children ("MenuFieldForm"), with the bundled font, rendered by Skia
	// at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: both labels in
	// the first column, both menu bars beginning at the same x in the
	// second and reaching to the right inset.
	// Why it matters: The field draws its label in the label item's frame
	// and its bar where the bar item put it; a form must line up.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	const auto menu = [](std::initializer_list<const char*> labels, int marked) {
		auto popUp = std::make_unique<BPopUpMenu>("menu");
		for (const char* label : labels)
			popUp->AddItem(std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(1)));
		popUp->ItemAt(marked)->SetMarked(true);
		return popUp;
	};
	BWindow window({0, 0, 300, 100}, "form");
	BView& form = *window.AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 240, 60}, "form",
		kFollowLeftTop, BViewFlag::WillDraw));
	BLayoutBuilder::Grid<>(&form, 5, 5)
		.SetInsets(5)
		.AddMenuField(std::make_unique<BMenuField>("size", "Size:",
			menu({"Small", "Medium", "Large"}, 1)), 0, 0)
		.AddMenuField(std::make_unique<BMenuField>("orientation", "Orientation:",
			menu({"Portrait", "Landscape"}, 0)), 0, 1);
	form.Layout(false);

	// When
	const DisplayListRef list = RecordTree(form, *text);

	// Then
	for (const float scale : {1.0f, 2.0f}) {
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *list, scale);
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "MenuFieldForm", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, TextViewsMatchTheirSnapshots)
{
	// What is tested: A focused text view of 160 × 60 in an active window
	// with "The quick brown fox jumps over the lazy dog", wrapped after
	// "jumps ": with "quick brown fox jumps ov" selected across the two
	// lines ("TextViewSelection"),
	// with the caret after "quick" ("TextViewCaret"), with an input
	// method composing "compose" there, its "pose" selected
	// ("TextViewInline"), and not editable without focus
	// ("TextViewReadOnly"); recorded with the bundled font,
	// rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the text on the
	// document background, wrapped at the view's width less the insets;
	// the selection inverted (white on black) from "quick" to the right
	// edge, the next line from the left edge to "ov"; a one pixel caret
	// of the line's height; the composed text on Haiku's light blue, its
	// selected part on light red; read only, the background a shade
	// darker.
	// Why it matters: The selection and the caret are drawn as Haiku's
	// inversions; the text is laid out by TextLayout with the font's
	// widths.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({0, 0, 300, 200}, "text");
	auto* view = static_cast<BTextView*>(window.AddChild(std::make_unique<BTextView>(
		gfx::Rect{0, 0, 160, 60}, "view", gfx::Rect{0, 0, 160, 60})));
	view->SetText("The quick brown fox jumps over the lazy dog");
	view->MakeFocus(true);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);

	const auto snapshot = [&](const char* name) {
		const DisplayListRef list = RecordTree(*view, *text);
		for (const float scale : {1.0f, 2.0f}) {
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, name, {.look = "be", .backend = "skia",
				.scale = scale, .tolerance = Tolerance::Text()});
		}
	};

	// When / Then
	view->Select(4, 28);
	snapshot("TextViewSelection");
	view->Select(9, 9);
	snapshot("TextViewCaret");
	BMessage compose(kInputMethodEvent);
	(void)compose.AddInt32("be:opcode", kInputMethodChanged);
	(void)compose.AddString("be:string", " compose");
	(void)compose.AddBool("be:confirmed", false);
	(void)compose.AddInt32("be:selection", 4);
	(void)compose.AddInt32("be:selection", 8);
	view->MessageReceived(&compose);
	snapshot("TextViewInline");
	BMessage stop(kInputMethodEvent);
	(void)stop.AddInt32("be:opcode", kInputMethodStopped);
	view->MessageReceived(&stop);
	view->MakeFocus(false);
	view->MakeEditable(false);
	snapshot("TextViewReadOnly");
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, StyledTextMatchesItsSnapshot)
{
	// What is tested: A stylable, focused text view of 200 × 70 in an
	// active window with "Plain, bold, big and red text that wraps": "bold"
	// in Noto Sans Bold, "big" at 20 pixels, "red" in red, "text" in Noto
	// Sans Mono; "and red" selected; recorded with the bundled font,
	// rendered by Skia at scale 1 and 2 ("TextViewStyled").
	// Success criteria: Within the text tolerance of 21.2: each run in its
	// font, size and color on one baseline per line; the first line as
	// high as "big" needs, the wrapped one as high as the plain font; the
	// selection inverted, the red run in its inverse (cyan).
	// Why it matters: Styled text is laid out and drawn run by run (W5f,
	// Haiku's text_run_array).
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({0, 0, 300, 200}, "text");
	auto* view = static_cast<BTextView*>(window.AddChild(std::make_unique<BTextView>(
		gfx::Rect{0, 0, 200, 70}, "view", gfx::Rect{0, 0, 200, 70})));
	view->SetStylable(true);
	view->SetText("Plain, bold, big and red text that wraps");
	const BFont bold = BFont::Bold();
	BFont big = BFont::Plain();
	big.SetSize(20);
	BFont mono = BFont::Fixed();
	const gfx::Color red = gfx::Color::FromRgba8(200, 0, 0);
	view->SetFontAndColor(7, 11, &bold);
	view->SetFontAndColor(13, 16, &big, BFontMode::Size);
	view->SetFontAndColor(21, 24, nullptr, BFontModeFlags{}, &red);
	view->SetFontAndColor(25, 29, &mono);
	view->MakeFocus(true);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);
	view->Select(17, 24);

	// When
	const DisplayListRef list = RecordTree(*view, *text);

	// Then
	for (const float scale : {1.0f, 2.0f}) {
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *list, scale);
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "TextViewStyled", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, AlertMatchesItsSnapshot)
{
	// What is tested: A warning alert "Save changes to the document
	// \"Untitled\" before closing?" with "Cancel", "Don't save" and "Save",
	// offset spacing, shown by Go(); its window recorded on its own thread
	// with the bundled font, rendered by Skia at scale 1 and 2 ("Alert").
	// Success criteria: Within the text tolerance of 21.2: Haiku's layout:
	// the darker stripe on the left and the room of the (later) icon, the
	// text wrapped beside it on the panel background, the buttons at the
	// bottom right, "Cancel" apart from the others, "Save" the default
	// button with its frame.
	// Why it matters: Alerts are the most common dialog; they are laid out
	// from Haiku's constants.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	constexpr std::uint32_t kRecord = FourCharCode("RCRD");
	class RecordingAlert : public BAlert {
	public:
		RecordingAlert(const TextSystem& textSystem)
			:
			BAlert("Alert", "Save changes to the document \"Untitled\" before closing?",
				"Cancel", "Don't save", "Save", BButtonWidth::AsUsual, BButtonSpacing::Offset,
				BAlertType::Warning),
			fText(textSystem)
		{
		}

		void MessageReceived(BMessage* message) override
		{
			if (message->what != kRecord) {
				BAlert::MessageReceived(message);
				return;
			}
			Layout(false);
			recorded = RecordTree(*RootView(), fText);
			(void)message->SendReply(BMessage(kReply));
		}

		DisplayListRef recorded;

	private:
		const TextSystem& fText;
	};
	auto owned = std::make_unique<RecordingAlert>(*text);
	RecordingAlert* alert = owned.get();
	const BMessenger messenger = *BAlert::Go(std::move(owned), [](int32) {});

	// When
	BMessage reply;
	(void)messenger.SendMessage(BMessage(kRecord), &reply);
	const DisplayListRef list = alert->recorded;

	// Then
	for (const float scale : {1.0f, 2.0f}) {
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *list, scale);
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "Alert", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "\n");
	(void)messenger.SendMessage(key);
	(void)messenger.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, ToolTipMatchesItsSnapshot)
{
	// What is tested: The text tooltip "Counts the clicks." / "Enter clicks
	// it too." of a view, shown in its window's popup; the popup recorded
	// with the bundled font, rendered by Skia at scale 1 and 2 ("ToolTip").
	// Success criteria: Within the text tolerance of 21.2: Haiku's tooltip:
	// both lines in black on the light yellow of the palette, with half the
	// item spacing around them and the one pixel border of a bordered
	// window.
	// Why it matters: Tooltips explain controls; they look like Haiku's.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({100, 100, 400, 300}, "tips");
	BView* view = window.AddChild(std::make_unique<BView>(gfx::Rect{10, 10, 110, 40}, "view",
		BResizingMode{}, BViewFlags{}));
	view->SetToolTip("Counts the clicks.\nEnter clicks it too.");

	// When
	view->ShowToolTip(view->ToolTip());
	ASSERT_EQ(window.CountPopups(), 1);
	BView* popup = window.PopupAt(0);
	popup->Layout(true);
	const DisplayListRef list = RecordTree(*popup, *text);

	// Then
	for (const float scale : {1.0f, 2.0f}) {
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *list, scale);
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "ToolTip", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, TextControlsMatchTheirSnapshots)
{
	// What is tested: Text controls "Name:" with "Haiku" at a frame of
	// 200 wide in an active window: as is ("TextControl"), focused with the
	// caret after the text ("TextControlFocused"), disabled
	// ("TextControlDisabled") and marked invalid ("TextControlInvalid");
	// each recorded with its text view on the panel background, with the
	// bundled font, rendered by Skia at scale 1 and 2.
	// Success criteria: Within the text tolerance of 21.2: the label left
	// of the divider, the field in the BeOS text control border with the
	// text on the document background; focused, the border in the focus
	// color and the caret; disabled, label, border and text dimmed and the
	// field on a mix of panel and document color; invalid, the field in
	// the failure color.
	// Why it matters: The text control is drawn by the control, its text
	// view and the look together; all must fit.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({0, 0, 300, 200}, "controls");
	std::array<BTextControl*, 4> controls{};
	for (std::size_t i = 0; i < controls.size(); i++) {
		const float top = 10 + 40 * static_cast<float>(i);
		controls[i] = static_cast<BTextControl*>(window.AddChild(std::make_unique<BTextControl>(
			gfx::Rect{10, top, 210, top + 24}, "control", "Name:", "Haiku", nullptr)));
		controls[i]->SetDivider(50);
	}
	controls[2]->SetEnabled(false);
	controls[3]->MarkAsInvalid(true);
	controls[1]->MakeFocus(true);
	controls[1]->TextView()->Select(5, 5);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);

	// When / Then
	const std::array<const char*, 4> names = {"TextControl", "TextControlFocused",
		"TextControlDisabled", "TextControlInvalid"};
	for (std::size_t i = 0; i < controls.size(); i++) {
		const gfx::Rect bounds = controls[i]->Bounds();
		const gfx::Caps caps;
		Canvas canvas({0, 0, bounds.Width(), bounds.Height()});
		canvas.FillRect({0, 0, bounds.Width(), bounds.Height()},
			gfx::Paint{.color = Palette::Default().Get(BColorWhich::PanelBackground)});
		canvas.DrawList(RecordTree(*controls[i], *text), {0, 0});
		const DisplayListRef list = canvas.Finish();
		for (const float scale : {1.0f, 2.0f}) {
			SkiaBackend backend;
			for (const FontFile& font : text->Fonts())
				ASSERT_TRUE(backend.AddFont(font.id, font.data));
			const auto image = RenderToImage(backend, *list, scale);
			ASSERT_TRUE(image.has_value()) << image.error().message;
			EXPECT_IMAGE_SNAPSHOT(*image, names[i], {.look = "be", .backend = "skia",
				.scale = scale, .tolerance = Tolerance::Text()});
		}
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, ListViewMatchesItsSnapshot)
{
	// What is tested: A list view of 140 × 80 in a scroll view with a
	// vertical bar and a fancy border, in an active window, with six string
	// items; "Deskbar" selected, "Terminal" disabled; with the bundled
	// font, rendered by Skia at scale 1 and 2 ("ListView").
	// Success criteria: Within the text tolerance of 21.2: the items one
	// below the other on the list background, the label spacing left of
	// each text; the selected item on the selection background, the
	// disabled one's text lightened; the scroll bar's thumb shows the part
	// of the list that is visible.
	// Why it matters: The list, its items and the scroll view are drawn
	// together; all must fit.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({0, 0, 300, 200}, "lists");
	auto owned = std::make_unique<BListView>(gfx::Rect{0, 0, 140, 80}, "list");
	BListView& list = *owned;
	auto& scrollView = static_cast<BScrollView&>(*window.AddChild(std::make_unique<BScrollView>(
		"scroll", std::move(owned), kFollowLeftTop, BViewFlags{}, false, true)));
	for (const char* name : {"Tracker", "Deskbar", "Terminal", "StyledEdit", "Icon-O-Matic",
		"People"}) {
		list.AddItem(std::make_unique<BStringItem>(name));
	}
	list.ItemAt(2)->SetEnabled(false);
	list.Select(1);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);
	scrollView.MoveTo({0, 0});
	const DisplayListRef recorded = RecordTree(scrollView, *text);

	for (const float scale : {1.0f, 2.0f}) {
		// When
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *recorded, scale);

		// Then
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "ListView", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, OutlineListViewMatchesItsSnapshot)
{
	// What is tested: An outline list view of 160 × 120 in a scroll view
	// with a vertical bar, in an active window: Kits (expanded) with
	// Interface (expanded, with BView and BWindow) and Storage (collapsed,
	// with BFile), then Apps; BWindow selected; with the bundled font,
	// rendered by Skia at scale 1 and 2 ("OutlineListView").
	// Success criteria: Within the text tolerance of 21.2: each level
	// indented by the item spacing; a latch pointing down left of Kits and
	// Interface, one pointing right left of Storage, none for the other
	// items, whose text still leaves room for it; BWindow on the selection
	// background from its text on; BFile hidden.
	// Why it matters: The latch, the indentation and the items are drawn
	// together; all must fit.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	BWindow window({0, 0, 300, 200}, "lists");
	auto owned = std::make_unique<BOutlineListView>(gfx::Rect{0, 0, 160, 120}, "outline");
	BOutlineListView& list = *owned;
	auto& scrollView = static_cast<BScrollView&>(*window.AddChild(std::make_unique<BScrollView>(
		"scroll", std::move(owned), kFollowLeftTop, BViewFlags{}, false, true)));
	const std::pair<const char*, uint32> items[] = {{"Kits", 0}, {"Interface", 1},
		{"BView", 2}, {"BWindow", 2}, {"Storage", 1}, {"BFile", 2}, {"Apps", 0}};
	for (const auto& [name, level] : items)
		list.AddItem(std::make_unique<BStringItem>(name, level));
	list.Collapse(list.FullListItemAt(4));
	list.Select(3);
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	window.DispatchMessage(&activated, &window);
	scrollView.MoveTo({0, 0});
	const DisplayListRef recorded = RecordTree(scrollView, *text);

	for (const float scale : {1.0f, 2.0f}) {
		// When
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *recorded, scale);

		// Then
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "OutlineListView", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


TEST(TextSkiaTest, ScrollingMenuMatchesItsSnapshot)
{
	// What is tested: On a fake screen of 300 × 160, a pop-up menu of 20
	// items, kept open, opened at (40, 20) from a view of a window; the
	// mouse moved onto the lower scroller; the popup recorded, with the
	// bundled font, rendered by Skia at scale 1 and 2
	// ("MenuPopupScrolling").
	// Success criteria: Within the text tolerance of 21.2: the popup as
	// high as the screen (its border one pixel beyond), Haiku's scrollers
	// above and below the items on the darkened menu background, both
	// arrows black as both can scroll; the items scrolled by one item.
	// Why it matters: The scrollers are drawn from Haiku's geometry; the
	// menu between them shows the part it is scrolled to.
	//
	// Given
	const auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	auto platform = std::make_unique<test::FakePlatform>();
	platform->displays = {{{0, 0, 300, 160}, 1, true}};
	BApplication app("application/x-vnd.ngui-scrolling-menu-snapshot");
	app.SetPulseRate(Clock::Time::zero());
	ASSERT_TRUE(app.UsePlatform(std::move(platform)).has_value());
	std::thread runner([&] { (void)app.Run(); });
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	ASSERT_NE(platform::Current(), nullptr);

	// A view that opens the long menu where the mouse is pressed.
	class OpeningView : public BView {
	public:
		OpeningView()
			:
			BView({0, 0, 300, 160}, "opening", {}, {}),
			fMenu(std::make_unique<BPopUpMenu>("long", false, false))
		{
			for (int i = 0; i < 20; i++) {
				fMenu->AddItem(std::make_unique<BMenuItem>("Item " + std::to_string(i),
					std::make_unique<BMessage>(1)));
			}
		}

		void MouseDown(gfx::Point where) override
		{
			(void)fMenu->Go(ConvertToScreen(where), {.keepOpen = true});
		}

	private:
		std::unique_ptr<BPopUpMenu> fMenu;
	};

	DisplayListRef recorded;
	{
		BWindow window({0, 0, 300, 160}, "menus");
		window.AddChild(std::make_unique<OpeningView>());
		const auto mouse = [&](std::uint32_t what, gfx::Point where, std::int32_t buttons) {
			BMessage message(what);
			(void)message.AddPoint("where", where);
			(void)message.AddInt32("buttons", buttons);
			window.DispatchMessage(&message, &window);
		};

		// When
		mouse(kMouseDown, {40, 20}, 1);
		mouse(kMouseUp, {40, 20}, 0);
		mouse(kMouseMoved, {60, 155}, 0);
		ASSERT_EQ(window.CountPopups(), 1);
		recorded = RecordTree(*window.PopupAt(0), *text);
		BMessage escape(kKeyDown);
		(void)escape.AddString("bytes", std::string(1, keys::kEscape));
		window.DispatchMessage(&escape, &window);
	}
	(void)AppMessenger().SendMessage(kQuitRequested);
	runner.join();

	// Then
	for (const float scale : {1.0f, 2.0f}) {
		SkiaBackend backend;
		for (const FontFile& font : text->Fonts())
			ASSERT_TRUE(backend.AddFont(font.id, font.data));
		const auto image = RenderToImage(backend, *recorded, scale);
		ASSERT_TRUE(image.has_value()) << image.error().message;
		EXPECT_IMAGE_SNAPSHOT(*image, "MenuPopupScrolling", {.look = "be", .backend = "skia",
			.scale = scale, .tolerance = Tolerance::Text()});
	}
	TextSystem::SetDefault(nullptr);
}


} // namespace
