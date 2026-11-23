/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/BeLook.h>

#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>
#include <ngui/test/GTest.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using ngui::test::FixedTextSystem;


// A Canvas of 100 × 40 and a DrawContext over it with the default palette
// and the fixed metrics (size 12, ascent 9, descent 3, 7 px per byte).
class Drawing {
public:
	Drawing()
		:
		fCanvas({0, 0, 100, 40}),
		fContext{fCanvas, fCaps, Palette::Default(), fText, 1}
	{
	}

	DrawContext& Context()
	{
		return fContext;
	}

	gfx::DisplayListRef Finish()
	{
		return fCanvas.Finish();
	}

	std::string Dump()
	{
		return gfx::DumpText(*fCanvas.Finish());
	}

private:
	gfx::Canvas			fCanvas;
	const gfx::Caps		fCaps;
	const FixedTextSystem fText;
	DrawContext			fContext;
};


const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);


TEST(BeLookTest, InfoAndMetricsAreHaikus)
{
	// What is tested: Info(), the label alignment and spacings, the scroll
	// bar width, and the frame and background insets of every type.
	// Success criteria: id "be" at level C1; left and vertically centered
	// labels; with font size 12 a label spacing of ceil(12 / 2) = 6 and an
	// item spacing of ceil(12 * 0.85) = 11; scroll bars 14 wide; the insets of
	// BeControlLook::GetFrameInsets() and GetBackgroundInsets(), including
	// the default button (5) and the pop-up indicator (1 + 11 on the right).
	// Why it matters: The metrics drive the layout (specification 18.2); a
	// wrong mapping of FrameType or BackgroundType onto Haiku's enums would
	// show here as a swapped value.
	//
	// Given
	const auto look = CreateBeLook();
	const FixedTextSystem text;

	// When / Then
	EXPECT_EQ(look->Info().id, "be");
	EXPECT_EQ(look->Info().required, ConformanceLevel::C1);
	EXPECT_EQ(look->DefaultLabelAlignment(),
		(BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle}));
	EXPECT_EQ(look->DefaultLabelSpacing(text), 6);
	EXPECT_EQ(look->DefaultItemSpacing(text), 11);
	EXPECT_EQ(look->ScrollBarWidth(text, BOrientation::Vertical), 14);

	EXPECT_EQ(look->FrameInsets(FrameType::Button, {}), (Insets{2, 2, 2, 2}));
	EXPECT_EQ(look->FrameInsets(FrameType::Button, ControlState::DefaultButton),
		(Insets{5, 5, 5, 5}));
	EXPECT_EQ(look->FrameInsets(FrameType::Group, {}), (Insets{3, 3, 3, 3}));
	EXPECT_EQ(look->FrameInsets(FrameType::MenuField, {}), (Insets{3, 3, 3, 3}));
	EXPECT_EQ(look->FrameInsets(FrameType::ScrollView, {}), (Insets{2, 2, 2, 2}));
	EXPECT_EQ(look->FrameInsets(FrameType::TextControl, {}), (Insets{2, 2, 2, 2}));

	EXPECT_EQ(look->BackgroundInsets(BackgroundType::Button, {}), (Insets{1, 1, 1, 1}));
	EXPECT_EQ(look->BackgroundInsets(BackgroundType::ButtonWithPopUp, {}),
		(Insets{1, 1, 12, 1}));
	EXPECT_EQ(look->BackgroundInsets(BackgroundType::MenuItem, {}), (Insets{1, 1, 1, 1}));
	EXPECT_EQ(look->BackgroundInsets(BackgroundType::HorizontalScrollBar, {}),
		(Insets{2, 0, 1, 0}));
	EXPECT_EQ(look->BackgroundInsets(BackgroundType::VerticalScrollBar, {}),
		(Insets{0, 2, 0, 1}));
	EXPECT_EQ(look->FrameAndBackgroundInsets(FrameType::Button, BackgroundType::Button, {}),
		(Insets{3, 3, 3, 3}));
}


TEST(BeLookTest, ButtonFrameIsHaikusBevel)
{
	// What is tested: DrawButtonFrame() of an enabled button in the panel
	// color 216 on [0, 100) × [0, 24), recorded as a DisplayList.
	// Success criteria: The dump equals the snapshot
	// BeLookTest_ButtonFrame.be.dump.1x.txt: a ring in the
	// background color, then one line array of 16 lines: the dark border in
	// tint_color(216, B_DARKEN_4_TINT) = 96, the light bevel in
	// tint_color(216, B_LIGHTEN_1_TINT) + 1 = 232, white, the shadow in
	// B_DARKEN_2_TINT = 152, all through the pixel centers. The rectangle
	// comes back as [5, 95) × [4, 20), the part left for the background.
	// Why it matters: This is the BeOS button, line by line from Haiku's
	// _DrawButtonFrame(); the reference pins the conversion of inclusive
	// coordinates (17.1) and the rectangle passed back (L6). Haiku's source
	// comments darken2 as 158, a value of an older tint; the formula gives 152.
	//
	// Given
	const auto look = CreateBeLook();
	Drawing drawing;
	gfx::Rect rect{0, 0, 100, 24};

	// When
	look->DrawButtonFrame(drawing.Context(), rect, kPanel, kPanel);

	// Then
	EXPECT_EQ(rect, (gfx::Rect{5, 4, 95, 20}));
	EXPECT_LIST_SNAPSHOT(*drawing.Finish(), "ButtonFrame", {.look = "be"});
}


TEST(BeLookTest, PressedButtonIsDarkenedInsteadOfInverted)
{
	// What is tested: DrawButtonBackground() of a released and of a pressed
	// (activated) button.
	// Success criteria: Both fill the rectangle with
	// tint_color(216, B_LIGHTEN_1_TINT) = 231; the pressed one adds a
	// rectangle 3 px larger on every side in B_DARKEN_2_TINT = 152 at alpha
	// 128, and nothing in the list depends on B_OP_INVERT.
	// Why it matters: The core profile has no inversion (specification 16);
	// the replacement is the documented deviation of the BeOS look, with a
	// reference of its own (21).
	//
	// Given
	const auto look = CreateBeLook();
	Drawing released;
	Drawing pressed;
	gfx::Rect releasedRect{5, 4, 95, 20};
	gfx::Rect pressedRect = releasedRect;

	// When
	look->DrawButtonBackground(released.Context(), releasedRect, kPanel);
	look->DrawButtonBackground(pressed.Context(), pressedRect, kPanel,
		ControlState::Activated);

	// Then
	const std::string fill = "FillRect rect=[5 4 95 20] color=(0.906 0.906 0.906 1)";
	EXPECT_NE(released.Dump().find(fill), std::string::npos);
	const std::string dump = pressed.Dump();
	EXPECT_NE(dump.find(fill), std::string::npos) << dump;
	EXPECT_NE(dump.find("FillRect rect=[2 1 98 23] color=(0.596 0.596 0.596 0.502)"),
		std::string::npos) << dump;
}


TEST(BeLookTest, LabelsSitOnHaikusBaseline)
{
	// What is tested: DrawLabel() in a rectangle, enabled, disabled and
	// focused, with the default alignment.
	// Success criteria: In [5, 95) × [4, 20) the text block of height
	// ceil(9) + ceil(3) = 12 is centered with Haiku's integer truncation, so
	// the baseline is at 4 + 2 + 9 = 15; the text is black, disabled it is
	// disable_color(black, 216) = mix_color(…, 185) = 156; focused there is
	// an underline at the descent in the keyboard navigation color.
	// Why it matters: Labels go through the text subsystem, but where they
	// sit is Haiku's arithmetic (BLayoutUtils::AlignOnRect()); one pixel off
	// is visible on every button.
	//
	// Given
	const auto look = CreateBeLook();
	Drawing enabled;
	Drawing disabled;
	Drawing focused;
	const gfx::Rect rect{5, 4, 95, 20};

	// When
	look->DrawLabel(enabled.Context(), "OK", rect, kPanel, {});
	look->DrawLabel(disabled.Context(), "OK", rect, kPanel, ControlState::Disabled);
	look->DrawLabel(focused.Context(), "OK", rect, kPanel, ControlState::Focused);

	// Then
	const std::string enabledDump = enabled.Dump();
	EXPECT_NE(enabledDump.find("DrawGlyphRun font=1 size=12 origin=(5 15) color=(0 0 0 1)"),
		std::string::npos) << enabledDump;
	const std::string disabledDump = disabled.Dump();
	EXPECT_NE(disabledDump.find("origin=(5 15) color=(0.612 0.612 0.612 1)"),
		std::string::npos) << disabledDump;
	const gfx::Color navigation = Palette::Default().Get(BColorWhich::KeyboardNavigation);
	bool underlined = false;
	const gfx::DisplayListRef list = focused.Finish();
	for (const gfx::Op op : list->Ops()) {
		if (op.Code() != gfx::OpCode::DrawLines)
			continue;
		const gfx::Line& line = op.Lines()[0];
		underlined = line.p0 == gfx::Point{5.5f, 18.5f} && line.p1 == gfx::Point{19.5f, 18.5f}
			&& line.color == navigation;
	}
	EXPECT_TRUE(underlined) << gfx::DumpText(*list);
}


TEST(BeLookTest, EveryMethodDraws)
{
	// What is tested: Every drawing method of the look, each with no flags
	// and with all states that change the drawing (disabled, activated,
	// focused, default button, hover, partially activated).
	// Success criteria: Each call records at least one operation and returns
	// a rectangle with finite edges; nothing crashes.
	// Why it matters: The port has 3,000 lines of Haiku code behind a thin
	// adapter; this exercises every path from the public interface into it
	// at least once.
	//
	// Given
	const auto look = CreateBeLook();
	const gfx::Color text = Palette::Default().Get(BColorWhich::ControlText);
	struct Call {
		const char*	name;
		std::function<void(DrawContext&, gfx::Rect&, ControlStateFlags)> draw;
	};
	const std::vector<Call> calls = {
		{"DrawButtonFrame", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawButtonFrame(c, r, kPanel, kPanel, f);
			look->DrawButtonFrame(c, r, 3, kPanel, kPanel, f);
			look->DrawButtonFrame(c, r, 1, 2, 3, 4, kPanel, kPanel, f); }},
		{"DrawButtonBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawButtonBackground(c, r, kPanel, f);
			look->DrawButtonBackground(c, r, 3, kPanel, f, kAllBorders, BOrientation::Vertical);
			look->DrawButtonBackground(c, r, 1, 2, 3, 4, kPanel, f); }},
		{"DrawMenuBarBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawMenuBarBackground(c, r, kPanel, f); }},
		{"DrawMenuFieldFrame", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawMenuFieldFrame(c, r, kPanel, kPanel, f);
			look->DrawMenuFieldFrame(c, r, 3, kPanel, kPanel, f);
			look->DrawMenuFieldFrame(c, r, 1, 2, 3, 4, kPanel, kPanel, f); }},
		{"DrawMenuFieldBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawMenuFieldBackground(c, r, kPanel, true, f);
			look->DrawMenuFieldBackground(c, r, 3, kPanel, false, f);
			look->DrawMenuFieldBackground(c, r, 1, 2, 3, 4, kPanel, true, f);
			look->DrawMenuFieldBackground(c, r, kPanel, f); }},
		{"DrawMenuBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawMenuBackground(c, r, kPanel, f); }},
		{"DrawMenuItemBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawMenuItemBackground(c, r, kPanel, f); }},
		{"DrawStatusBar", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags) {
			look->DrawStatusBar(c, r, kPanel, text, 40); }},
		{"DrawCheckBox", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawCheckBox(c, r, kPanel, f); }},
		{"DrawRadioButton", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawRadioButton(c, r, kPanel, f); }},
		{"DrawScrollBarBackground", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			gfx::Rect second{r.x0, r.y0 + 10, r.x1, r.y1};
			look->DrawScrollBarBackground(c, r, second, kPanel, f, BOrientation::Vertical);
			look->DrawScrollBarBackground(c, r, kPanel, f, BOrientation::Horizontal); }},
		{"DrawScrollViewFrame", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawScrollViewFrame(c, r, {86, 0, 100, 26}, {0, 26, 86, 40}, kPanel,
				BBorderStyle::Fancy, f);
			look->DrawScrollViewFrame(c, r, {}, {}, kPanel, BBorderStyle::Plain, f); }},
		{"DrawArrowShape", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (ArrowDirection d : {ArrowDirection::Left, ArrowDirection::Right,
					ArrowDirection::Up, ArrowDirection::Down, ArrowDirection::LeftUp,
					ArrowDirection::RightUp, ArrowDirection::RightDown,
					ArrowDirection::LeftDown}) {
				gfx::Rect arrow = r;
				look->DrawArrowShape(c, arrow, kPanel, d, f);
			} }},
		{"DrawSliderBar", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawSliderBar(c, r, kPanel, text, kPanel, 0.5f, f, BOrientation::Horizontal);
			look->DrawSliderBar(c, r, kPanel, text, f, BOrientation::Vertical); }},
		{"DrawSliderThumb", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawSliderThumb(c, r, kPanel, f, BOrientation::Horizontal); }},
		{"DrawSliderTriangle", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			gfx::Rect other = r;
			look->DrawSliderTriangle(c, r, kPanel, f, BOrientation::Horizontal);
			look->DrawSliderTriangle(c, other, kPanel, text, f, BOrientation::Vertical); }},
		{"DrawSliderHashMarks", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawSliderHashMarks(c, r, kPanel, 5, BHashMarkLocation::Both, f,
				BOrientation::Horizontal); }},
		{"DrawTabs", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (Border side : {Border::Top, Border::Bottom, Border::Left, Border::Right}) {
				gfx::Rect frame = r;
				gfx::Rect active = r;
				gfx::Rect inactive = r;
				look->DrawTabFrame(c, frame, kPanel, f, kAllBorders, BBorderStyle::Fancy,
					side);
				look->DrawActiveTab(c, active, kPanel, f, kAllBorders, side, 1, 1, 0, 2);
				look->DrawInactiveTab(c, inactive, kPanel, f, kAllBorders, side, 0, 1, 0,
					2);
			} }},
		{"DrawSplitter", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawSplitter(c, r, kPanel, BOrientation::Vertical, f); }},
		{"DrawBorder", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawBorder(c, r, kPanel, BBorderStyle::Fancy, f);
			look->DrawBorder(c, r, kPanel, BBorderStyle::Plain, f); }},
		{"DrawRaisedBorder", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawRaisedBorder(c, r, kPanel, f); }},
		{"DrawGroupFrame", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags) {
			look->DrawGroupFrame(c, r, kPanel); }},
		{"DrawTextControlBorder", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawTextControlBorder(c, r, kPanel, f); }},
		{"DrawLabel", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawLabel(c, "Label", r, kPanel, f);
			look->DrawLabel(c, "Label", kPanel, f, {2, 15});
			look->DrawLabel(c, "Label", 0, r, kPanel, f); }},
		{"DrawButtonWithPopUpBackground",
			[&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawButtonWithPopUpBackground(c, r, kPanel, f);
			look->DrawButtonWithPopUpBackground(c, r, 3, kPanel, f);
			look->DrawButtonWithPopUpBackground(c, r, 1, 2, 3, 4, kPanel, f); }},
		{"DrawScrollBarButton", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawScrollBarButton(c, r, kPanel, text, f, ArrowDirection::Up,
				BOrientation::Vertical, true); }},
		{"DrawScrollBarThumb", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (KnobStyle knob : {KnobStyle::None, KnobStyle::Dots, KnobStyle::Lines}) {
				gfx::Rect thumb = r;
				look->DrawScrollBarThumb(c, thumb, kPanel, f, BOrientation::Vertical, knob);
				thumb = r;
				look->DrawScrollBarThumb(c, thumb, kPanel, f, BOrientation::Horizontal,
					knob);
			} }},
		{"DrawScrollBarBorder", [&](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look->DrawScrollBarBorder(c, r, kPanel, f, BOrientation::Horizontal); }},
	};
	const ControlStateFlags states[] = {
		{},
		ControlState::Disabled,
		ControlState::Activated,
		ControlState::Focused | ControlState::IsControl,
		ControlState::DefaultButton,
		ControlState::Hover | ControlState::Flat,
		ControlState::PartiallyActivated,
		ControlState::Disabled | ControlState::Activated | ControlState::DefaultButton
	};

	for (const Call& call : calls) {
		for (ControlStateFlags state : states) {
			// When
			Drawing drawing;
			gfx::Rect rect{0, 0, 100, 40};
			call.draw(drawing.Context(), rect, state);
			const gfx::DisplayListRef list = drawing.Finish();

			// Then
			EXPECT_GT(list->OpCount(), 0u) << call.name << " flags " << state.Raw();
			EXPECT_TRUE(std::isfinite(rect.x0) && std::isfinite(rect.y0)
				&& std::isfinite(rect.x1) && std::isfinite(rect.y1))
				<< call.name << " flags " << state.Raw();
		}
	}
}


TEST(BeLookTest, SliderBarColorDarkensThePanel)
{
	// What is tested: SliderBarColor() with the default palette and with a
	// palette whose panel color is 200, each for two base colors.
	// Success criteria: tint_color(panel, B_DARKEN_1_TINT): 184 for 216 and
	// 170 for 200; the base color makes no difference.
	// Why it matters: The BeOS look takes the panel color through
	// ui_color() and ignores the base; here ui_color() is the palette the
	// caller passes, so a changed palette changes the slider bar (L3).
	//
	// Given
	const auto look = CreateBeLook();
	Palette darker = Palette::Default();
	darker.Set(BColorWhich::PanelBackground, gfx::Color::FromRgba8(200, 200, 200, 255));

	// When / Then
	const gfx::Color expected = gfx::Color::FromRgba8(184, 184, 184, 255);
	EXPECT_EQ(look->SliderBarColor(Palette::Default(), kPanel), expected);
	EXPECT_EQ(look->SliderBarColor(Palette::Default(), {1, 0, 0, 1}), expected);
	const gfx::Color expectedDarker = gfx::Color::FromRgba8(170, 170, 170, 255);
	EXPECT_EQ(look->SliderBarColor(darker, kPanel), expectedDarker);
	EXPECT_EQ(look->SliderBarColor(darker, {1, 0, 0, 1}), expectedDarker);
}


TEST(BeLookTest, ScrollBarsGrowWithTheFont)
{
	// What is tested: ScrollBarWidth() with the plain font at 12, 13 and 24.
	// Success criteria: Haiku's formula max(ceil(ceil(size * 0.85) * 1.272),
	// 14): 14 for 12 (11 * 1.272 = 13.99), 16 for 13 (12 * 1.272 = 15.26),
	// 27 for 24 (21 * 1.272 = 26.71).
	// Why it matters: Scroll bars are part of the layout; with a larger font
	// they must grow as in Haiku, which needs the text system (18.2).
	//
	// Given
	class SizedText : public FixedTextSystem {
	public:
		explicit SizedText(float size)
			:
			fSize(size)
		{
		}

		float FontSize(FontRole) const override
		{
			return fSize;
		}

	private:
		float fSize;
	};
	const auto look = CreateBeLook();

	// When / Then
	EXPECT_EQ(look->ScrollBarWidth(SizedText(12), BOrientation::Vertical), 14);
	EXPECT_EQ(look->ScrollBarWidth(SizedText(13), BOrientation::Vertical), 16);
	EXPECT_EQ(look->ScrollBarWidth(SizedText(24), BOrientation::Horizontal), 27);
}


} // namespace
