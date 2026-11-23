/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Box.h>

#include <memory>

#include <gtest/gtest.h>

#include <ngui/CheckBox.h>
#include <ngui/GroupLayout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/View.h>
#include <ngui/Window.h>


namespace {


using namespace ngui;


// A view for a layout with explicit sizes: minimum 50 × 20, preferred
// 80 × 30, maximum 200 × 100.
std::unique_ptr<BView>
SizedChild()
{
	auto child = std::make_unique<BView>("child");
	child->SetExplicitMinSize({50, 20});
	child->SetExplicitPreferredSize({80, 30});
	child->SetExplicitMaxSize({200, 100});
	return child;
}


TEST(BoxTest, SizesAddTheBorderAndTheLabel)
{
	// What is tested: The sizes of boxes around a child of minimum 50 × 20,
	// preferred 80 × 30, maximum 200 × 100, with the placeholder font
	// (ascent 9, descent 3, 7.2 px per character): fancy with the label
	// "Options"; plain without label; fancy without child and label.
	// Success criteria: The label box is Haiku's (6, 0, 14 + 50.4, 9),
	// half-open (6, 0, 65.4, 10); the label is 9 + 3 + 1 = 13 high and sets
	// the top inset. So the fancy box adds 6 × 16: minimum 56 × 36, but at
	// least 65.4 + 3 = 68.4 wide for the label; preferred 86 × 46; maximum
	// 206 × 116. The plain box adds 2 × 2: 52 × 22, 82 × 32, 202 × 102.
	// The empty fancy box is 6 × 6 at least, unlimited at most. The top
	// border of the labelled box is 4.5 down (half the label box).
	// Why it matters: Haiku's preference panels group their controls in
	// boxes sized by these formulas.
	//
	// Given
	BBox fancy("fancy", BViewFlag::WillDraw, BBorderStyle::Fancy, SizedChild());
	BBox plain("plain", BViewFlag::WillDraw, BBorderStyle::Plain, SizedChild());
	BBox empty(BBorderStyle::Fancy);

	// When
	fancy.SetLabel("Options");

	// Then
	EXPECT_EQ(fancy.MinSize(), (gfx::Size{68.4f, 36}));
	EXPECT_EQ(fancy.PreferredSize(), (gfx::Size{86, 46}));
	EXPECT_EQ(fancy.MaxSize(), (gfx::Size{206, 116}));
	EXPECT_FLOAT_EQ(fancy.TopBorderOffset(), 4.5f);
	EXPECT_EQ(fancy.Label(), "Options");
	EXPECT_EQ(plain.MinSize(), (gfx::Size{52, 22}));
	EXPECT_EQ(plain.PreferredSize(), (gfx::Size{82, 32}));
	EXPECT_EQ(plain.MaxSize(), (gfx::Size{202, 102}));
	EXPECT_EQ(empty.MinSize(), (gfx::Size{6, 6}));
	EXPECT_EQ(empty.MaxSize(), (gfx::Size{kSizeUnlimited, kSizeUnlimited}));
	EXPECT_FLOAT_EQ(empty.TopBorderOffset(), 0);
}


TEST(BoxTest, LayoutPlacesTheChildAndTheLabelView)
{
	// What is tested: A fancy box 100 × 50 in a window with the label view
	// a check box "Enable" and a child for a layout of maximum 200 × 100,
	// laid out; a box for a layout and a box at a frame, each with a child
	// at a frame (20, 20, 60, 40).
	// Success criteria: The check box is at (10, 0) at its preferred size;
	// its height, 19, sets the top inset. The child fills the inner frame,
	// as it may grow: (3, 19, 97, 47). A child at a frame is only moved to
	// the inner frame's corner (3, 3), keeping its size; a box at a frame
	// does no layout, as in Haiku, and leaves it where it is.
	// Why it matters: Boxes with a check box as label (enable a group of
	// settings) are common in Haiku's preferences.
	//
	// Given
	BWindow window({0, 0, 300, 200}, "boxes");
	auto* box = static_cast<BBox*>(window.AddChild(std::make_unique<BBox>("box")));
	auto checkBox = std::make_unique<BCheckBox>("enable", "Enable", nullptr);
	const gfx::Size checkSize = checkBox->PreferredSize();
	box->SetLabel(std::move(checkBox));
	BView* child = box->AddChild(SizedChild());
	box->ResizeTo(100, 50);
	const auto frameChild = [] {
		return std::make_unique<BView>(gfx::Rect{20, 20, 60, 40}, "framed child",
			kFollowLeftTop, BViewFlag::WillDraw);
	};
	auto* laidOut = static_cast<BBox*>(window.AddChild(std::make_unique<BBox>("laid out")));
	BView* movedChild = laidOut->AddChild(frameChild());
	auto* framed = static_cast<BBox*>(window.AddChild(
		std::make_unique<BBox>(gfx::Rect{0, 60, 100, 110})));
	BView* keptChild = framed->AddChild(frameChild());

	// When
	box->Layout(true);
	laidOut->Layout(true);
	framed->Layout(true);

	// Then
	EXPECT_EQ(checkSize.h, 19);
	EXPECT_EQ(box->LabelView()->Frame(), (gfx::Rect{10, 0, 10 + checkSize.w, 19}));
	EXPECT_EQ(box->InnerFrame(), (gfx::Rect{3, 19, 97, 47}));
	EXPECT_EQ(child->Frame(), (gfx::Rect{3, 19, 97, 47}));
	EXPECT_EQ(movedChild->Frame(), (gfx::Rect{3, 3, 43, 23}));
	EXPECT_EQ(keptChild->Frame(), (gfx::Rect{20, 20, 60, 40}));
}


TEST(BoxTest, TheLabelViewStaysOutOfTheLayout)
{
	// What is tested: A box with a check box as label view gets a group
	// layout with one view; then the label is replaced by a string.
	// Success criteria: The layout has one item, the label view is not in
	// it but still the first child. The label string removes and destroys
	// the label view: one child left.
	// Why it matters: In Haiku the label view landed in the layout and
	// counted for its size; here it is only ever placed by the box.
	//
	// Given
	BBox box("box");
	box.SetLabel(std::make_unique<BCheckBox>("enable", "Enable", nullptr));
	BView* labelView = box.LabelView();

	// When
	box.SetLayout(std::make_unique<BGroupLayout>(BOrientation::Vertical));
	box.AddChild(SizedChild());

	// Then
	EXPECT_EQ(box.GetLayout()->CountItems(), 1);
	EXPECT_EQ(box.ChildAt(0), labelView);
	EXPECT_EQ(box.CountChildren(), 2);
	box.SetLabel("Text");
	EXPECT_EQ(box.LabelView(), nullptr);
	EXPECT_EQ(box.CountChildren(), 1);
}


} // namespace
