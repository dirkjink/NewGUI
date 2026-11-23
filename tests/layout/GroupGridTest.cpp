/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/GridLayout.h>
#include <ngui/GroupLayout.h>

#include <cmath>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/AbstractLayoutItem.h>
#include <ngui/BeLook.h>
#include <ngui/GridView.h>
#include <ngui/GroupView.h>
#include <ngui/LayoutUtils.h>
#include <ngui/SpaceLayoutItem.h>
#include <ngui/TextSystem.h>
#include <ngui/View.h>

#include "support/StackLayout.h"


namespace {


using namespace ngui;
using namespace ngui::test;


// A child view of \a min, at most \a max, added to \a layout.
BView*
AddBox(BLayout* layout, gfx::Size min, gfx::Size max = {kSizeUnlimited, kSizeUnlimited})
{
	BView* view = layout->AddView(std::make_unique<BView>("box"))->View();
	view->SetExplicitMinSize(min);
	view->SetExplicitMaxSize(max);
	return view;
}


BView*
AddCell(BGridLayout* grid, gfx::Size min, int32 column, int32 row, int32 columns = 1)
{
	BView* view = grid->AddView(std::make_unique<BView>("cell"), column, row, columns)->View();
	view->SetExplicitMinSize(min);
	return view;
}


TEST(GroupLayoutTest, ARowSharesItsWidthByWeight)
{
	// What is tested: A horizontal group with spacing 10 in a view of
	// 300 × 50: item a at least 40 × 20, item b 60 × 20 to 100 wide; laid
	// out; then a's weight set to 3 and b's maximum removed.
	// Success criteria: The minimum is 40 + 10 + 60 = 110 × 20. Of the 290
	// pixels each gets 145, b is cut to 100 and a gets the rest: a (0, 0,
	// 190, 50), b (200, 0, 300, 50). With weights 3 : 1 a gets 217 (of
	// 290 · 3/4 = 217.5) and b 73.
	// Why it matters: Rows of buttons and label/field pairs are groups;
	// these are Haiku's SimpleLayouter results through the group layout.
	//
	// Given
	BView owner({0, 0, 300, 50}, "owner");
	auto* group = static_cast<BGroupLayout*>(owner.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Horizontal, 10.0f)));
	BView* a = AddBox(group, {40, 20});
	BView* b = AddBox(group, {60, 20}, {100, kSizeUnlimited});

	// When
	const gfx::Size min = owner.MinSize();
	owner.Layout(false);
	const gfx::Rect aFrame = a->Frame();
	const gfx::Rect bFrame = b->Frame();
	group->SetItemWeight(0, 3);
	b->SetExplicitMaxSize({kSizeUnlimited, kSizeUnlimited});
	owner.Layout(false);

	// Then
	EXPECT_EQ(min, (gfx::Size{110, 20}));
	EXPECT_EQ(aFrame, (gfx::Rect{0, 0, 190, 50}));
	EXPECT_EQ(bFrame, (gfx::Rect{200, 0, 300, 50}));
	EXPECT_EQ(group->ItemWeight(0), 3);
	EXPECT_EQ(a->Frame(), (gfx::Rect{0, 0, 217, 50}));
	EXPECT_EQ(b->Frame(), (gfx::Rect{227, 0, 300, 50}));
}


TEST(GroupLayoutTest, AColumnWithInsetsGlueAndHiddenItems)
{
	// What is tested: A vertical group with spacing 5 and insets 10 in a
	// view of 100 × 200: a box of exactly 20 high, glue, a box of exactly
	// 30 high; then the first box hidden.
	// Success criteria: The minimum is 20 + 5 + 0 + 5 + 30 + 2 · 10 = 80
	// high, 20 wide (insets only). The boxes are at the top and the bottom
	// inside the insets, the glue takes the rest: (10, 10, 90, 30) and
	// (10, 160, 90, 190). Hidden, the first box takes no space and no
	// spacing: the second stays at the bottom, the minimum is 55.
	// Why it matters: Dialogs are columns with glue pushing the buttons
	// down; hidden views must not leave gaps.
	//
	// Given
	BView owner({0, 0, 100, 200}, "owner");
	auto* group = static_cast<BGroupLayout*>(owner.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Vertical, 5.0f)));
	group->SetInsets(10);
	BView* top = AddBox(group, {0, 20}, {kSizeUnlimited, 20});
	(void)group->AddItem(BSpaceLayoutItem::CreateGlue());
	BView* bottom = AddBox(group, {0, 30}, {kSizeUnlimited, 30});

	// When
	const gfx::Size min = owner.MinSize();
	owner.Layout(false);
	const gfx::Rect topFrame = top->Frame();
	const gfx::Rect bottomFrame = bottom->Frame();
	top->Hide();
	const gfx::Size minHidden = owner.MinSize();
	owner.Layout(false);

	// Then
	EXPECT_EQ(min, (gfx::Size{20, 80}));
	EXPECT_EQ(topFrame, (gfx::Rect{10, 10, 90, 30}));
	EXPECT_EQ(bottomFrame, (gfx::Rect{10, 160, 90, 190}));
	EXPECT_EQ(minHidden, (gfx::Size{20, 55}));
	EXPECT_EQ(bottom->Frame(), (gfx::Rect{10, 160, 90, 190}));
}


TEST(GroupLayoutTest, SpacingConstantsFollowTheLookAndFont)
{
	// What is tested: BLayoutUtils::ComposeSpacing() for each constant and
	// for a plain value, with the BeOS look and a 12 px font; a group with
	// the default spacing and window insets.
	// Success criteria: The item spacing is ceil(12 · 0.85) = 11; half 6,
	// small 8 (7.7), corner 14 (13.99), big 20 (19.8), border 1; 7 stays 7.
	// The group reports spacing 11 and insets 11 and adds them to its
	// minimum: two items of 10 wide are 10 + 11 + 10 + 2 · 11 = 53.
	// Why it matters: Haiku's layouts use these constants everywhere
	// (B_USE_DEFAULT_SPACING); they must give the look's metrics, also
	// after a change of font or look (resolved when used, rule L11).
	//
	// Given
	const auto look = CreateBeLook();
	const TextSystem& text = TextSystem::Default();
	BView owner({0, 0, 200, 50}, "owner");
	auto* group = static_cast<BGroupLayout*>(owner.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Horizontal)));
	group->SetInsets(kUseWindowInsets);
	(void)AddBox(group, {10, 10});
	(void)AddBox(group, {10, 10});
	float left = 0;
	float bottom = 0;

	// When
	group->GetInsets(&left, nullptr, nullptr, &bottom);

	// Then
	ASSERT_EQ(text.FontSize(FontRole::Plain), 12);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseDefaultSpacing, *look, text), 11);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseItemSpacing, *look, text), 11);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseHalfItemSpacing, *look, text), 6);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseWindowSpacing, *look, text), 11);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseSmallSpacing, *look, text), 8);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseCornerSpacing, *look, text), 14);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseBigSpacing, *look, text), 20);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseBorderSpacing, *look, text), 1);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(7, *look, text), 7);
	EXPECT_EQ(BLayoutUtils::ComposeSpacing(kUseDefaultSpacing), 11);
	EXPECT_EQ(group->Spacing(), 11);
	EXPECT_EQ(left, 11);
	EXPECT_EQ(bottom, 11);
	EXPECT_EQ(owner.MinSize(), (gfx::Size{53, 32}));
}


TEST(GridLayoutTest, CellsAndSpansAreLaidOut)
{
	// What is tested: A grid with spacing 5 in a view of 200 × 100: A at
	// (0, 0) at least 40 × 20, B at (1, 0) at least 60 × 20, C spanning
	// both columns of row 1, at least 150 × 20; then an item added without
	// a cell, and C removed.
	// Success criteria: Minimum width max(40 + 60, 145) + 5 = 150, height
	// 20 + 5 + 20 = 45. Laid out, the columns get 97 and 98 of 195 and the
	// rows 47 and 48 of 95: A (0, 0, 97, 47), B (102, 0, 200, 47), C
	// (0, 52, 200, 100). Both cells of row 1 give C. The new item goes to a
	// third column; removing C shrinks the grid to one row.
	// Why it matters: Forms are grids with labels and fields; spanning
	// cells need the ComplexLayouter (G10a).
	//
	// Given
	BView owner({0, 0, 200, 100}, "owner");
	auto* grid = static_cast<BGridLayout*>(owner.SetLayout(
		std::make_unique<BGridLayout>(5.0f, 5.0f)));
	BView* a = AddCell(grid, {40, 20}, 0, 0);
	BView* b = AddCell(grid, {60, 20}, 1, 0);
	BView* c = AddCell(grid, {150, 20}, 0, 1, 2);
	BLayoutItem* cItem = grid->ItemAt(0, 1);

	// When
	const gfx::Size min = owner.MinSize();
	owner.Layout(false);
	BLayoutItem* secondCellOfC = grid->ItemAt(1, 1);
	BLayoutItem* added = grid->AddItem(BSpaceLayoutItem::CreateGlue());
	const int32 columnsAfterAdd = grid->CountColumns();
	std::unique_ptr<BLayoutItem> removed = grid->RemoveItem(cItem);

	// Then
	EXPECT_EQ(min, (gfx::Size{150, 45}));
	EXPECT_EQ(a->Frame(), (gfx::Rect{0, 0, 97, 47}));
	EXPECT_EQ(b->Frame(), (gfx::Rect{102, 0, 200, 47}));
	EXPECT_EQ(c->Frame(), (gfx::Rect{0, 52, 200, 100}));
	EXPECT_EQ(secondCellOfC, cItem);
	EXPECT_EQ(cItem->View(), c);
	EXPECT_EQ(grid->ItemAt(2, 0), added);
	EXPECT_EQ(columnsAfterAdd, 3);
	EXPECT_EQ(removed.get(), cItem);
	EXPECT_EQ(grid->CountRows(), 1);
	EXPECT_EQ(grid->AddView(std::make_unique<BView>("taken"), 0, 0), nullptr);
	EXPECT_EQ(grid->AddView(std::make_unique<BView>("none"), 0, 3, 0), nullptr);
}


TEST(GridLayoutTest, ColumnLimitsAndEmptyColumns)
{
	// What is tested: A grid with spacing 10 in a view of 300 wide: A in
	// column 0, B in column 2 (column 1 empty), both at least 40 wide;
	// column 0 at least 80 wide; then column 2 at most 100 wide.
	// Success criteria: The empty column takes no space and no spacing:
	// minimum 80 + 10 + 40 = 130. A minimum without a maximum does not
	// hold the column at 80 (Haiku's unset maximum did): both get 145 of
	// 290. With column 2 limited, B gets 100 and A the remaining 190.
	// Why it matters: Columns of labels get a minimum width so that forms
	// line up; they must still grow with the window.
	//
	// Given
	BView owner({0, 0, 300, 50}, "owner");
	auto* grid = static_cast<BGridLayout*>(owner.SetLayout(
		std::make_unique<BGridLayout>(10.0f, 10.0f)));
	BView* a = AddCell(grid, {40, 20}, 0, 0);
	BView* b = AddCell(grid, {40, 20}, 2, 0);
	grid->SetMinColumnWidth(0, 80);

	// When
	const gfx::Size min = owner.MinSize();
	owner.Layout(false);
	const gfx::Rect aFrame = a->Frame();
	const gfx::Rect bFrame = b->Frame();
	grid->SetMaxColumnWidth(2, 100);
	owner.Layout(false);

	// Then
	EXPECT_EQ(min.w, 130);
	EXPECT_EQ(aFrame, (gfx::Rect{0, 0, 145, 50}));
	EXPECT_EQ(bFrame, (gfx::Rect{155, 0, 300, 50}));
	EXPECT_EQ(a->Frame(), (gfx::Rect{0, 0, 190, 50}));
	EXPECT_EQ(b->Frame(), (gfx::Rect{200, 0, 300, 50}));
	EXPECT_EQ(grid->MinColumnWidth(0), 80);
	EXPECT_EQ(grid->MaxColumnWidth(1), kSizeUnset);
}


TEST(GridLayoutTest, AlignedLayoutsShareTheirColumns)
{
	// What is tested: Two grids in two views of 300 wide, each with two
	// columns: in the first, column 0 holds an item of 100 to 120 wide; in
	// the second an item of at least 30. Laid out without and with
	// AlignLayoutWith(Horizontal).
	// Success criteria: Alone, the second grid splits 300 into 150 and 150.
	// Aligned, the columns of both are laid out together: column 0 is 120
	// (the first grid's maximum) in both, column 1 gets 180.
	// Why it matters: Label columns of separate boxes line up in Haiku's
	// preference windows this way.
	//
	// Given
	BView first({0, 0, 300, 50}, "first");
	BView second({0, 0, 300, 50}, "second");
	auto* firstGrid = static_cast<BGridLayout*>(first.SetLayout(
		std::make_unique<BGridLayout>(0.0f, 0.0f)));
	auto* secondGrid = static_cast<BGridLayout*>(second.SetLayout(
		std::make_unique<BGridLayout>(0.0f, 0.0f)));
	BView* wide = AddCell(firstGrid, {100, 10}, 0, 0);
	wide->SetExplicitMaxSize({120, kSizeUnlimited});
	(void)AddCell(firstGrid, {10, 10}, 1, 0);
	BView* narrow = AddCell(secondGrid, {30, 10}, 0, 0);
	(void)AddCell(secondGrid, {10, 10}, 1, 0);

	// When
	second.Layout(false);
	const gfx::Rect alone = narrow->Frame();
	secondGrid->AlignLayoutWith(firstGrid, BOrientation::Horizontal);
	first.Layout(false);
	second.Layout(false);

	// Then
	EXPECT_EQ(alone, (gfx::Rect{0, 0, 150, 50}));
	EXPECT_EQ(wide->Frame(), (gfx::Rect{0, 0, 120, 50}));
	EXPECT_EQ(narrow->Frame(), (gfx::Rect{0, 0, 120, 50}));
}


// An item whose height depends on its width, as wrapped text: it needs
// 2000 / width pixels (rounded up), at least 20 wide.
class WrapItem : public BAbstractLayoutItem {
public:
	gfx::Size BaseMinSize() override
	{
		return {20, 0};
	}

	bool HasHeightForWidth() override
	{
		return true;
	}

	void GetHeightForWidth(float width, float* min, float* max,
		float* preferred) override
	{
		const float height = std::ceil(2000 / width);
		if (min)
			*min = height;
		if (max)
			*max = height;
		if (preferred)
			*preferred = height;
	}

	bool IsVisible() override
	{
		return true;
	}

	void SetVisible(bool) override
	{
	}

	gfx::Rect Frame() override
	{
		return frame;
	}

	void SetFrame(gfx::Rect newFrame) override
	{
		frame = newFrame;
	}

	gfx::Rect frame = {0, 0, 0, 0};
};


TEST(GroupLayoutTest, HeightForWidthIsLaidOutWithTheWidth)
{
	// What is tested: A vertical group in a view of 100 × 300 holding an
	// item that needs 2000 / width pixels in height; the group's height for
	// widths 100 and 50; laid out.
	// Success criteria: The group has height for width; for 100 it needs
	// 20, for 50 it needs 40. Laid out at 100 wide, the item gets its 20
	// pixels, centered in the 300: (0, 140, 100, 160).
	// Why it matters: Wrapping labels and text views (W5) need a height
	// that depends on the width they get.
	//
	// Given
	BView owner({0, 0, 100, 300}, "owner");
	auto* group = static_cast<BGroupLayout*>(owner.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Vertical, 0.0f)));
	auto* item = static_cast<WrapItem*>(group->AddItem(std::make_unique<WrapItem>()));
	float at100 = 0;
	float at50 = 0;

	// When
	const bool hasHeightForWidth = group->HasHeightForWidth();
	group->GetHeightForWidth(100, &at100, nullptr, nullptr);
	group->GetHeightForWidth(50, &at50, nullptr, nullptr);
	owner.Layout(false);

	// Then
	EXPECT_TRUE(hasHeightForWidth);
	EXPECT_EQ(at100, 20);
	EXPECT_EQ(at50, 40);
	EXPECT_EQ(item->frame, (gfx::Rect{0, 140, 100, 160}));
}


TEST(GroupLayoutTest, GroupAndGridViewsKeepTheirKindOfLayout)
{
	// What is tested: A BGroupView and a BGridView, each given a layout of
	// the other kind and of their own kind.
	// Success criteria: Each starts with its kind of layout and refuses the
	// other kind (nullptr, layout unchanged); a layout of its kind replaces
	// the old one. Both have the panel background.
	// Why it matters: Haiku's BGroupView and BGridView guarantee their
	// layout's type to GroupLayout() and GridLayout().
	//
	// Given
	BGroupView groupView("group", BOrientation::Vertical, 3);
	BGridView gridView("grid", 4, 5);
	BGroupLayout* firstGroup = groupView.GroupLayout();
	ASSERT_NE(firstGroup, nullptr);
	const BOrientation firstOrientation = firstGroup->Orientation();
	const float firstSpacing = firstGroup->Spacing();

	// When
	BLayout* refusedGroup = groupView.SetLayout(std::make_unique<BGridLayout>());
	BLayout* refusedGrid = gridView.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Horizontal));
	BLayout* replaced = groupView.SetLayout(
		std::make_unique<BGroupLayout>(BOrientation::Horizontal));

	// Then
	EXPECT_EQ(firstOrientation, BOrientation::Vertical);
	EXPECT_EQ(firstSpacing, 3);
	EXPECT_EQ(refusedGroup, nullptr);
	EXPECT_EQ(refusedGrid, nullptr);
	EXPECT_EQ(replaced, groupView.GroupLayout());
	EXPECT_EQ(groupView.GroupLayout()->Orientation(), BOrientation::Horizontal);
	ASSERT_NE(gridView.GridLayout(), nullptr);
	EXPECT_EQ(gridView.GridLayout()->HorizontalSpacing(), 4);
	EXPECT_EQ(gridView.GridLayout()->VerticalSpacing(), 5);
	EXPECT_EQ(groupView.ViewColor(), Palette::Default().Get(BColorWhich::PanelBackground));
}


} // namespace
