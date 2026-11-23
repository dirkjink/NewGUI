/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/View.h>

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Dump.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using ngui::test::FixedTextSystem;


// A view that counts its Draw() calls and fills a red square, and records
// its frame events.
class Probe : public BView {
public:
	Probe(gfx::Rect frame, std::string_view name, BResizingMode mode = kFollowLeftTop,
		BViewFlags flags = BViewFlag::WillDraw)
		:
		BView(frame, name, mode, flags)
	{
	}

	~Probe() override
	{
		if (destroyed != nullptr)
			*destroyed = true;
	}

	void Draw(DrawContext& context) override
	{
		draws++;
		context.canvas.FillRect({1, 1, 3, 3}, gfx::Paint{.color = {1, 0, 0, 1}});
	}

	void DrawAfterChildren(DrawContext& context) override
	{
		context.canvas.StrokeRect(Bounds(), gfx::Paint{.color = {0, 0, 1, 1}});
	}

	void FrameMoved(gfx::Point where) override
	{
		events.push_back("moved " + std::to_string(static_cast<int>(where.x)) + ","
			+ std::to_string(static_cast<int>(where.y)));
	}

	void FrameResized(float width, float height) override
	{
		events.push_back("resized " + std::to_string(static_cast<int>(width)) + "x"
			+ std::to_string(static_cast<int>(height)));
	}

	int draws = 0;
	bool* destroyed = nullptr;
	std::vector<std::string> events;
};


struct Environment {
	const gfx::Caps caps;
	const FixedTextSystem text;
	const DrawEnvironment environment{caps, Palette::Default(), text, 1};
};


TEST(ViewTest, ChildrenAreOwnedByTheirParent)
{
	// What is tested: AddChild() at the end and before a sibling,
	// RemoveChild(), RemoveSelf(), the sibling and index queries, FindView(),
	// and the destruction of a parent with children.
	// Success criteria: The order of the children follows the insertions;
	// removing gives the child back as std::unique_ptr without a parent;
	// FindView() searches depth first; destroying the parent destroys its
	// children; adding before a view that is no child fails.
	// Why it matters: The view tree is the backbone of a window (G8); its
	// ownership is explicit (CONVENTIONS.md 3.3) where Haiku deletes
	// children in the parent's destructor.
	//
	// Given
	auto root = std::make_unique<BView>(gfx::Rect{0, 0, 100, 100}, "root");
	BView* a = root->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 10, 10}, "a"));
	BView* c = root->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 10, 10}, "c"));
	BView* b = root->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 10, 10}, "b"), c);
	auto* deep = static_cast<Probe*>(b->AddChild(std::make_unique<Probe>(gfx::Rect{1, 1, 2, 2},
		"deep")));
	bool deepDestroyed = false;
	deep->destroyed = &deepDestroyed;
	BView outsider({0, 0, 1, 1}, "outsider");

	// When / Then
	EXPECT_EQ(root->CountChildren(), 3);
	EXPECT_EQ(root->ChildAt(0), a);
	EXPECT_EQ(root->ChildAt(1), b);
	EXPECT_EQ(root->ChildAt(2), c);
	EXPECT_EQ(root->ChildAt(3), nullptr);
	EXPECT_EQ(a->NextSibling(), b);
	EXPECT_EQ(c->PreviousSibling(), b);
	EXPECT_EQ(a->PreviousSibling(), nullptr);
	EXPECT_EQ(root->FindView("deep"), deep);
	EXPECT_EQ(root->FindView("root"), root.get());
	EXPECT_EQ(root->FindView("none"), nullptr);
	EXPECT_EQ(deep->Parent(), b);
	EXPECT_EQ(root->AddChild(std::make_unique<BView>("x"), &outsider), nullptr);
	EXPECT_EQ(root->AddChild(nullptr), nullptr);

	std::unique_ptr<BView> removed = root->RemoveChild(a);
	EXPECT_EQ(removed.get(), a);
	EXPECT_EQ(a->Parent(), nullptr);
	EXPECT_EQ(root->RemoveChild(a), nullptr);
	std::unique_ptr<BView> self = c->RemoveSelf();
	EXPECT_EQ(self.get(), c);
	EXPECT_EQ(root->CountChildren(), 1);
	EXPECT_EQ(outsider.RemoveSelf(), nullptr);

	root.reset();
	EXPECT_TRUE(deepDestroyed);
}


TEST(ViewTest, GeometryIsHalfOpen)
{
	// What is tested: Frame() and Bounds() of a view of 100 × 50 at (10, 20),
	// moved, resized and scrolled; the conversions between its coordinates
	// and its parent's; the frame hooks with and without FrameEvents.
	// Success criteria: Bounds() has the frame's size at the scroll
	// position; a point and a rectangle convert there and back; the hooks
	// report the new position and size only with FrameEvents.
	// Why it matters: Every event position and every drawing in a window
	// goes through these coordinates; rectangles are half-open
	// (specification 4.1), unlike Haiku's.
	//
	// Given
	Probe view({10, 20, 110, 70}, "view", kFollowLeftTop,
		BViewFlag::WillDraw | BViewFlag::FrameEvents);
	Probe quiet({0, 0, 10, 10}, "quiet");

	// When / Then
	EXPECT_EQ(view.Bounds(), (gfx::Rect{0, 0, 100, 50}));
	view.MoveBy(5, 5);
	view.MoveTo({0, 0});
	view.ResizeBy(10, 0);
	view.ResizeTo(40, 30);
	EXPECT_EQ(view.events, (std::vector<std::string>{"moved 15,25", "moved 0,0",
		"resized 110x50", "resized 40x30"}));
	view.MoveTo({10, 20});
	view.ScrollTo({5, 7});
	EXPECT_EQ(view.Frame(), (gfx::Rect{10, 20, 50, 50}));
	EXPECT_EQ(view.Bounds(), (gfx::Rect{5, 7, 45, 37}));
	EXPECT_EQ(view.ConvertToParent(gfx::Point{5, 7}), (gfx::Point{10, 20}));
	EXPECT_EQ(view.ConvertFromParent(gfx::Point{10, 20}), (gfx::Point{5, 7}));
	EXPECT_EQ(view.ConvertToParent(gfx::Rect{5, 7, 15, 17}), (gfx::Rect{10, 20, 20, 30}));
	EXPECT_EQ(view.ConvertFromParent(gfx::Rect{10, 20, 20, 30}), (gfx::Rect{5, 7, 15, 17}));

	quiet.MoveBy(1, 1);
	quiet.ResizeBy(1, 1);
	EXPECT_TRUE(quiet.events.empty());
}


TEST(ViewTest, ChildrenFollowTheirResizingModes)
{
	// What is tested: A parent of 100 × 100 with children in each resizing
	// mode, the parent grown by 10 × 21.
	// Success criteria: Left/Top keeps the frame; Right/Bottom moves by the
	// change; LeftRight/TopBottom grows by it; Center moves by half of it,
	// truncated to whole pixels as Haiku does (21 → 10).
	// Why it matters: Views without a layout keep their place this way when
	// a window is resized (Haiku's B_FOLLOW_* modes).
	//
	// Given
	BView parent({0, 0, 100, 100}, "parent");
	const gfx::Rect frame{10, 10, 30, 30};
	BView* leftTop = parent.AddChild(std::make_unique<BView>(frame, "lt", kFollowLeftTop));
	BView* rightBottom = parent.AddChild(std::make_unique<BView>(frame, "rb",
		BResizingMode{BHorizontalFollow::Right, BVerticalFollow::Bottom}));
	BView* all = parent.AddChild(std::make_unique<BView>(frame, "all", kFollowAll));
	BView* center = parent.AddChild(std::make_unique<BView>(frame, "center",
		BResizingMode{BHorizontalFollow::Center, BVerticalFollow::Center}));

	// When
	parent.ResizeBy(10, 21);

	// Then
	EXPECT_EQ(leftTop->Frame(), (gfx::Rect{10, 10, 30, 30}));
	EXPECT_EQ(rightBottom->Frame(), (gfx::Rect{20, 31, 40, 51}));
	EXPECT_EQ(all->Frame(), (gfx::Rect{10, 10, 40, 51}));
	EXPECT_EQ(center->Frame(), (gfx::Rect{15, 20, 35, 40}));
	EXPECT_EQ(parent.Frame(), (gfx::Rect{0, 0, 110, 121}));
}


TEST(ViewTest, HidingNestsAndHidesTheSubtree)
{
	// What is tested: Hide() twice and Show() once on a parent, and the
	// recording of a parent with a hidden child.
	// Success criteria: The parent stays hidden until shown as often as
	// hidden; its child counts as hidden through it; a hidden child is left
	// out of the parent's recording.
	// Why it matters: Haiku's nested Hide()/Show() is relied on by views
	// that hide parts of themselves temporarily.
	//
	// Given
	const Environment environment;
	BView parent({0, 0, 20, 20}, "parent");
	BView* child = parent.AddChild(std::make_unique<Probe>(gfx::Rect{0, 0, 5, 5}, "child"));

	// When / Then
	parent.Hide();
	parent.Hide();
	parent.Show();
	EXPECT_TRUE(parent.IsHidden());
	EXPECT_TRUE(child->IsHidden());
	parent.Show();
	EXPECT_FALSE(child->IsHidden());
	child->Hide();
	EXPECT_EQ(parent.RecordTree(environment.environment)->OpCount(), 1u);
	child->Show();
	EXPECT_EQ(parent.RecordTree(environment.environment)->OpCount(), 2u);
}


TEST(ViewTest, RecordingFollowsSection11_4)
{
	// What is tested: RecordTree() of a parent with a view color and a
	// transparent, scrolled child that draws, twice, then after the child
	// was invalidated; and a view that draws after its children.
	// Success criteria: The tree's list holds the parent's own list, then
	// the child's, clipped to its frame and placed at frame minus scroll;
	// the child draws only on the first recording and after Invalidate();
	// a transparent view has no background; DrawAfterChildren() comes last.
	// Why it matters: Specification 11.4: each view keeps its list, an
	// invalidation drops only its own, and the window's list is put
	// together from references for every frame.
	//
	// Given
	const Environment environment;
	Probe parent({0, 0, 20, 20}, "parent", kFollowLeftTop,
		BViewFlag::WillDraw | BViewFlag::DrawOnChildren);
	parent.SetViewColor(gfx::Color{0.5f, 0.5f, 0.5f, 1});
	auto* child = static_cast<Probe*>(parent.AddChild(std::make_unique<Probe>(
		gfx::Rect{5, 5, 15, 15}, "child")));
	child->SetViewColor(std::nullopt);
	child->ScrollBy(1, 0);

	// When
	const gfx::DisplayListRef first = parent.RecordTree(environment.environment);
	(void)parent.RecordTree(environment.environment);
	const int drawsBefore = child->draws;
	child->Invalidate();
	EXPECT_FALSE(child->IsValid());
	(void)parent.RecordTree(environment.environment);

	// Then
	EXPECT_EQ(drawsBefore, 1);
	EXPECT_EQ(child->draws, 2);
	EXPECT_EQ(parent.draws, 1);
	EXPECT_EQ(gfx::DumpText(*first),
		"DisplayList bounds=[0 0 20 20] ops=3\n"
		"clip [0 0 20 20]\n"
		"DrawList offset=(0 0)\n"
		"  DisplayList bounds=[0 0 20 20] ops=2\n"
		"  clip [0 0 20 20]\n"
		"  FillRect rect=[0 0 20 20] color=(0.5 0.5 0.5 1) width=1 cap=Butt join=Miter aa=1\n"
		"  FillRect rect=[1 1 3 3] color=(1 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"clip [5 5 15 15]\n"
		"DrawList offset=(4 5)\n"
		"  DisplayList bounds=[1 0 11 10] ops=1\n"
		"  clip [1 0 11 10]\n"
		"  FillRect rect=[1 1 3 3] color=(1 0 0 1) width=1 cap=Butt join=Miter aa=1\n"
		"clip [0 0 20 20]\n"
		"DrawList offset=(0 0)\n"
		"  DisplayList bounds=[0 0 20 20] ops=1\n"
		"  clip [0 0 20 20]\n"
		"  StrokeRect rect=[0 0 20 20] color=(0 0 1 1) width=1 cap=Butt join=Miter aa=1\n");
}


} // namespace
