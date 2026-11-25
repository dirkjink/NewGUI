/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/HaikuLook.h>

#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Application.h>
#include <ngui/LookRegistry.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>
#include <ngui/test/GTest.h>

#include "look/LookCalls.h"
#include "support/FixedTextSystem.h"
#include "support/ScopedEnvironment.h"


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

private:
	gfx::Canvas			fCanvas;
	const gfx::Caps		fCaps;
	const FixedTextSystem fText;
	DrawContext			fContext;
};


// The fixed metrics at another font size.
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


const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);


// How many recorded operations fill with a gradient.
std::size_t
GradientFills(const gfx::DisplayList& list)
{
	const std::string dump = gfx::DumpText(list);
	std::size_t count = 0;
	for (std::size_t at = dump.find("gradient="); at != std::string::npos;
			at = dump.find("gradient=", at + 1)) {
		count++;
	}
	return count;
}


TEST(HaikuLookTest, InfoAndMetricsAreHaikus)
{
	// What is tested: Info(), the label alignment and spacings, the scroll
	// bar width, the frame and background insets of every type at font size
	// 12, and the frame insets at size 16; the default registry.
	// Success criteria: id "haiku" at level C2, registered by default; left
	// and vertically centered labels; label spacing 6 and item spacing 11
	// at size 12; scroll bars 14 wide; the insets of
	// HaikuControlLook::GetFrameInsets() and GetBackgroundInsets(), including
	// the default button (5) and the pop-up indicator (1 + 11 on the right).
	// At size 16 the frame insets grow by 16 / 12, rounded up: 3 for a
	// button, 7 for the default button, 4 for a group.
	// Why it matters: The metrics drive the layout (specification 18.2);
	// unlike the BeOS look, Haiku's frames grow with the font.
	//
	// Given
	const auto look = CreateHaikuLook();
	const FixedTextSystem text;
	const SizedText larger(16);

	// When / Then
	EXPECT_EQ(look->Info().id, "haiku");
	EXPECT_EQ(look->Info().required, ConformanceLevel::C2);
	const auto ids = LookRegistry::Default().Ids();
	EXPECT_NE(std::find(ids.begin(), ids.end(), "haiku"), ids.end());
	EXPECT_EQ(look->DefaultLabelAlignment(),
		(BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Middle}));
	EXPECT_EQ(look->DefaultLabelSpacing(text), 6);
	EXPECT_EQ(look->DefaultItemSpacing(text), 11);
	EXPECT_EQ(look->ScrollBarWidth(text, BOrientation::Vertical), 14);

	EXPECT_EQ(look->FrameInsets(text, FrameType::Button, {}), (Insets{2, 2, 2, 2}));
	EXPECT_EQ(look->FrameInsets(text, FrameType::Button, ControlState::DefaultButton),
		(Insets{5, 5, 5, 5}));
	EXPECT_EQ(look->FrameInsets(text, FrameType::Group, {}), (Insets{3, 3, 3, 3}));
	EXPECT_EQ(look->FrameInsets(text, FrameType::MenuField, {}), (Insets{3, 3, 3, 3}));
	EXPECT_EQ(look->FrameInsets(text, FrameType::ScrollView, {}), (Insets{2, 2, 2, 2}));
	EXPECT_EQ(look->FrameInsets(text, FrameType::TextControl, {}), (Insets{2, 2, 2, 2}));

	EXPECT_EQ(look->BackgroundInsets(text, BackgroundType::Button, {}), (Insets{1, 1, 1, 1}));
	EXPECT_EQ(look->BackgroundInsets(text, BackgroundType::ButtonWithPopUp, {}),
		(Insets{1, 1, 12, 1}));
	EXPECT_EQ(look->BackgroundInsets(text, BackgroundType::MenuItem, {}), (Insets{1, 1, 1, 1}));
	EXPECT_EQ(look->BackgroundInsets(text, BackgroundType::HorizontalScrollBar, {}),
		(Insets{2, 0, 1, 0}));
	EXPECT_EQ(look->BackgroundInsets(text, BackgroundType::VerticalScrollBar, {}),
		(Insets{0, 2, 0, 1}));
	EXPECT_EQ(look->FrameInsets(larger, FrameType::Button, {}), (Insets{3, 3, 3, 3}));
	EXPECT_EQ(look->FrameInsets(larger, FrameType::Button, ControlState::DefaultButton),
		(Insets{7, 7, 7, 7}));
	EXPECT_EQ(look->FrameInsets(larger, FrameType::Group, {}), (Insets{4, 4, 4, 4}));
}


TEST(HaikuLookTest, ButtonIsHaikusGradientBevel)
{
	// What is tested: DrawButtonFrame() and DrawButtonBackground() of an
	// enabled button in the panel color 216 on [0, 100) × [0, 24), recorded
	// as a DisplayList.
	// Success criteria: The dump equals the snapshot
	// HaikuLookTest_Button.haiku.dump.1x.txt; the frame leaves [2, 98) ×
	// [2, 22) for the background, which leaves [3, 97) × [3, 21) for the
	// label; the background has gradients.
	// Why it matters: This is Haiku's button, line by line from
	// _DrawButtonFrame() and _DrawButtonBackground() with their rounded
	// corners and gradients; the reference pins every operation.
	//
	// Given
	const auto look = CreateHaikuLook();
	Drawing drawing;
	gfx::Rect rect{0, 0, 100, 24};

	// When
	look->DrawButtonFrame(drawing.Context(), rect, kPanel, kPanel);
	const gfx::Rect frame = rect;
	look->DrawButtonBackground(drawing.Context(), rect, kPanel);
	const gfx::DisplayListRef list = drawing.Finish();

	// Then
	EXPECT_EQ(frame, (gfx::Rect{2, 2, 98, 22}));
	EXPECT_EQ(rect, (gfx::Rect{3, 3, 97, 21}));
	EXPECT_GT(GradientFills(*list), 0u);
	EXPECT_LIST_SNAPSHOT(*list, "Button", {.look = "haiku"});
}


TEST(HaikuLookTest, EveryMethodDraws)
{
	// What is tested: Every drawing method of the look, each with no flags
	// and with all states that change the drawing (disabled, activated,
	// focused, default button, hover, partially activated).
	// Success criteria: Each call records at least one operation and returns
	// a rectangle with finite edges; nothing crashes.
	// Why it matters: The port has 3,900 lines of Haiku code behind the
	// adapter; this exercises every path from the public interface into it
	// at least once.
	//
	// Given
	const auto look = CreateHaikuLook();
	const std::vector<test::LookCall> calls = test::EveryLookCall(*look);
	const std::vector<ControlStateFlags> states = test::LookStates();

	for (const test::LookCall& call : calls) {
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


TEST(HaikuLookTest, SliderBarColorDarkensThePanel)
{
	// What is tested: SliderBarColor() with the default palette and with a
	// palette whose panel color is 200.
	// Success criteria: tint_color(panel, B_DARKEN_1_TINT): 184 for 216 and
	// 170 for 200, whatever the base color.
	// Why it matters: Haiku's look reads the panel color through ui_color();
	// here that is the palette the caller passes (L3).
	//
	// Given
	const auto look = CreateHaikuLook();
	Palette darker = Palette::Default();
	darker.Set(BColorWhich::PanelBackground, gfx::Color::FromRgba8(200, 200, 200, 255));

	// When / Then
	EXPECT_EQ(look->SliderBarColor(Palette::Default(), kPanel),
		gfx::Color::FromRgba8(184, 184, 184, 255));
	EXPECT_EQ(look->SliderBarColor(darker, {1, 0, 0, 1}),
		gfx::Color::FromRgba8(170, 170, 170, 255));
}


TEST(HaikuLookTest, TheSettingChoosesTheLookAtStart)
{
	// What is tested: A BApplication made with NGUI_CONTROL_LOOK set to
	// "haiku", then to a look that does not exist, then unset.
	// Success criteria: "haiku" becomes current; an unknown look falls back
	// to "be"; without the variable the current look stays as it is.
	// Why it matters: The look is the user's setting ngui.controlLook
	// (specification 18.4); until there are settings, the environment
	// variable lets a user start any program in the Haiku look.
	//
	// Given
	LookRegistry& registry = LookRegistry::Default();
	std::string chosen;
	std::string unknown;
	std::string kept;

	// When
	{
		const test::ScopedEnvironment variable("NGUI_CONTROL_LOOK", "haiku");
		const BApplication app("application/x-vnd.ngui-haiku-look-test");
		chosen = std::string(registry.Current()->Info().id);
	}
	{
		const test::ScopedEnvironment variable("NGUI_CONTROL_LOOK", "nonexistent");
		const BApplication app("application/x-vnd.ngui-haiku-look-test");
		unknown = std::string(registry.Current()->Info().id);
	}
	ASSERT_TRUE(registry.SetCurrent("haiku").has_value());
	{
		const BApplication app("application/x-vnd.ngui-haiku-look-test");
		kept = std::string(registry.Current()->Info().id);
	}
	ASSERT_TRUE(registry.SetCurrent("be").has_value());

	// Then
	EXPECT_EQ(chosen, "haiku");
	EXPECT_EQ(unknown, "be");
	EXPECT_EQ(kept, "haiku");
}


} // namespace
