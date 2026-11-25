/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ControlLook.h>

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "StubLook.h"


namespace {


using namespace ngui;


// A text system with fixed metrics, enough for looks that only measure.
class FixedTextSystem : public TextSystem {
public:
			float				FontSize(FontRole) const override
								{
									return 12;
								}
			FontHeight			Height(FontRole) const override
								{
									return {9, 3, 1};
								}
			float				StringWidth(std::string_view text, FontRole) const override
								{
									return 7.0f * static_cast<float>(text.size());
								}
			std::string			Truncate(std::string_view text, float,
									TruncationMode, FontRole) const override
								{
									return std::string(text);
								}
			ShapedText			Shape(std::string_view, FontRole) const override
								{
									return {};
								}
};


TEST(ControlLookTest, FramesShrinkTheRectangle)
{
	// What is tested: A Draw…Frame() call followed by a Draw…Background()
	// call on the same rectangle, through a DrawContext.
	// Success criteria: Both methods are called in order; the frame shrinks
	// the rectangle, so the background gets the rest.
	// Why it matters: Rule L6: the frame consumes its border, the background
	// draws inside it; controls rely on this when they draw in two steps.
	//
	// Given (called through the base class, as controls do: the defaults
	// of the parameters and the non-virtual overloads live there)
	test::StubLook stub;
	const ControlLook& look = stub;
	gfx::Canvas canvas({0, 0, 100, 30});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context = {canvas, caps, Palette::Default(), text, 1};
	gfx::Rect rect = {0, 0, 100, 30};
	const gfx::Color base = Palette::Default().Get(BColorWhich::ControlBackground);

	// When
	look.DrawButtonFrame(context, rect, base, base, ControlState::Focused | ControlState::Hover);
	look.DrawButtonBackground(context, rect, base);

	// Then
	EXPECT_EQ(rect, (gfx::Rect{1, 1, 99, 29}));
	EXPECT_EQ(stub.Calls(), (std::vector<std::string>{"DrawButtonFrame",
		"DrawButtonBackground"}));
}


TEST(ControlLookTest, NonVirtualHelpersForward)
{
	// What is tested: The non-virtual DrawLabel() with an icon and
	// FrameAndBackgroundInsets().
	// Success criteria: DrawLabel() forwards to the virtual overload with
	// the look's default alignment; the insets are the side-wise sum of
	// frame and background insets.
	// Why it matters: Both are fixed parts of BControlLook that controls call
	// directly; a look only implements the virtual methods.
	//
	// Given
	test::StubLook stub;
	const ControlLook& look = stub;
	gfx::Canvas canvas({0, 0, 10, 10});
	const gfx::Caps caps;
	const FixedTextSystem text;
	DrawContext context = {canvas, caps, Palette::Default(), text, 1};

	// When
	look.DrawLabel(context, "OK", 0, {0, 0, 10, 10}, {0, 0, 0, 1}, {});
	const Insets insets = look.FrameAndBackgroundInsets(text, FrameType::Button,
		BackgroundType::Button, {});

	// Then
	EXPECT_EQ(stub.Calls(), (std::vector<std::string>{"DrawLabel"}));
	EXPECT_EQ(insets, (Insets{2, 4, 6, 8}));
	EXPECT_EQ(look.Info().id, "stub");
	EXPECT_EQ(look.DefaultItemSpacing(text), 5);
}


TEST(ControlLookTest, VocabularyMatchesHaiku)
{
	// What is tested: The bit values of ControlState and Border, the set
	// kAllBorders and the interface version.
	// Success criteria: The masks have Haiku's values (B_FOCUSED = 1,
	// B_BLEND_FRAME = 1 << 16, B_LEFT_BORDER = 1 …); kAllBorders holds all
	// four sides; the interface is version 2, since Flags() was added (L8).
	// Why it matters: The compatibility layer maps Haiku's constants onto
	// these (specification 17, L5); equal values keep that mapping trivial.
	//
	// When / Then
	static_assert(ControlStateFlags(ControlState::Focused).Raw() == 1u);
	static_assert(ControlStateFlags(ControlState::IsControl).Raw() == 1u << 10);
	static_assert(ControlStateFlags(ControlState::BlendFrame).Raw() == 1u << 16);
	static_assert(BorderFlags(Border::Bottom).Raw() == 8u);
	static_assert(kAllBorders.Raw() == 0b1111u);
	static_assert(kAllBorders.Has(Border::Left) && kAllBorders.Has(Border::Top));
	EXPECT_EQ(ControlLook::kInterfaceVersion, 2u);
}


} // namespace
