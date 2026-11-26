/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/FlatLook.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/HaikuLook.h>
#include <ngui/LookRegistry.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>
#include <ngui/test/GTest.h>

#include "look/LookCalls.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using ngui::test::FixedTextSystem;


// What \a draw records on a Canvas of 100 × 40 with the default palette and
// the fixed metrics, as text, and the rectangle it leaves.
std::string
Recorded(const test::LookCall& call, ControlStateFlags state, gfx::Rect* left = nullptr)
{
	gfx::Canvas canvas({0, 0, 100, 40});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	gfx::Rect rect{0, 0, 100, 40};
	call.draw(context, rect, state);
	if (left != nullptr)
		*left = rect;
	return gfx::DumpText(*canvas.Finish());
}


const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);


// The drawing methods FlatControlLook overrides (FlatControlLook.h), by the
// names of test::EveryLookCall() (DrawTabs draws the active tab).
const std::set<std::string> kOverridden = {"DrawButtonFrame", "DrawButtonBackground",
	"DrawMenuBarBackground", "DrawMenuFieldFrame", "DrawMenuFieldBackground",
	"DrawMenuBackground", "DrawMenuItemBackground", "DrawScrollBarBackground",
	"DrawScrollViewFrame", "DrawSliderBar", "DrawSliderThumb", "DrawTabs", "DrawSplitter",
	"DrawBorder", "DrawRaisedBorder", "DrawGroupFrame", "DrawTextControlBorder",
	"DrawButtonWithPopUpBackground", "DrawScrollBarButton", "DrawScrollBarThumb",
	"DrawScrollBarBorder"};


TEST(FlatLookTest, InfoAndMetricsAreTheHaikuLooks)
{
	// What is tested: Info(); the default registry; the label alignment and
	// spacings, the scroll bar width and the insets of every frame and
	// background type, at font size 12 and 16, with and without the default
	// button state, compared with the Haiku look.
	// Success criteria: id "flat", named "Flat", at level C2, registered by
	// default and available on a backend of the core profile; every metric
	// equals the Haiku look's, from which the flat look derives without
	// changing them.
	// Why it matters: Switching between the Haiku and the flat look must not
	// move a single view (specification 18.2).
	//
	// Given
	const auto flat = CreateFlatLook();
	const auto haiku = CreateHaikuLook();
	const FixedTextSystem text;
	class Larger : public FixedTextSystem {
	public:
		float FontSize(FontRole) const override
		{
			return 16;
		}
	} larger;

	// When / Then
	EXPECT_EQ(flat->Info().id, "flat");
	EXPECT_EQ(flat->Info().displayName, "Flat");
	EXPECT_EQ(flat->Info().required, ConformanceLevel::C2);
	const auto ids = LookRegistry::Default().Ids();
	EXPECT_NE(std::find(ids.begin(), ids.end(), "flat"), ids.end());
	const auto available = LookRegistry::Default().Available();
	EXPECT_TRUE(std::any_of(available.begin(), available.end(),
		[](const LookInfo& info) { return info.id == "flat"; }));
	EXPECT_EQ(flat->DefaultLabelAlignment(), haiku->DefaultLabelAlignment());
	for (const TextSystem* metrics : {static_cast<const TextSystem*>(&text),
			static_cast<const TextSystem*>(&larger)}) {
		EXPECT_EQ(flat->DefaultLabelSpacing(*metrics), haiku->DefaultLabelSpacing(*metrics));
		EXPECT_EQ(flat->DefaultItemSpacing(*metrics), haiku->DefaultItemSpacing(*metrics));
		for (const BOrientation orientation : {BOrientation::Horizontal,
				BOrientation::Vertical}) {
			EXPECT_EQ(flat->ScrollBarWidth(*metrics, orientation),
				haiku->ScrollBarWidth(*metrics, orientation));
		}
		for (const ControlStateFlags state : {ControlStateFlags{},
				ControlStateFlags{ControlState::DefaultButton}}) {
			for (const FrameType type : {FrameType::Button, FrameType::Group,
					FrameType::MenuField, FrameType::ScrollView, FrameType::TextControl}) {
				EXPECT_EQ(flat->FrameInsets(*metrics, type, state),
					haiku->FrameInsets(*metrics, type, state))
					<< static_cast<int>(type);
			}
			for (const BackgroundType type : {BackgroundType::Button,
					BackgroundType::ButtonWithPopUp, BackgroundType::Menu,
					BackgroundType::MenuBar, BackgroundType::MenuField, BackgroundType::MenuItem,
					BackgroundType::HorizontalScrollBar, BackgroundType::VerticalScrollBar}) {
				EXPECT_EQ(flat->BackgroundInsets(*metrics, type, state),
					haiku->BackgroundInsets(*metrics, type, state))
					<< static_cast<int>(type);
			}
		}
	}
}


TEST(FlatLookTest, OnlyTheOverriddenMethodsDrawDifferently)
{
	// What is tested: Every drawing method of the flat and the Haiku look in
	// all states of test::LookStates(), the recorded operations compared.
	// Success criteria: The 21 methods FlatControlLook overrides draw
	// differently from the Haiku look in some state; the 7 it inherits (the
	// status bar, check box, radio button, arrow, slider triangle, hash
	// marks and label) draw exactly the same in every state.
	// Why it matters: The flat look is a subclass of the ported Haiku look;
	// this shows that every override reaches the adapter and that nothing
	// else changed on the way.
	//
	// Given
	const auto flat = CreateFlatLook();
	const auto haiku = CreateHaikuLook();
	const std::vector<test::LookCall> flatCalls = test::EveryLookCall(*flat);
	const std::vector<test::LookCall> haikuCalls = test::EveryLookCall(*haiku);
	ASSERT_EQ(flatCalls.size(), haikuCalls.size());

	// When
	std::set<std::string> different;
	std::set<std::string> same;
	for (std::size_t i = 0; i < flatCalls.size(); i++) {
		bool differs = false;
		for (const ControlStateFlags state : test::LookStates())
			differs = differs || Recorded(flatCalls[i], state) != Recorded(haikuCalls[i], state);
		(differs ? different : same).insert(flatCalls[i].name);
	}

	// Then
	EXPECT_EQ(different, kOverridden);
	EXPECT_EQ(same, (std::set<std::string>{"DrawStatusBar", "DrawCheckBox", "DrawRadioButton",
		"DrawArrowShape", "DrawSliderTriangle", "DrawSliderHashMarks", "DrawLabel"}));
}


TEST(FlatLookTest, EveryMethodDraws)
{
	// What is tested: Every drawing method of the flat look, each in every
	// state of test::LookStates().
	// Success criteria: Each call records at least one operation and leaves
	// a rectangle with finite edges; nothing crashes.
	// Why it matters: The port has 2,200 lines of Haiku code behind the
	// adapter; this exercises every path from the public interface into it.
	//
	// Given
	const auto look = CreateFlatLook();

	for (const test::LookCall& call : test::EveryLookCall(*look)) {
		for (const ControlStateFlags state : test::LookStates()) {
			// When
			gfx::Rect rect;
			const std::string recorded = Recorded(call, state, &rect);

			// Then
			EXPECT_FALSE(recorded.empty()) << call.name << " flags " << state.Raw();
			EXPECT_TRUE(std::isfinite(rect.x0) && std::isfinite(rect.y0)
				&& std::isfinite(rect.x1) && std::isfinite(rect.y1))
				<< call.name << " flags " << state.Raw();
		}
	}
}


TEST(FlatLookTest, ButtonIsFlat)
{
	// What is tested: DrawButtonFrame() and DrawButtonBackground() of an
	// enabled button in the panel color 216 on [0, 100) × [0, 24), recorded
	// as a DisplayList.
	// Success criteria: The dump equals the snapshot
	// FlatLookTest_Button.flat.dump.1x.txt; frame and background leave the
	// same rectangles as the Haiku look's ([2, 98) × [2, 22), then
	// [3, 97) × [3, 21)).
	// Why it matters: The button is the flat look's most visible change;
	// the reference pins every operation of the port.
	//
	// Given
	const auto look = CreateFlatLook();
	gfx::Canvas canvas({0, 0, 100, 40});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	gfx::Rect rect{0, 0, 100, 24};

	// When
	look->DrawButtonFrame(context, rect, kPanel, kPanel);
	const gfx::Rect frame = rect;
	look->DrawButtonBackground(context, rect, kPanel);
	const gfx::DisplayListRef list = canvas.Finish();

	// Then
	EXPECT_EQ(frame, (gfx::Rect{2, 2, 98, 22}));
	EXPECT_EQ(rect, (gfx::Rect{3, 3, 97, 21}));
	EXPECT_LIST_SNAPSHOT(*list, "Button", {.look = "flat"});
}


TEST(FlatLookTest, SliderBarColorFollowsTheBase)
{
	// What is tested: SliderBarColor() with a light base (the panel color)
	// and a dark one, with the default palette.
	// Success criteria: The panel color tinted by 1.05 for a light base and
	// by 0.95 for a dark one, as FlatControlLook::SliderBarColor() does.
	// Why it matters: Unlike the Haiku look, the flat look's bar depends on
	// the base color, so sliders stay visible in dark palettes.
	//
	// Given
	const auto look = CreateFlatLook();

	// When / Then
	EXPECT_EQ(look->SliderBarColor(Palette::Default(), kPanel), TintColor(kPanel, 1.05f));
	EXPECT_EQ(look->SliderBarColor(Palette::Default(), gfx::Color::FromRgba8(40, 40, 40, 255)),
		TintColor(kPanel, 0.95f));
}


} // namespace
