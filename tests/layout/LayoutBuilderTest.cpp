/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/LayoutBuilder.h>

#include <cmath>
#include <initializer_list>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/AbstractLayoutItem.h>
#include <ngui/CardView.h>
#include <ngui/MenuBar.h>
#include <ngui/PopUpMenu.h>
#include <ngui/SeparatorItem.h>
#include <ngui/View.h>
#include <ngui/Window.h>


namespace {


using namespace ngui;


// A view of exactly \a size, its pointer in \a stored.
std::unique_ptr<BView>
Fixed(gfx::Size size, BView** stored)
{
	auto view = std::make_unique<BView>("fixed");
	view->SetExplicitSize(size);
	*stored = view.get();
	return view;
}


TEST(LayoutBuilderTest, ADialogIsBuiltInOneExpression)
{
	// What is tested: A window of 300 × 200 laid out by a builder: a
	// column with spacing 5 and insets 10 holding a label of 100 × 20, glue,
	// and a row with spacing 5 of glue and two buttons of 60 × 24; laid out.
	// Success criteria: The builder returns to its parent with End(). The
	// minimum is 150 × 74 (max(100, 60 + 5 + 60 + 5) + 2 · 10 wide,
	// 20 + 5 + 24 + 5 + 2 · 10 high). The label is centered at the top,
	// (100, 10, 200, 30); the glue takes the rest of the height; the row
	// sits at the bottom, its glue pushes the buttons right: cancel (165,
	// 166, 225, 190), ok (230, 166, 290, 190). The views are children of
	// the root view.
	// Why it matters: Haiku applications build their windows this way; the
	// builder must produce the same tree as the layout calls by hand.
	//
	// Given
	BWindow window({0, 0, 300, 200}, "dialog");
	BView* label = nullptr;
	BView* cancel = nullptr;
	BView* ok = nullptr;
	BLayoutBuilder::Group<> builder(&window, BOrientation::Vertical, 5);

	// When
	BLayoutBuilder::Group<>& ended = builder
		.SetInsets(10)
		.Add(Fixed({100, 20}, &label))
		.AddGlue()
		.AddGroup(BOrientation::Horizontal, 5)
			.AddGlue()
			.Add(Fixed({60, 24}, &cancel))
			.Add(Fixed({60, 24}, &ok))
		.End();
	const gfx::Size min = window.RootView()->MinSize();
	window.Layout(false);

	// Then
	EXPECT_EQ(&ended, &builder);
	EXPECT_EQ(builder.View(), window.RootView());
	EXPECT_EQ(min, (gfx::Size{150, 74}));
	EXPECT_EQ(label->Frame(), (gfx::Rect{100, 10, 200, 30}));
	EXPECT_EQ(cancel->Frame(), (gfx::Rect{165, 166, 225, 190}));
	EXPECT_EQ(ok->Frame(), (gfx::Rect{230, 166, 290, 190}));
	EXPECT_EQ(ok->Parent(), window.RootView());
}


TEST(LayoutBuilderTest, GridsHoldNestedGroupsAndGlue)
{
	// What is tested: A grid built on a view: views at (0, 0) and (1, 0), a
	// nested row spanning both columns of row 1 with a view in it, glue at
	// (0, 2), column 1 with weight 2.
	// Success criteria: The grid has 2 columns and 3 rows; both cells of
	// row 1 hold the nested group, whose builder has no view of its own;
	// the view in the group is a child of the grid's view; the column
	// weight is set; the view got the panel color.
	// Why it matters: Forms mix grids and groups; nested layouts share the
	// view of the outermost one.
	//
	// Given
	BView view({0, 0, 200, 100}, "form");
	BView* inGroup = nullptr;
	BView* unused = nullptr;
	BLayoutBuilder::Grid<> builder(&view, 5, 5);

	// When
	BView* groupView = builder
		.Add(Fixed({10, 10}, &unused), 0, 0)
		.Add(Fixed({10, 10}, &unused), 1, 0)
		.AddGroup(BOrientation::Horizontal, 0, 0, 1, 2)
			.Add(Fixed({30, 10}, &inGroup))
			.View();
	builder.AddGlue(0, 2).SetColumnWeight(1, 2);
	BGridLayout* grid = builder;

	// Then
	EXPECT_EQ(grid->CountColumns(), 2);
	EXPECT_EQ(grid->CountRows(), 3);
	EXPECT_NE(grid->ItemAt(0, 1), nullptr);
	EXPECT_EQ(grid->ItemAt(0, 1), grid->ItemAt(1, 1));
	EXPECT_EQ(groupView, nullptr);
	EXPECT_EQ(inGroup->Parent(), &view);
	EXPECT_EQ(grid->ColumnWeight(1), 2);
	EXPECT_EQ(view.ViewColor(), Palette::Default().Get(BColorWhich::PanelBackground));
}


TEST(LayoutBuilderTest, CardsShowOnePage)
{
	// What is tested: Cards built on a view of 200 × 100 with a page of
	// 50 × 40 and a page of at least 80 × 20; the second made visible; laid
	// out; then a group view added with AddCards() to a group.
	// Success criteria: Only the visible page is shown; the view's minimum
	// is the largest of both pages (80 × 40), so switching does not resize;
	// the visible page fills the view (its maximum is unlimited). AddCards()
	// adds a card view whose builder is on its card layout.
	// Why it matters: Wizards and tab views switch pages this way (BCardView
	// in Haiku's BTabView).
	//
	// Given
	BView view({0, 0, 200, 100}, "pages");
	BView* first = nullptr;
	BView* second = nullptr;
	BView owner({0, 0, 100, 100}, "owner");

	// When
	BLayoutBuilder::Cards<>(&view)
		.Add(Fixed({50, 40}, &first))
		.Add([&] {
			auto page = std::make_unique<BView>("second");
			page->SetExplicitMinSize({80, 20});
			second = page.get();
			return page;
		}())
		.SetVisibleItem(1);
	view.Layout(false);
	BView* cardView = BLayoutBuilder::Group<>(&owner).AddCards().View();

	// Then
	EXPECT_TRUE(first->IsHidden());
	EXPECT_FALSE(second->IsHidden());
	EXPECT_EQ(view.MinSize(), (gfx::Size{80, 40}));
	EXPECT_EQ(second->Frame(), (gfx::Rect{0, 0, 200, 100}));
	ASSERT_NE(cardView, nullptr);
	EXPECT_EQ(cardView->Parent(), &owner);
	EXPECT_NE(dynamic_cast<BCardView*>(cardView), nullptr);
}


// An item whose height is 1000 / width, as wrapped text.
class WrapItem : public BAbstractLayoutItem {
public:
	explicit WrapItem(float preferredFactor)
		:
		fPreferredFactor(preferredFactor)
	{
	}

	bool HasHeightForWidth() override
	{
		return true;
	}

	void GetHeightForWidth(float width, float* min, float* max,
		float* preferred) override
	{
		const float height = std::ceil(1000 / width);
		if (min)
			*min = height;
		if (max)
			*max = kSizeUnlimited;
		if (preferred)
			*preferred = height * fPreferredFactor;
	}

	bool IsVisible() override
	{
		return fVisible;
	}

	void SetVisible(bool visible) override
	{
		fVisible = visible;
	}

	gfx::Rect Frame() override
	{
		return {0, 0, 0, 0};
	}

	void SetFrame(gfx::Rect) override
	{
	}

private:
	float fPreferredFactor;
	bool fVisible = true;
};


TEST(CardLayoutTest, SizesCoverAllPages)
{
	// What is tested: A card layout with two pages whose height depends on
	// the width (1000 / width, preferred 2 and 3 times that), asked for
	// the heights at width 100; a page that is not in the layout made
	// visible.
	// Success criteria: The minimum height is 10, the preferred height the
	// largest of the pages, 30 (Haiku took the smallest, 20). A foreign
	// item is ignored: the visible page stays.
	// Why it matters: A card layout must be large enough for every page, or
	// the window resizes when the user switches pages.
	//
	// Given
	BView view({0, 0, 100, 100}, "pages");
	auto* cards = static_cast<BCardLayout*>(view.SetLayout(std::make_unique<BCardLayout>()));
	BLayoutItem* firstPage = cards->AddItem(std::make_unique<WrapItem>(2.0f));
	(void)cards->AddItem(std::make_unique<WrapItem>(3.0f));
	WrapItem foreign(1);
	float min = 0;
	float preferred = 0;

	// When
	cards->GetHeightForWidth(100, &min, nullptr, &preferred);
	cards->SetVisibleItem(&foreign);

	// Then
	EXPECT_TRUE(cards->HasHeightForWidth());
	EXPECT_EQ(min, 10);
	EXPECT_EQ(preferred, 30);
	EXPECT_EQ(cards->VisibleItem(), firstPage);
	EXPECT_EQ(cards->VisibleIndex(), 0);
}


// A pop-up menu with the items \a labels, the first marked.
std::unique_ptr<BPopUpMenu>
Choices(std::initializer_list<const char*> labels)
{
	auto menu = std::make_unique<BPopUpMenu>("choices");
	for (const char* label : labels)
		menu->AddItem(std::make_unique<BMenuItem>(label, nullptr));
	menu->ItemAt(0)->SetMarked(true);
	return menu;
}


TEST(LayoutBuilderTest, MenuFieldsLineUpInAGrid)
{
	// What is tested: A grid on a window of 300 × 200 (spacing 5, no
	// insets) with two menu fields added by AddMenuField(), "Size:" in row
	// 0 and "Orientation:" in row 1; laid out. Then a field's label item
	// asked for again, and the first field removed from its layout.
	// Success criteria: Each field is a child of the root view and spans
	// both columns: its frame begins at x 0 and reaches to the right edge
	// (the menu bar column is unlimited). The labels take column 0, as wide
	// as the wider label; both menu bars begin at the same x (the dividers
	// are equal, column 1's left edge). A label item is handed out once
	// while it lives. Removing the field's items takes the field out of the
	// window, and it can make new items.
	// Why it matters: Forms of several menu fields are aligned this way in
	// Haiku's preferences; the field must follow its two items.
	//
	// Given
	BWindow window({0, 0, 300, 200}, "form");
	auto size = std::make_unique<BMenuField>("size", "Size:", Choices({"Small", "Large"}));
	auto orientation = std::make_unique<BMenuField>("orientation", "Orientation:",
		Choices({"Portrait", "Landscape"}));
	BMenuField* first = size.get();
	BMenuField* second = orientation.get();

	// When
	BLayoutBuilder::Grid<>(&window, 5, 5)
		.AddMenuField(std::move(size), 0, 0)
		.AddMenuField(std::move(orientation), 0, 1);
	window.Layout(false);
	const auto barLeft = [](BMenuField* field) {
		return field->Frame().x0 + field->MenuBar()->Frame().x0;
	};
	const std::unique_ptr<BLayoutItem> again = first->CreateLabelLayoutItem();
	BGridLayout* grid = static_cast<BGridLayout*>(window.RootView()->GetLayout());
	BLayoutItem* column1 = grid->ItemAt(1, 1);
	const float column1Left = column1->Frame().x0;
	std::unique_ptr<BView> removed = grid->RemoveView(first);
	const bool newItems = first->CreateLabelLayoutItem() != nullptr
		&& first->CreateMenuBarLayoutItem() != nullptr;

	// Then
	EXPECT_EQ(second->Parent(), window.RootView());
	EXPECT_EQ(grid->CountColumns(), 2);
	EXPECT_EQ(grid->CountRows(), 2);
	EXPECT_EQ(second->Frame().x0, 0);
	EXPECT_EQ(second->Frame().x1, 300);
	EXPECT_EQ(second->Divider(), column1Left);
	EXPECT_EQ(barLeft(second), column1Left + 2);
	EXPECT_EQ(again, nullptr);
	EXPECT_EQ(removed.get(), first);
	EXPECT_EQ(first->Parent(), nullptr);
	EXPECT_TRUE(newItems);
}


TEST(LayoutBuilderTest, MenusAreBuiltInOneExpression)
{
	// What is tested: A menu bar filled by a Menu builder: "File" with
	// New (1, Command+N), Open (2, disabled), a separator and the submenu
	// "Recent" with a.txt (3, marked); "Edit" with Cut (a message 4,
	// Command+X), its item stored with GetItem().
	// Success criteria: The bar has File and Edit; File has its four
	// items in order, Open disabled, the separator third, Recent's a.txt
	// marked; New has its shortcut; GetItem() gave Cut, whose message is 4.
	// End() leads back from a submenu to its menu and from a menu to the
	// bar's builder.
	// Why it matters: Haiku applications build their menus this way.
	//
	// Given
	BMenuBar bar({0, 0, 300, 20}, "bar");
	BMenuItem* cut = nullptr;

	// When
	BLayoutBuilder::Menu<>(&bar)
		.AddMenu("File")
			.AddItem("New", 1, 'N')
			.AddItem("Open", 2).SetEnabled(false)
			.AddSeparator()
			.AddMenu("Recent")
				.AddItem("a.txt", 3).SetMarked(true)
			.End()
		.End()
		.AddMenu("Edit")
			.AddItem("Cut", std::make_unique<BMessage>(4), 'X').GetItem(cut)
		.End();
	BMenu* file = bar.SubmenuAt(0);
	BModifierFlags modifiers;

	// Then
	ASSERT_EQ(bar.CountItems(), 2);
	ASSERT_NE(file, nullptr);
	EXPECT_EQ(file->Name(), "File");
	EXPECT_EQ(bar.SubmenuAt(1)->Name(), "Edit");
	ASSERT_EQ(file->CountItems(), 4);
	EXPECT_EQ(file->ItemAt(0)->Label(), "New");
	EXPECT_EQ(file->ItemAt(0)->Shortcut(&modifiers), static_cast<uint32>('N'));
	EXPECT_FALSE(file->ItemAt(1)->IsEnabled());
	EXPECT_NE(dynamic_cast<BSeparatorItem*>(file->ItemAt(2)), nullptr);
	ASSERT_NE(file->SubmenuAt(3), nullptr);
	EXPECT_TRUE(file->SubmenuAt(3)->ItemAt(0)->IsMarked());
	ASSERT_NE(cut, nullptr);
	EXPECT_EQ(cut->Label(), "Cut");
	EXPECT_EQ(cut->Message()->what, 4u);
}


} // namespace
