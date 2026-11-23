/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Layout.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AbstractLayoutItem.h>
#include <ngui/LayoutContext.h>
#include <ngui/LayoutUtils.h>
#include <ngui/SpaceLayoutItem.h>
#include <ngui/View.h>

#include "support/StackLayout.h"


namespace {


using namespace ngui;
using namespace ngui::test;


constexpr BAlignment kCentered = {BHorizontalAlignment::Center, BVerticalAlignment::Middle};
constexpr BAlignment kLeftTop = {BHorizontalAlignment::Left, BVerticalAlignment::Top};


// The stack layout a view got with SetLayout().
StackLayout*
Stack(BView& view)
{
	return static_cast<StackLayout*>(view.SetLayout(std::make_unique<StackLayout>()));
}


// A layout item with base sizes 10, 100 and 50 that records its frame.
class BoxItem : public BAbstractLayoutItem {
public:
	gfx::Size BaseMinSize() override
	{
		return {10, 10};
	}

	gfx::Size BaseMaxSize() override
	{
		return {100, 100};
	}

	gfx::Size BasePreferredSize() override
	{
		return {50, 50};
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
		return fFrame;
	}

	void SetFrame(gfx::Rect frame) override
	{
		fFrame = frame;
	}

private:
	gfx::Rect fFrame = {0, 0, 0, 0};
};


TEST(LayoutUtilsTest, RectanglesFollowHaikusFormulas)
{
	// What is tested: ComposeSize(), ComposeAlignment(), AlignInFrame(),
	// AlignOnRect() and MoveIntoFrame() on a frame of 100 × 50.
	// Success criteria: Unset parts come from the second argument. A
	// maximum of 40 × 20 is centered at (30, 15), right and bottom at
	// (60, 30), and uses the full width with UseFullWidth; a larger maximum
	// leaves the frame. AlignOnRect() centers a wider rectangle around the
	// frame (-10 to 110). MoveIntoFrame() moves a rectangle back inside,
	// left and top edges first.
	// Why it matters: Every layout places its items with these; they are
	// Haiku's formulas on half-open rectangles, where the width is the pixel
	// count.
	//
	// Given
	const gfx::Rect frame = {0, 0, 100, 50};

	// When / Then
	EXPECT_EQ(BLayoutUtils::ComposeSize({kSizeUnset, 30}, {10, 20}), (gfx::Size{10, 30}));
	EXPECT_EQ(BLayoutUtils::ComposeAlignment(
		{BHorizontalAlignment::Unset, BVerticalAlignment::Bottom}, kLeftTop),
		(BAlignment{BHorizontalAlignment::Left, BVerticalAlignment::Bottom}));
	EXPECT_EQ(BLayoutUtils::AlignInFrame(frame, {40, 20}, kCentered),
		(gfx::Rect{30, 15, 70, 35}));
	EXPECT_EQ(BLayoutUtils::AlignInFrame(frame, {40, 20},
		{BHorizontalAlignment::Right, BVerticalAlignment::Bottom}),
		(gfx::Rect{60, 30, 100, 50}));
	EXPECT_EQ(BLayoutUtils::AlignInFrame(frame, {40, 20},
		{BHorizontalAlignment::UseFullWidth, BVerticalAlignment::Top}),
		(gfx::Rect{0, 0, 100, 20}));
	EXPECT_EQ(BLayoutUtils::AlignInFrame(frame, {400, 200}, kCentered), frame);
	EXPECT_EQ(BLayoutUtils::AlignOnRect(frame, {120, 20}, kCentered),
		(gfx::Rect{-10, 15, 110, 35}));
	EXPECT_EQ(BLayoutUtils::MoveIntoFrame({90, 40, 130, 60}, {100, 50}),
		(gfx::Rect{60, 30, 100, 50}));
	EXPECT_EQ(BLayoutUtils::MoveIntoFrame({10, 10, 150, 20}, {100, 50}),
		(gfx::Rect{0, 10, 140, 20}));
}


TEST(LayoutItemTest, ExplicitSizesComposeWithBaseSizes)
{
	// What is tested: A BAbstractLayoutItem with base sizes 10 (min), 100
	// (max) and 50 (preferred), given an explicit minimum width, an
	// explicit preferred height, an explicit horizontal alignment, aligned
	// in a frame of 200 × 200, then given one explicit size for all.
	// Success criteria: The minimum is 20 × 10, the preferred size 50 × 60
	// (Haiku composed the explicit maximum there instead), the alignment
	// right and middle; the item is placed at (100, 50) with its maximum
	// 100 × 100; SetExplicitSize() sets minimum, maximum and preferred.
	// Why it matters: Applications fix sizes of single views this way
	// (BLayoutItem::SetExplicit…), and layouts read them on every pass.
	//
	// Given
	BoxItem item;

	// When
	item.SetExplicitMinSize({20, kSizeUnset});
	item.SetExplicitPreferredSize({kSizeUnset, 60});
	item.SetExplicitAlignment({BHorizontalAlignment::Right, BVerticalAlignment::Unset});
	item.AlignInFrame({0, 0, 200, 200});
	const gfx::Rect aligned = item.Frame();
	const gfx::Size min = item.MinSize();
	const gfx::Size preferred = item.PreferredSize();
	item.SetExplicitSize({30, 30});

	// Then
	EXPECT_EQ(min, (gfx::Size{20, 10}));
	EXPECT_EQ(preferred, (gfx::Size{50, 60}));
	EXPECT_EQ(item.Alignment(),
		(BAlignment{BHorizontalAlignment::Right, BVerticalAlignment::Middle}));
	EXPECT_EQ(aligned, (gfx::Rect{100, 50, 200, 150}));
	EXPECT_EQ(item.MinSize(), (gfx::Size{30, 30}));
	EXPECT_EQ(item.MaxSize(), (gfx::Size{30, 30}));
	EXPECT_EQ(item.PreferredSize(), (gfx::Size{30, 30}));
}


TEST(LayoutItemTest, SpaceItemsAreGlueAndStruts)
{
	// What is tested: BSpaceLayoutItem::CreateGlue(), CreateHorizontalStrut(10)
	// and an explicit maximum height for the strut.
	// Success criteria: Glue goes from 0 to unlimited; the strut is exactly
	// 10 wide (Haiku: 11 pixels, from its width 10 plus 1) and from 0 to
	// unlimited high; an explicit maximum changes only the set part.
	// Why it matters: Glue and struts push items apart in groups (G10c);
	// their sizes must be pixel counts like everything else.
	//
	// Given
	const auto glue = BSpaceLayoutItem::CreateGlue();
	const auto strut = BSpaceLayoutItem::CreateHorizontalStrut(10);

	// When
	strut->SetExplicitMaxSize({kSizeUnset, 20});

	// Then
	EXPECT_EQ(glue->MinSize(), (gfx::Size{0, 0}));
	EXPECT_EQ(glue->MaxSize(), (gfx::Size{kSizeUnlimited, kSizeUnlimited}));
	EXPECT_EQ(strut->MinSize(), (gfx::Size{10, 0}));
	EXPECT_EQ(strut->PreferredSize(), (gfx::Size{10, 0}));
	EXPECT_EQ(strut->MaxSize(), (gfx::Size{10, 20}));
	EXPECT_EQ(strut->Alignment(), kCentered);
}


TEST(LayoutTest, AddViewMakesTheViewAChildOfTheTarget)
{
	// What is tested: A view with a layout: AddView() of a new view, of a
	// view that is no child, AddChild() on the view, AddItem() of glue at
	// the end and of another at index 0; then SetLayout() on a view that
	// has two children already.
	// Success criteria: The new view becomes a child of the layout's owner
	// with an item at index 0; a view that is not a child is refused;
	// AddChild() adds the child to the layout; the hooks see the indexes 0,
	// 1, 2 and 0; a new layout gets an item for every existing child.
	// Why it matters: Views in a layout are children of its target view, as
	// in Haiku; ownership stays explicit: the layout owns the items, the
	// view its children.
	//
	// Given
	BView owner({0, 0, 100, 50}, "owner");
	StackLayout* stack = Stack(owner);
	BView outsider("outsider");
	BView parent({0, 0, 100, 50}, "parent");
	(void)parent.AddChild(std::make_unique<BView>("c"));
	(void)parent.AddChild(std::make_unique<BView>("d"));

	// When
	BView* a = stack->AddView(std::make_unique<BView>("a"))->View();
	BLayoutItem* refused = stack->AddView(&outsider);
	BView* b = owner.AddChild(std::make_unique<BView>("b"));
	(void)stack->AddItem(BSpaceLayoutItem::CreateGlue());
	(void)stack->AddItem(0, BSpaceLayoutItem::CreateGlue());
	BLayout* replaced = parent.SetLayout(std::make_unique<StackLayout>());

	// Then
	EXPECT_EQ(a->Parent(), &owner);
	EXPECT_EQ(refused, nullptr);
	EXPECT_EQ(outsider.Parent(), nullptr);
	EXPECT_EQ(stack->IndexOfView(a), 1);
	EXPECT_EQ(stack->IndexOfView(b), 2);
	EXPECT_EQ(stack->CountItems(), 4);
	EXPECT_EQ(stack->events, (std::vector<std::string>{"added 0", "added 1", "added 2",
		"added 0"}));
	EXPECT_EQ(stack->Owner(), &owner);
	EXPECT_EQ(stack->TargetView(), &owner);
	EXPECT_EQ(owner.GetLayout(), stack);
	EXPECT_TRUE(owner.Flags().Has(BViewFlag::SupportsLayout));
	EXPECT_EQ(replaced->CountItems(), 2);
}


TEST(LayoutTest, RemovingTheLastItemTakesTheViewAlong)
{
	// What is tested: Removing the only item of a view from the layout,
	// adding that item back, RemoveView(), and RemoveChild() of a view that
	// is in the layout.
	// Success criteria: The removed item holds the view, which is no
	// longer a child; added back, the view is a child again. RemoveView()
	// gives the view back and leaves no item. RemoveChild() also removes
	// the child's item.
	// Why it matters: Haiku removes the view from its parent and leaves it
	// to whoever holds a pointer; NewGUI's ownership must keep it alive and
	// reachable without leaks.
	//
	// Given
	BView owner({0, 0, 100, 50}, "owner");
	StackLayout* stack = Stack(owner);
	BView* a = stack->AddView(std::make_unique<BView>("a"))->View();

	// When
	std::unique_ptr<BLayoutItem> item = stack->RemoveItem(stack->ItemAt(0));
	const bool removedFromOwner = a->Parent() == nullptr;
	BView* inItem = item->View();
	(void)stack->AddItem(std::move(item));
	const bool backInOwner = a->Parent() == &owner;
	std::unique_ptr<BView> view = stack->RemoveView(a);
	const int32 itemsAfterRemoveView = stack->CountItems();
	BView* b = owner.AddChild(std::make_unique<BView>("b"));
	std::unique_ptr<BView> removedB = owner.RemoveChild(b);

	// Then
	EXPECT_TRUE(removedFromOwner);
	EXPECT_EQ(inItem, a);
	EXPECT_TRUE(backInOwner);
	EXPECT_EQ(view.get(), a);
	EXPECT_EQ(view->Parent(), nullptr);
	EXPECT_EQ(itemsAfterRemoveView, 0);
	EXPECT_EQ(removedB.get(), b);
	EXPECT_EQ(stack->CountItems(), 0);
	EXPECT_EQ(owner.CountChildren(), 0);
	EXPECT_EQ(stack->events, (std::vector<std::string>{"added 0", "removed 0", "added 0",
		"removed 0", "added 0", "removed 0"}));
}


TEST(LayoutTest, LayoutPlacesTheItemsOnlyWhenNeeded)
{
	// What is tested: A view of 100 × 50 with a child of at most 40 × 20 and
	// glue, laid out twice, then after a new explicit alignment of the
	// child, then forced.
	// Success criteria: The first Layout() runs DoLayout() once: the child
	// is centered at (30, 15), the glue fills the view. A second Layout()
	// does nothing; the new alignment invalidates the layout and the next
	// Layout() moves the child to (0, 0). Layout(true) lays out the views
	// again, but leaves a valid layout as it is: Haiku's BView::DoLayout()
	// passes force = false to the layout.
	// Why it matters: Layout runs before frames; it must happen when sizes
	// changed and not otherwise (Haiku's validity states).
	//
	// Given
	BView owner({0, 0, 100, 50}, "owner");
	StackLayout* stack = Stack(owner);
	BView* child = stack->AddView(std::make_unique<BView>("child"))->View();
	BLayoutItem* glue = stack->AddItem(BSpaceLayoutItem::CreateGlue());
	child->SetExplicitMaxSize({40, 20});

	// When
	owner.Layout(false);
	const gfx::Rect centered = child->Frame();
	const int afterFirst = stack->layouts;
	owner.Layout(false);
	const int afterSecond = stack->layouts;
	child->SetExplicitAlignment(kLeftTop);
	const bool invalidated = !owner.IsLayoutValid();
	owner.Layout(false);
	const gfx::Rect leftTop = child->Frame();
	owner.Layout(true);

	// Then
	EXPECT_EQ(centered, (gfx::Rect{30, 15, 70, 35}));
	EXPECT_EQ(glue->Frame(), (gfx::Rect{0, 0, 100, 50}));
	EXPECT_EQ(afterFirst, 1);
	EXPECT_EQ(afterSecond, 1);
	EXPECT_TRUE(invalidated);
	EXPECT_EQ(leftTop, (gfx::Rect{0, 0, 40, 20}));
	EXPECT_EQ(stack->layouts, 2);
	EXPECT_TRUE(owner.IsLayoutValid());
}


TEST(LayoutTest, ViewSizesComeFromLayoutAndExplicitSizes)
{
	// What is tested: The sizes of a view whose layout holds a child of
	// minimum 30 × 10, preferred 60 × 20 and one of minimum 20 × 40,
	// preferred 20 × 50; with an explicit minimum height of 100; with the
	// second child hidden; an explicit maximum set on the layout; and a
	// plain view of 70 × 30 without layout.
	// Success criteria: The minimum is 30 × 40 and the preferred size 60 ×
	// 50; the explicit height makes the minimum 30 × 100; hidden, the
	// second child does not count (preferred 60 × 20). The layout's
	// explicit maximum is the view's. The plain view's minimum and
	// preferred size are its size, its maximum unlimited, its alignment
	// centered.
	// Why it matters: Windows and parent layouts ask views for these sizes;
	// they combine the layout with the application's explicit values as in
	// Haiku.
	//
	// Given
	BView owner({0, 0, 100, 50}, "owner");
	StackLayout* stack = Stack(owner);
	BView* a = stack->AddView(std::make_unique<BView>("a"))->View();
	BView* b = stack->AddView(std::make_unique<BView>("b"))->View();
	a->SetExplicitMinSize({30, 10});
	a->SetExplicitPreferredSize({60, 20});
	b->SetExplicitMinSize({20, 40});
	b->SetExplicitPreferredSize({20, 50});
	BView plain({0, 0, 70, 30}, "plain");

	// When
	const gfx::Size min = owner.MinSize();
	const gfx::Size preferred = owner.PreferredSize();
	owner.SetExplicitMinSize({kSizeUnset, 100});
	const gfx::Size explicitMin = owner.MinSize();
	b->Hide();
	stack->SetExplicitMaxSize({200, kSizeUnset});

	// Then
	EXPECT_EQ(min, (gfx::Size{30, 40}));
	EXPECT_EQ(preferred, (gfx::Size{60, 50}));
	EXPECT_EQ(explicitMin, (gfx::Size{30, 100}));
	EXPECT_EQ(owner.PreferredSize(), (gfx::Size{60, 20}));
	EXPECT_EQ(owner.MaxSize(), (gfx::Size{200, kSizeUnlimited}));
	EXPECT_EQ(owner.ExplicitMaxSize(), (gfx::Size{200, kSizeUnset}));
	EXPECT_EQ(owner.LayoutAlignment(),
		(BAlignment{BHorizontalAlignment::UseFullWidth, BVerticalAlignment::UseFullHeight}));
	EXPECT_EQ(plain.MinSize(), (gfx::Size{70, 30}));
	EXPECT_EQ(plain.PreferredSize(), (gfx::Size{70, 30}));
	EXPECT_EQ(plain.MaxSize(), (gfx::Size{kSizeUnlimited, kSizeUnlimited}));
	EXPECT_EQ(plain.LayoutAlignment(), kCentered);
}


TEST(LayoutTest, InvalidationReachesTheEnclosingLayouts)
{
	// What is tested: A view "outer" whose layout holds "inner", whose own
	// layout holds "leaf"; all laid out; then a new explicit size of leaf;
	// with invalidation disabled on outer; and leaf hidden.
	// Success criteria: After Layout() all are valid. The new size makes
	// inner's and outer's layouts invalid and calls their hooks; the next
	// Layout() lays out inner again. With invalidation disabled outer stays
	// valid while inner does not. Hiding leaf invalidates inner's layout.
	// Why it matters: A changed label or font deep in a window must reach
	// the window, which then lays out before its next frame.
	//
	// Given
	BView outer({0, 0, 200, 100}, "outer");
	StackLayout* outerStack = Stack(outer);
	BView* inner = outerStack->AddView(std::make_unique<BView>("inner"))->View();
	StackLayout* innerStack = Stack(*inner);
	BView* leaf = innerStack->AddView(std::make_unique<BView>("leaf"))->View();
	outer.Layout(false);
	const bool allValid = outer.IsLayoutValid() && inner->IsLayoutValid()
		&& leaf->IsLayoutValid();
	const int innerInvalidations = innerStack->invalidations;
	const int outerInvalidations = outerStack->invalidations;
	const int innerLayouts = innerStack->layouts;

	// When
	leaf->SetExplicitMinSize({10, 10});
	const bool innerInvalid = !inner->IsLayoutValid();
	const bool outerInvalid = !outer.IsLayoutValid();
	outer.Layout(false);
	const int innerLayoutsAfter = innerStack->layouts;
	outer.DisableLayoutInvalidation();
	leaf->SetExplicitMinSize({20, 20});
	const bool outerStillValid = outer.IsLayoutValid();
	const bool innerInvalidAgain = !inner->IsLayoutValid();
	outer.EnableLayoutInvalidation();
	outer.Layout(true);
	leaf->Hide();

	// Then
	EXPECT_TRUE(allValid);
	EXPECT_EQ(inner->Frame(), (gfx::Rect{0, 0, 200, 100}));
	EXPECT_EQ(leaf->Frame(), (gfx::Rect{0, 0, 200, 100}));
	EXPECT_TRUE(innerInvalid);
	EXPECT_TRUE(outerInvalid);
	EXPECT_GT(innerStack->invalidations, innerInvalidations);
	EXPECT_GT(outerStack->invalidations, outerInvalidations);
	EXPECT_EQ(innerLayoutsAfter, innerLayouts + 1);
	EXPECT_TRUE(outerStillValid);
	EXPECT_TRUE(innerInvalidAgain);
	EXPECT_FALSE(inner->IsLayoutValid());
	EXPECT_FALSE(outer.IsLayoutValid());
}


TEST(LayoutTest, NestedLayoutsShareTheTarget)
{
	// What is tested: A layout without owner nested in a view's layout,
	// given a view, an explicit maximum of 50 × 20 and a left-top
	// alignment; laid out; then removed again.
	// Success criteria: The nested layout's target is the owner of the
	// outer one, so its view becomes the owner's child. Laid out, the
	// nested layout gets the frame (0, 0, 50, 20) and places its view in
	// it, once. Removed, it has no target and no items; its view stays a
	// child of the owner, as in Haiku.
	// Why it matters: Builders nest groups in groups (G10d); the views of
	// all of them are children of one view.
	//
	// Given
	BView owner({0, 0, 100, 50}, "owner");
	StackLayout* stack = Stack(owner);
	auto* nested = static_cast<StackLayout*>(stack->AddItem(std::make_unique<StackLayout>()));

	// When
	BView* view = nested->AddView(std::make_unique<BView>("view"))->View();
	nested->SetExplicitMaxSize({50, 20});
	nested->SetExplicitAlignment(kLeftTop);
	owner.Layout(false);
	const gfx::Rect nestedFrame = nested->Frame();
	const int nestedLayouts = nested->layouts;
	std::unique_ptr<BLayoutItem> removed = stack->RemoveItem(nested);

	// Then
	EXPECT_EQ(view->Parent(), &owner);
	EXPECT_EQ(nested->Owner(), nullptr);
	EXPECT_EQ(nestedFrame, (gfx::Rect{0, 0, 50, 20}));
	EXPECT_EQ(view->Frame(), (gfx::Rect{0, 0, 50, 20}));
	EXPECT_EQ(nestedLayouts, 1);
	EXPECT_EQ(removed.get(), nested);
	EXPECT_EQ(nested->TargetView(), nullptr);
	EXPECT_EQ(nested->CountItems(), 0);
	EXPECT_EQ(view->Parent(), &owner);
}


// Records the addresses of the contexts it is told about (addresses only:
// the contexts are gone when they are compared).
class ContextListener : public BLayoutContextListener {
public:
	void LayoutContextLeft(BLayoutContext* context) override
	{
		left.push_back(reinterpret_cast<std::uintptr_t>(context));
	}

	std::vector<std::uintptr_t> left;
};


TEST(LayoutTest, ContextsTellTheirListeners)
{
	// What is tested: Two listeners added to a layout context, one removed
	// again, and the context destroyed; LayoutContext() of a view during and
	// outside its layout.
	// Success criteria: Only the remaining listener learns that the
	// context is left, once; a view has no context outside a layout pass.
	// Why it matters: Grid layouts keep computed data for one pass and drop
	// it when the context ends (BTwoDimensionalLayout, G10c).
	//
	// Given
	ContextListener kept;
	ContextListener removed;
	std::uintptr_t address = 0;
	BView view({0, 0, 10, 10}, "view");

	// When
	{
		BLayoutContext context;
		address = reinterpret_cast<std::uintptr_t>(&context);
		context.AddListener(&kept);
		context.AddListener(&removed);
		context.AddListener(nullptr);
		context.RemoveListener(&removed);
	}

	// Then
	EXPECT_EQ(kept.left, (std::vector<std::uintptr_t>{address}));
	EXPECT_TRUE(removed.left.empty());
	EXPECT_EQ(view.LayoutContext(), nullptr);
}


} // namespace
