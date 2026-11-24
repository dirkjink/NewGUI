/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Box.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/CheckBox.h>
#include <ngui/Clock.h>
#include <ngui/GroupLayout.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/ScrollBar.h>
#include <ngui/ScrollView.h>
#include <ngui/SplitView.h>
#include <ngui/TabView.h>
#include <ngui/TextSystem.h>
#include <ngui/View.h>
#include <ngui/Window.h>


namespace {


using namespace ngui;
using namespace std::chrono_literals;


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


// What the scroll tests ask a running window: the value of the scroll bar
// "bar" and the top left corner of the bounds of the view "target".
constexpr std::uint32_t kAsk = FourCharCode("ASK ");


class ScrollWindow : public BWindow {
public:
	ScrollWindow()
		:
		BWindow({0, 0, 300, 200}, "scrolling")
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		auto* bar = static_cast<BScrollBar*>(FindView("bar"));
		(void)reply.AddFloat("value", bar->Value());
		(void)reply.AddPoint("scroll", {FindView("target")->Bounds().x0,
			FindView("target")->Bounds().y0});
		(void)message->SendReply(reply);
	}
};


BMessage
Event(std::uint32_t what, gfx::Point where, std::int32_t buttons = 0,
	BModifierFlags modifiers = {})
{
	BMessage message(what);
	(void)message.AddPoint("where", where);
	(void)message.AddInt32("buttons", buttons);
	(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
	return message;
}


BMessage
Wheel(float deltaY, BModifierFlags modifiers = {})
{
	BMessage message(kMouseWheelChanged);
	(void)message.AddFloat("be:wheel_delta_x", 0);
	(void)message.AddFloat("be:wheel_delta_y", deltaY);
	(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
	return message;
}


float
AskValue(const BMessenger& window)
{
	BMessage reply;
	EXPECT_TRUE(window.SendMessage(BMessage(kAsk), &reply).has_value());
	return reply.GetFloat("value", -1);
}


TEST(ScrollBarTest, SizesFollowHaikusFormula)
{
	// What is tested: The sizes of a vertical and a horizontal scroll bar
	// with Haiku's default settings and the placeholder font (size 12).
	// Success criteria: Across, the look's scroll bar width 14 plus 1 = 15
	// pixels; along, two arrows of 14 and two minimal thumbs of 15 plus 1 =
	// 59 at least, unlimited at most.
	// Why it matters: Scroll views and layouts place scroll bars by these
	// sizes.
	//
	// Given
	BScrollBar vertical("vertical", nullptr, 0, 100, BOrientation::Vertical);
	BScrollBar horizontal("horizontal", nullptr, 0, 100, BOrientation::Horizontal);

	// When / Then
	EXPECT_EQ(GetScrollBarInfo(), BScrollBarInfo{});
	EXPECT_EQ(vertical.MinSize(), (gfx::Size{15, 59}));
	EXPECT_EQ(vertical.PreferredSize(), (gfx::Size{15, 59}));
	EXPECT_EQ(vertical.MaxSize(), (gfx::Size{15, kSizeUnlimited}));
	EXPECT_EQ(horizontal.MinSize(), (gfx::Size{59, 15}));
	EXPECT_EQ(horizontal.MaxSize(), (gfx::Size{kSizeUnlimited, 15}));
	EXPECT_FALSE(SetScrollBarInfo({.minKnobSize = 8}).has_value());
}


TEST(ScrollBarTest, ValueAndTargetFollowEachOther)
{
	// What is tested: A vertical scroll bar from 0 to 1000 with a target
	// view: SetValue() beyond the range and fractional; the target scrolled
	// itself beyond the range; SetRange() below the value; SetSteps().
	// Success criteria: The value is kept within the range and rounded:
	// 2000 is 1000, -5 is 0, 10.4 is 10; the target's bounds follow every
	// value. ScrollTo(0, 2000) on the target stops at 1000 and moves the
	// bar too. A range 0 .. 300 pulls value and target back to 300. Steps
	// are rounded. Without a target, the view's own scroll bar is nullptr.
	// Why it matters: The bar and its target are one piece of state; Haiku
	// keeps them in sync from both sides.
	//
	// Given
	BView target({0, 0, 100, 100}, "target", kFollowLeftTop, BViewFlag::WillDraw);
	BScrollBar bar({0, 0, 15, 100}, "bar", &target, 0, 1000, BOrientation::Vertical);

	// When / Then
	EXPECT_EQ(target.ScrollBar(BOrientation::Vertical), &bar);
	EXPECT_EQ(target.ScrollBar(BOrientation::Horizontal), nullptr);
	bar.SetValue(2000);
	EXPECT_FLOAT_EQ(bar.Value(), 1000);
	EXPECT_FLOAT_EQ(target.Bounds().y0, 1000);
	bar.SetValue(-5);
	EXPECT_FLOAT_EQ(bar.Value(), 0);
	bar.SetValue(10.4f);
	EXPECT_FLOAT_EQ(bar.Value(), 10);
	EXPECT_FLOAT_EQ(target.Bounds().y0, 10);
	target.ScrollTo({0, 2000});
	EXPECT_FLOAT_EQ(target.Bounds().y0, 1000);
	EXPECT_FLOAT_EQ(bar.Value(), 1000);
	bar.SetRange(0, 300);
	EXPECT_FLOAT_EQ(bar.Value(), 300);
	EXPECT_FLOAT_EQ(target.Bounds().y0, 300);
	bar.SetSteps(2.4f, 20.6f);
	float small = 0;
	float large = 0;
	bar.GetSteps(&small, &large);
	EXPECT_FLOAT_EQ(small, 2);
	EXPECT_FLOAT_EQ(large, 21);
	bar.SetTarget(static_cast<BView*>(nullptr));
	EXPECT_EQ(target.ScrollBar(BOrientation::Vertical), nullptr);
}


TEST(ScrollBarTest, ClicksAndDragsChangeTheValue)
{
	// What is tested: A vertical scroll bar 15 × 100 from 0 to 1000 in a
	// running window: a click on the down arrow, on it with Shift, below
	// the thumb, three on the up arrow with Shift, a drag of the thumb by
	// 14 pixels, a click with the secondary button.
	// Success criteria: Haiku's geometry: inside the border the buttons are
	// 13 high; the thumb is 15 high (the minimal knob) at y 15 for 0. An
	// arrow adds the small step 1, with Shift the large step 10, the empty
	// area the large step; the up arrow goes back down to 0, not below.
	// Dragging the thumb from 20 to 34 puts its top at
	// 29: (29 - 12) / 56 · 1001 = 304. The secondary button at y 26 centers
	// the thumb there: (26 - 7 - 12) / 56 · 1001 = 125.
	// Why it matters: This is how users scroll; the numbers are Haiku's.
	//
	// Given
	auto owned = std::make_unique<ScrollWindow>();
	BView* target = owned->AddChild(std::make_unique<BView>(gfx::Rect{20, 0, 120, 100},
		"target", kFollowLeftTop, BViewFlag::WillDraw));
	owned->AddChild(std::make_unique<BScrollBar>(gfx::Rect{0, 0, 15, 100}, "bar", target,
		0.0f, 1000.0f, BOrientation::Vertical));
	const BMessenger window = *BLooper::Run(std::move(owned));
	const auto send = [&](const BMessage& message) {
		EXPECT_TRUE(window.SendMessage(message).has_value());
	};

	// When / Then
	send(Event(kMouseDown, {7, 92}, 1));
	send(Event(kMouseUp, {7, 92}));
	EXPECT_FLOAT_EQ(AskValue(window), 1);
	send(Event(kMouseDown, {7, 92}, 1, BModifier::Shift));
	send(Event(kMouseUp, {7, 92}));
	EXPECT_FLOAT_EQ(AskValue(window), 11);
	send(Event(kMouseDown, {7, 60}, 1));
	send(Event(kMouseUp, {7, 60}));
	EXPECT_FLOAT_EQ(AskValue(window), 21);
	for (int i = 0; i < 3; i++) {
		send(Event(kMouseDown, {7, 5}, 1, BModifier::Shift));
		send(Event(kMouseUp, {7, 5}));
	}
	EXPECT_FLOAT_EQ(AskValue(window), 0);
	send(Event(kMouseDown, {7, 20}, 1));
	send(Event(kMouseMoved, {7, 34}, 1));
	send(Event(kMouseUp, {7, 34}));
	EXPECT_FLOAT_EQ(AskValue(window), 304);
	send(Event(kMouseDown, {7, 26}, 2));
	send(Event(kMouseUp, {7, 26}));
	EXPECT_FLOAT_EQ(AskValue(window), 125);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
}


TEST(ScrollBarTest, DoubleArrowsSitAtBothEnds)
{
	// What is tested: With double arrows in the scroll bar settings, a
	// horizontal scroll bar 200 × 15 from 0 to 100 in a running window:
	// clicks on the second arrow from the left, the second from the right,
	// the last one and the first one. A second bar of 90 × 15, too short
	// for double arrows, clicked where the second arrow from the right
	// would be.
	// Success criteria: Inside the border the buttons are 13 wide: at the
	// left (1 .. 14) left and (14 .. 27) right, at the right (172 .. 185)
	// left and (185 .. 198) right. The clicks give 1, 0, 1, 0. The short
	// bar (90 is not more than 4 · 16 + 2 · 15 = 94) has single arrows:
	// the click at 66 (the left arrow at 62 .. 75 with double arrows) hits
	// the empty area right of the thumb and pages by the large step 10.
	// Why it matters: Haiku users can turn on double arrows; clicks must
	// hit what the scroll bar draws.
	//
	// Given
	const BScrollBarInfo saved = GetScrollBarInfo();
	BScrollBarInfo info = saved;
	info.doubleArrows = true;
	ASSERT_TRUE(SetScrollBarInfo(info).has_value());
	auto owned = std::make_unique<ScrollWindow>();
	BView* target = owned->AddChild(std::make_unique<BView>(gfx::Rect{0, 20, 200, 120},
		"target", kFollowLeftTop, BViewFlag::WillDraw));
	owned->AddChild(std::make_unique<BScrollBar>(gfx::Rect{0, 0, 200, 15}, "bar", target,
		0.0f, 100.0f, BOrientation::Horizontal));
	const BMessenger window = *BLooper::Run(std::move(owned));
	auto shortOwned = std::make_unique<ScrollWindow>();
	BView* shortTarget = shortOwned->AddChild(std::make_unique<BView>(
		gfx::Rect{0, 20, 90, 120}, "target", kFollowLeftTop, BViewFlag::WillDraw));
	shortOwned->AddChild(std::make_unique<BScrollBar>(gfx::Rect{0, 0, 90, 15}, "bar",
		shortTarget, 0.0f, 100.0f, BOrientation::Horizontal));
	const BMessenger shortWindow = *BLooper::Run(std::move(shortOwned));
	ASSERT_TRUE(SetScrollBarInfo(saved).has_value());
	const auto click = [](const BMessenger& to, float x) {
		EXPECT_TRUE(to.SendMessage(Event(kMouseDown, {x, 7}, 1)).has_value());
		EXPECT_TRUE(to.SendMessage(Event(kMouseUp, {x, 7})).has_value());
		return AskValue(to);
	};

	// When
	std::vector<float> values;
	for (float x : {20.0f, 178.0f, 190.0f, 7.0f})
		values.push_back(click(window, x));
	const float paged = click(shortWindow, 66);

	// Then
	EXPECT_EQ(values, (std::vector<float>{1, 0, 1, 0}));
	EXPECT_FLOAT_EQ(paged, 10);
	for (const BMessenger& running : {window, shortWindow}) {
		ASSERT_TRUE(running.SendMessage(kQuitRequested).has_value());
		ASSERT_TRUE(running.WaitForLooperQuit(5s).has_value());
	}
}


TEST(ScrollBarTest, AHeldArrowRepeatsAfterTheDelay)
{
	// What is tested: The down arrow of a scroll bar in a running window
	// held with a ManualClock as the default clock, the time advanced by
	// 299 ms, then to 400 ms, then released and advanced again.
	// Success criteria: The press steps once (1). Until 300 ms nothing more
	// happens; from 300 ms on every 25 ms one step: at 400 ms 1 + 5 = 6.
	// After the release the value stays.
	// Why it matters: Holding an arrow scrolls on, after Haiku's delay of
	// 300 ms and at its pace; the clock makes it testable (M22).
	//
	// Given
	ManualClock clock;
	Clock::SetDefault(&clock);
	auto owned = std::make_unique<ScrollWindow>();
	BView* target = owned->AddChild(std::make_unique<BView>(gfx::Rect{20, 0, 120, 100},
		"target", kFollowLeftTop, BViewFlag::WillDraw));
	owned->AddChild(std::make_unique<BScrollBar>(gfx::Rect{0, 0, 15, 100}, "bar", target,
		0.0f, 1000.0f, BOrientation::Vertical));
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	ASSERT_TRUE(window.SendMessage(Event(kMouseDown, {7, 92}, 1)).has_value());
	const float pressed = AskValue(window);
	clock.Advance(299ms);
	const float beforeDelay = AskValue(window);
	clock.Advance(101ms);
	const float held = AskValue(window);
	ASSERT_TRUE(window.SendMessage(Event(kMouseUp, {7, 92})).has_value());
	(void)AskValue(window);
	clock.Advance(100ms);
	const float released = AskValue(window);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	Clock::SetDefault(nullptr);

	// Then
	EXPECT_FLOAT_EQ(pressed, 1);
	EXPECT_FLOAT_EQ(beforeDelay, 1);
	EXPECT_FLOAT_EQ(held, 6);
	EXPECT_FLOAT_EQ(released, 6);
}


TEST(ScrollBarTest, TheWheelScrollsTheViewUnderTheMouse)
{
	// What is tested: The mouse wheel in a running window over a target
	// with a vertical scroll bar (small step 1, large step 10), with and
	// without Shift; over a child of the target that has no scroll bar;
	// over the scroll bar itself.
	// Success criteria: Two wheel steps scroll by 2 · 3 small steps = 6,
	// with Shift by 2 large steps = 20. The child passes the wheel on to
	// its parent, the target: another 6. The bar itself scrolls by 6 once
	// (Haiku passed it on to the parents as well).
	// Why it matters: The wheel is the most common way to scroll.
	//
	// Given
	auto owned = std::make_unique<ScrollWindow>();
	BView* target = owned->AddChild(std::make_unique<BView>(gfx::Rect{20, 0, 120, 100},
		"target", kFollowLeftTop, BViewFlag::WillDraw));
	target->AddChild(std::make_unique<BView>(gfx::Rect{50, 50, 90, 90}, "child",
		kFollowLeftTop, BViewFlag::WillDraw));
	owned->AddChild(std::make_unique<BScrollBar>(gfx::Rect{0, 0, 15, 100}, "bar", target,
		0.0f, 1000.0f, BOrientation::Vertical));
	const BMessenger window = *BLooper::Run(std::move(owned));
	const auto send = [&](const BMessage& message) {
		EXPECT_TRUE(window.SendMessage(message).has_value());
	};

	// When / Then
	send(Event(kMouseMoved, {30, 10}));
	send(Wheel(2));
	EXPECT_FLOAT_EQ(AskValue(window), 6);
	send(Wheel(2, BModifier::Shift));
	EXPECT_FLOAT_EQ(AskValue(window), 26);
	send(Event(kMouseMoved, {80, 60}));
	send(Wheel(2));
	EXPECT_FLOAT_EQ(AskValue(window), 32);
	send(Event(kMouseMoved, {7, 50}));
	send(Wheel(2));
	BMessage reply;
	ASSERT_TRUE(window.SendMessage(BMessage(kAsk), &reply).has_value());
	EXPECT_FLOAT_EQ(reply.GetFloat("value", -1), 38);
	EXPECT_EQ(reply.GetPoint("scroll", {}), (gfx::Point{0, 38}));
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
}


TEST(ScrollViewTest, AtAFrameItSurroundsTheTarget)
{
	// What is tested: A scroll view with both bars and a fancy border
	// around a target at (0, 0, 200, 100).
	// Success criteria: Haiku's frame: the target plus the bars (14 each)
	// plus 2 of border on each side: (-2, -2, 216, 116); the target is
	// moved inside the border to (2, 2, 202, 102); the horizontal bar runs
	// below it from x 1 to 203, overlapping the frame on the left and the
	// vertical bar by a pixel, (1, 102, 203, 117); the vertical bar
	// (202, 1, 217, 103). Both bars scroll the target.
	// Why it matters: Programs without layout place scroll views from the
	// frames of their targets.
	//
	// Given
	auto owned = std::make_unique<BView>(gfx::Rect{0, 0, 200, 100}, "target",
		kFollowLeftTop, BViewFlag::WillDraw);
	BView* target = owned.get();

	// When
	BScrollView scrollView("scroll", std::move(owned), kFollowLeftTop, {}, true, true);

	// Then
	EXPECT_EQ(scrollView.Frame(), (gfx::Rect{-2, -2, 216, 116}));
	EXPECT_EQ(scrollView.Target(), target);
	EXPECT_EQ(target->Frame(), (gfx::Rect{2, 2, 202, 102}));
	BScrollBar* horizontal = scrollView.ScrollBar(BOrientation::Horizontal);
	BScrollBar* vertical = scrollView.ScrollBar(BOrientation::Vertical);
	ASSERT_NE(horizontal, nullptr);
	ASSERT_NE(vertical, nullptr);
	EXPECT_EQ(horizontal->Frame(), (gfx::Rect{1, 102, 203, 117}));
	EXPECT_EQ(vertical->Frame(), (gfx::Rect{202, 1, 217, 103}));
	EXPECT_EQ(horizontal->Target(), target);
	EXPECT_EQ(target->ScrollBar(BOrientation::Vertical), vertical);
}


TEST(ScrollViewTest, ANewBorderGivesTheFramesOfThatBorder)
{
	// What is tested: Scroll views at a frame around a target at (0, 0,
	// 200, 100), with both bars, only the vertical one and only the
	// horizontal one: each made with one border style (plain, fancy, none)
	// and then given another with SetBorder().
	// Success criteria: Afterwards the scroll view, its target and its bars
	// have exactly the frames of a scroll view made with the new border
	// style from the start; Border() reports it.
	// Why it matters: Haiku moves and resizes the scroll view and its bars
	// by the border's difference, with a gap of a pixel where a border
	// meets a bar; done wrong, the target slides under a bar.
	//
	// Given
	const BBorderStyle styles[] = {BBorderStyle::Plain, BBorderStyle::Fancy,
		BBorderStyle::None};
	const std::pair<bool, bool> barSets[] = {{true, true}, {false, true}, {true, false}};
	const auto make = [](BBorderStyle border, std::pair<bool, bool> bars) {
		auto target = std::make_unique<BView>(gfx::Rect{0, 0, 200, 100}, "target",
			kFollowLeftTop, BViewFlag::WillDraw);
		return std::make_unique<BScrollView>("scroll", std::move(target), kFollowLeftTop,
			BViewFlags{}, bars.first, bars.second, border);
	};
	const auto frames = [](const BScrollView& view) {
		std::vector<gfx::Rect> all{view.Frame(), view.Target()->Frame()};
		for (BOrientation orientation : {BOrientation::Horizontal, BOrientation::Vertical}) {
			if (const BScrollBar* bar = view.ScrollBar(orientation))
				all.push_back(bar->Frame());
		}
		return all;
	};

	// When / Then
	for (const std::pair<bool, bool>& bars : barSets) {
		for (BBorderStyle from : styles) {
			for (BBorderStyle to : styles) {
				std::unique_ptr<BScrollView> changed = make(from, bars);
				changed->SetBorder(to);
				SCOPED_TRACE(testing::Message() << "bars " << bars.first << bars.second
					<< " from " << static_cast<int>(from) << " to " << static_cast<int>(to));
				EXPECT_EQ(changed->Border(), to);
				EXPECT_EQ(frames(*changed), frames(*make(to, bars)));
			}
		}
	}
}


TEST(ScrollViewTest, InALayoutItSetsUpItsBars)
{
	// What is tested: A scroll view for a layout with both bars around a
	// target of minimum 50 × 20 and preferred 400 × 300, resized to
	// 200 × 100 and laid out; then given a new target.
	// Success criteria: Minimum: the target's plus the bars, but at least
	// the vertical bar's 59, plus the border: 68 × 63. Laid out, the target
	// fills (2, 2, 184, 84), the bars run along it. The bars get ranges
	// from the target's preferred size: 399 - 199 = 200 wide and 299 - 99 =
	// 200 high, proportions 199 / 399 and 99 / 299, small steps 12 · 1.33 =
	// 16, large steps a page (199, 99). The new target takes the old one's
	// place and gives it back.
	// Why it matters: Text views and lists in layouts rely on the scroll
	// view to size them and to set up their bars.
	//
	// Given
	BWindow window({0, 0, 300, 200}, "scrolling");
	auto target = SizedChild();
	target->SetExplicitPreferredSize({400, 300});
	target->SetExplicitMaxSize({kSizeUnlimited, kSizeUnlimited});
	BView* first = target.get();
	auto* scrollView = static_cast<BScrollView*>(window.AddChild(
		std::make_unique<BScrollView>("scroll", std::move(target), BViewFlags{}, true,
			true)));

	// When
	const gfx::Size min = scrollView->MinSize();
	scrollView->ResizeTo(200, 100);
	scrollView->Layout(true);

	// Then
	EXPECT_EQ(min, (gfx::Size{68, 63}));
	EXPECT_EQ(first->Frame(), (gfx::Rect{2, 2, 184, 84}));
	BScrollBar* horizontal = scrollView->ScrollBar(BOrientation::Horizontal);
	BScrollBar* vertical = scrollView->ScrollBar(BOrientation::Vertical);
	EXPECT_EQ(horizontal->Frame(), (gfx::Rect{1, 84, 185, 99}));
	EXPECT_EQ(vertical->Frame(), (gfx::Rect{184, 1, 199, 85}));
	float min0 = -1;
	float max0 = -1;
	horizontal->GetRange(&min0, &max0);
	EXPECT_FLOAT_EQ(max0, 200);
	vertical->GetRange(&min0, &max0);
	EXPECT_FLOAT_EQ(max0, 200);
	EXPECT_FLOAT_EQ(horizontal->Proportion(), 199.0f / 399.0f);
	EXPECT_FLOAT_EQ(vertical->Proportion(), 99.0f / 299.0f);
	float small = 0;
	float large = 0;
	vertical->GetSteps(&small, &large);
	EXPECT_FLOAT_EQ(small, 16);
	EXPECT_FLOAT_EQ(large, 99);

	std::unique_ptr<BView> old = scrollView->SetTarget(SizedChild());
	EXPECT_EQ(old.get(), first);
	EXPECT_EQ(scrollView->ChildAt(0), scrollView->Target());
	EXPECT_EQ(scrollView->Target()->Frame(), (gfx::Rect{2, 2, 184, 84}));
	EXPECT_EQ(vertical->Target(), scrollView->Target());
	EXPECT_EQ(old->ScrollBar(BOrientation::Vertical), nullptr);
}


// A pane for split views: minimum \a minWidth × 20, preferred
// (\a minWidth + 50) × 30, unlimited maximum.
std::unique_ptr<BView>
Pane(std::string_view name, float minWidth)
{
	auto pane = std::make_unique<BView>(name);
	pane->SetExplicitMinSize({minWidth, 20});
	pane->SetExplicitPreferredSize({minWidth + 50, 30});
	pane->SetExplicitMaxSize({kSizeUnlimited, kSizeUnlimited});
	return pane;
}


TEST(SplitViewTest, ItemsShareTheRoomByWeight)
{
	// What is tested: A horizontal split view with panes of minimum width
	// 50 and 30 (default spacing 11, splitter 6), its sizes and its layout
	// at 300 × 100; a vertical one with the same panes.
	// Success criteria: Between the panes are the splitter and a spacing on
	// both sides: 28. Minimum 50 + 30 + 28 = 108 × 20; preferred as wide as
	// the minimum, as in Haiku (its SimpleLayouter ignores the preferred
	// sizes), 30 high; unlimited maximum. With weights 1 : 1 the 272 pixels
	// left are halved: (0, 0, 136, 100) and (164, 0, 300, 100). The vertical
	// one adds the heights: 50 × 68.
	// Why it matters: Split views divide windows like Haiku's Debugger and
	// Tracker's panels.
	//
	// Given
	BWindow window({0, 0, 400, 200}, "split");
	auto* split = static_cast<BSplitView*>(window.AddChild(
		std::make_unique<BSplitView>(BOrientation::Horizontal)));
	BLayoutItem* first = split->AddChild(Pane("a", 50), 1.0f);
	BLayoutItem* second = split->AddChild(Pane("b", 30), 1.0f);
	BSplitView vertical(BOrientation::Vertical);
	vertical.AddChild(Pane("a", 50), 1.0f);
	vertical.AddChild(Pane("b", 30), 1.0f);

	// When
	split->ResizeTo(300, 100);
	split->Layout(true);

	// Then
	EXPECT_FLOAT_EQ(split->Spacing(), 11);
	EXPECT_EQ(split->CountItems(), 2);
	EXPECT_EQ(split->MinSize(), (gfx::Size{108, 20}));
	EXPECT_EQ(split->PreferredSize(), (gfx::Size{108, 30}));
	EXPECT_EQ(split->MaxSize(), (gfx::Size{kSizeUnlimited, kSizeUnlimited}));
	EXPECT_EQ(first->View()->Frame(), (gfx::Rect{0, 0, 136, 100}));
	EXPECT_EQ(second->View()->Frame(), (gfx::Rect{164, 0, 300, 100}));
	EXPECT_EQ(vertical.MinSize(), (gfx::Size{50, 68}));
	EXPECT_EQ(split->SetLayout(std::make_unique<BGroupLayout>(BOrientation::Vertical)),
		nullptr);
}


TEST(SplitViewTest, DraggingMovesAndCollapses)
{
	// What is tested: The splitter of the split view of the previous test
	// (at x 147 .. 153) dragged by 30 to the right and released; then
	// dragged far to the left, once with the first pane collapsible, once
	// not.
	// Success criteria: The first pane grows to 166, the second shrinks to
	// 106 (from 194); on release the weights become the widths and a new
	// layout keeps the sizes. Dragged far left, the collapsible first pane
	// collapses (smaller than half its minimum): it is hidden, the second
	// pane starts after splitter and one spacing at 17 and is 283 wide. Not
	// collapsible, the first pane keeps its minimum 50 and the second gets
	// the rest from 78; then collapsible again, the splitter at 61 .. 67
	// dragged left collapses it.
	// Why it matters: Dragging and collapsing are what split views are for;
	// Haiku's rules decide where the panes end up.
	//
	// Given
	BWindow window({0, 0, 400, 200}, "split");
	auto* split = static_cast<BSplitView*>(window.AddChild(
		std::make_unique<BSplitView>(BOrientation::Horizontal)));
	BView* a = split->AddChild(Pane("a", 50), 1.0f)->View();
	BView* b = split->AddChild(Pane("b", 30), 1.0f)->View();
	split->ResizeTo(300, 100);
	split->Layout(true);

	// When
	split->MouseDown({150, 50});
	split->MouseMoved({180, 50}, BMouseTransit::Inside, nullptr);
	split->MouseUp({180, 50});
	split->Layout(true);
	const gfx::Rect aDragged = a->Frame();
	const gfx::Rect bDragged = b->Frame();
	const float weight0 = split->ItemWeight(0);
	const float weight1 = split->ItemWeight(1);
	split->SetCollapsible(0, false);
	split->MouseDown({180, 50});
	split->MouseMoved({10, 50}, BMouseTransit::Inside, nullptr);
	split->MouseUp({10, 50});
	split->Layout(true);
	const gfx::Rect aRigid = a->Frame();
	const gfx::Rect bRigid = b->Frame();
	split->SetCollapsible(0, true);
	split->MouseDown({64, 50});
	split->MouseMoved({10, 50}, BMouseTransit::Inside, nullptr);
	split->MouseUp({10, 50});
	split->Layout(true);

	// Then
	EXPECT_EQ(aDragged, (gfx::Rect{0, 0, 166, 100}));
	EXPECT_EQ(bDragged, (gfx::Rect{194, 0, 300, 100}));
	EXPECT_FLOAT_EQ(weight0, 166);
	EXPECT_FLOAT_EQ(weight1, 106);
	EXPECT_EQ(aRigid, (gfx::Rect{0, 0, 50, 100}));
	EXPECT_EQ(bRigid, (gfx::Rect{78, 0, 300, 100}));
	EXPECT_TRUE(split->IsItemCollapsed(0));
	EXPECT_TRUE(a->IsHidden());
	EXPECT_EQ(b->Frame(), (gfx::Rect{17, 0, 300, 100}));
}


TEST(SplitViewTest, TheOtherPaneCollapsesAndMaximaHold)
{
	// What is tested: The split view of 300 × 100 with panes of minimum
	// width 50 and 30: the splitter dragged far right and released, then
	// dragged back to the middle; the first pane given a maximum width of
	// 100 and the splitter dragged right again.
	// Success criteria: Dragged far right, the second pane (smaller than
	// half its minimum) collapses: it is hidden and the first pane takes
	// all but the splitter and a spacing, 283. Dragged back, the second
	// pane is shown again beside the first. With its maximum, the first
	// pane stops at 100 and the second takes the rest after the splitter
	// and both spacings, from 128.
	// Why it matters: Haiku's splitter rules cover collapsing either side
	// and the panes' maxima; a split view must never break them.
	//
	// Given
	BWindow window({0, 0, 400, 200}, "split");
	auto* split = static_cast<BSplitView*>(window.AddChild(
		std::make_unique<BSplitView>(BOrientation::Horizontal)));
	BView* a = split->AddChild(Pane("a", 50), 1.0f)->View();
	BView* b = split->AddChild(Pane("b", 30), 1.0f)->View();
	split->ResizeTo(300, 100);
	split->Layout(true);
	const auto drag = [&](float from, float to) {
		split->MouseDown({from, 50});
		split->MouseMoved({to, 50}, BMouseTransit::Inside, nullptr);
		split->MouseUp({to, 50});
		split->Layout(true);
	};

	// When
	drag(150, 298);
	const bool collapsed = split->IsItemCollapsed(1);
	const bool hidden = b->IsHidden();
	const gfx::Rect aAlone = a->Frame();
	// the splitter after the first pane's spacing (at 294 .. 300)
	drag(aAlone.x1 + 11 + 3, 150);
	const bool collapsedAgain = split->IsItemCollapsed(1);
	const bool shown = !b->IsHidden();
	a->SetExplicitMaxSize({100, kSizeUnlimited});
	split->Layout(true);
	drag(a->Frame().x1 + 14, 250);
	const gfx::Rect aLimited = a->Frame();
	const gfx::Rect bRest = b->Frame();

	// Then
	EXPECT_TRUE(collapsed);
	EXPECT_TRUE(hidden);
	EXPECT_EQ(aAlone, (gfx::Rect{0, 0, 283, 100}));
	EXPECT_FALSE(collapsedAgain);
	EXPECT_TRUE(shown);
	EXPECT_LT(a->Frame().x1, b->Frame().x0);
	EXPECT_EQ(aLimited, (gfx::Rect{0, 0, 100, 100}));
	EXPECT_EQ(bRest, (gfx::Rect{128, 0, 300, 100}));
}


TEST(SplitViewTest, SettersChangeTheSizes)
{
	// What is tested: A horizontal split view with panes of minimum width
	// 50 and 30: SetSpacing(5), SetSplitterSize(10), SetInsets(4),
	// SetItemCollapsed(0) and back, SetOrientation(Vertical).
	// Success criteria: The minimum width is the panes' minima plus the
	// splitter, two spacings and the insets: 50 + 30 + 10 + 10 + 8 = 108
	// with the new values; a collapsed pane is hidden and shown again when
	// uncollapsed; vertical, the minimum adds the heights (20 + 20 + 10 +
	// 10 + 8) and takes the widest pane plus the insets (58).
	// Why it matters: Applications tune split views; the layout follows
	// at once.
	//
	// Given
	BSplitView split(BOrientation::Horizontal);
	BView* a = split.AddChild(Pane("a", 50), 1.0f)->View();
	(void)split.AddChild(Pane("b", 30), 1.0f);

	// When
	split.SetSpacing(5);
	split.SetSplitterSize(10);
	split.SetInsets(4);
	const gfx::Size horizontal = split.MinSize();
	split.SetItemCollapsed(0, true);
	const bool hidden = a->IsHidden();
	split.SetItemCollapsed(0, false);
	const bool shown = !a->IsHidden();
	split.SetOrientation(BOrientation::Vertical);
	const gfx::Size vertical = split.MinSize();

	// Then
	EXPECT_FLOAT_EQ(split.Spacing(), 5);
	EXPECT_FLOAT_EQ(split.SplitterSize(), 10);
	EXPECT_EQ(horizontal, (gfx::Size{108, 28}));
	EXPECT_TRUE(hidden);
	EXPECT_TRUE(shown);
	EXPECT_EQ(split.Orientation(), BOrientation::Vertical);
	EXPECT_EQ(vertical, (gfx::Size{58, 68}));
}


TEST(SplitViewTest, TheBuilderNestsSplitViews)
{
	// What is tested: BLayoutBuilder::Group with AddSplit(), into which a
	// view, a nested group with a view and a nested split view are added,
	// the first not collapsible, insets 5.
	// Success criteria: The window's group has the split view as its only
	// item; the split view has three items, the second a group layout with
	// one view, the third a split view with one item; item 0 is not
	// collapsible, item 1 is; the insets are 5.
	// Why it matters: Haiku's applications build split views with the
	// builder (Split, AddSplit()).
	//
	// Given
	BWindow window({0, 0, 400, 200}, "split");
	BSplitView* split = nullptr;
	BSplitView* inner = nullptr;

	// When
	BLayoutBuilder::Group<>(&window, BOrientation::Vertical)
		.AddSplit(BOrientation::Horizontal)
			.GetSplitView(&split)
			.Add(Pane("list", 50))
			.AddGroup(BOrientation::Vertical)
				.Add(Pane("details", 30))
			.End()
			.AddSplit(BOrientation::Vertical, 0.0f)
				.GetSplitView(&inner)
				.Add(Pane("preview", 20))
			.End()
			.SetCollapsible(0, false)
			.SetInsets(5)
		.End();

	// Then
	ASSERT_NE(split, nullptr);
	EXPECT_EQ(window.GetLayout()->CountItems(), 1);
	EXPECT_EQ(split->CountItems(), 3);
	EXPECT_EQ(split->Parent(), window.ChildAt(0)->Parent());
	EXPECT_EQ(inner->CountItems(), 1);
	EXPECT_EQ(inner->Parent(), split);
	EXPECT_FALSE(split->IsCollapsible(0));
	EXPECT_TRUE(split->IsCollapsible(1));
	float left = 0;
	split->GetInsets(&left, nullptr, nullptr, nullptr);
	EXPECT_FLOAT_EQ(left, 5);
}


// Whether \a actual is \a expected up to float rounding.
::testing::AssertionResult
RectNear(const gfx::Rect& actual, const gfx::Rect& expected)
{
	const auto near = [](float a, float b) { return std::abs(a - b) < 1e-3f; };
	if (near(actual.x0, expected.x0) && near(actual.y0, expected.y0)
		&& near(actual.x1, expected.x1) && near(actual.y1, expected.y1)) {
		return ::testing::AssertionSuccess();
	}
	return ::testing::AssertionFailure() << "(" << actual.x0 << ", " << actual.y0 << ", "
		<< actual.x1 << ", " << actual.y1 << ")";
}


// A page for tab views named \a name: minimum \a min, preferred 50 more
// in both directions, unlimited maximum.
std::unique_ptr<BView>
Page(std::string_view name, gfx::Size min)
{
	auto page = std::make_unique<BView>(name);
	page->SetExplicitMinSize(min);
	page->SetExplicitPreferredSize({min.w + 50, min.h + 30});
	page->SetExplicitMaxSize({kSizeUnlimited, kSizeUnlimited});
	return page;
}


TEST(TabViewTest, GeometryIsHaikus)
{
	// What is tested: A tab view for a layout in a window with the pages
	// "General" (minimum 100 × 50) and "Advanced" (80 × 60), tabs as wide
	// as the widest label, with the placeholder font; laid out at
	// 300 × 200.
	// Success criteria: The tabs are ceil(12 + 1.3 · 6) = 20 high (Haiku's
	// measure: 21 rows). Padding ceil(6 · 3.3) = 20, so each tab is
	// 57.6 + 20 = 77.6 wide, starting at the window spacing 11; Haiku's
	// inclusive frames share a pixel: (11, 0, 89.6, 21) and (88.6, 0,
	// 167.2, 21). The layout insets are 3 and 3 + 20 - 1 = 22 at the top:
	// minimum 106 × 85, preferred 156 × 115; the container fills (3, 22,
	// 297, 197). In the window the first tab is selected at once.
	// Why it matters: Preference windows are tab views; their tabs must be
	// where Haiku draws them and where clicks hit them.
	//
	// Given
	BWindow window({0, 0, 400, 300}, "tabs");
	auto* tabs = static_cast<BTabView*>(window.AddChild(std::make_unique<BTabView>("tabs")));

	// When
	tabs->AddTab(Page("General", {100, 50}));
	tabs->AddTab(Page("Advanced", {80, 60}));
	tabs->ResizeTo(300, 200);
	tabs->Layout(true);

	// Then
	EXPECT_FLOAT_EQ(tabs->TabHeight(), 20);
	EXPECT_TRUE(RectNear(tabs->TabFrame(0), {11, 0, 89.6f, 21}));
	EXPECT_TRUE(RectNear(tabs->TabFrame(1), {88.6f, 0, 167.2f, 21}));
	EXPECT_EQ(tabs->TabFrame(2), gfx::Rect{});
	EXPECT_EQ(tabs->MinSize(), (gfx::Size{106, 85}));
	EXPECT_EQ(tabs->PreferredSize(), (gfx::Size{156, 115}));
	EXPECT_EQ(tabs->ContainerView()->Frame(), (gfx::Rect{3, 22, 297, 197}));
	EXPECT_EQ(tabs->Selection(), 0);
	EXPECT_EQ(tabs->TabAt(1)->Label(), "Advanced");
}


TEST(TabViewTest, TabsSitOnEverySideInEveryWidth)
{
	// What is tested: The tab view of the previous test at 300 × 200:
	// TabFrame() of both tabs for tab widths as usual, as wide as their
	// labels and as the widest; each on the top, the bottom, the left and
	// the right side.
	// Success criteria: As usual, a tab is five paddings wide (100),
	// starting at the window spacing 11; from the label, it is the label
	// plus a padding (20) and the second starts after the first; the
	// widest makes both as wide as "General" plus a padding (77.6). At the
	// bottom the tabs end at the bounds' bottom, at the left and right they
	// run downwards with the tab height as their width. Frames are Haiku's
	// inclusive ones, one pixel more each way.
	// Why it matters: Haiku's tab views put their tabs on any side; clicks
	// and drawing take these frames.
	//
	// Given
	BWindow window({0, 0, 400, 300}, "tabs");
	auto* tabs = static_cast<BTabView*>(window.AddChild(std::make_unique<BTabView>("tabs")));
	tabs->AddTab(Page("General", {100, 50}));
	tabs->AddTab(Page("Advanced", {80, 60}));
	tabs->ResizeTo(300, 200);
	tabs->Layout(true);
	const float general = TextSystem::Default().StringWidth("General", FontRole::Plain);
	const float advanced = TextSystem::Default().StringWidth("Advanced", FontRole::Plain);
	// a tab from \a start to \a end along its side
	const auto onSide = [](BTabSide side, float start, float end) {
		switch (side) {
			case BTabSide::Top:
				return gfx::Rect{start, 0, end + 1, 21};
			case BTabSide::Bottom:
				return gfx::Rect{start, 179, end + 1, 200};
			case BTabSide::Left:
				return gfx::Rect{0, start, 21, end + 1};
			case BTabSide::Right:
				return gfx::Rect{279, start, 300, end + 1};
		}
		return gfx::Rect{};
	};

	// When / Then
	for (BTabSide side : {BTabSide::Top, BTabSide::Bottom, BTabSide::Left, BTabSide::Right}) {
		tabs->SetTabSide(side);
		EXPECT_EQ(tabs->TabSide(), side);
		tabs->SetTabWidth(BButtonWidth::AsUsual);
		EXPECT_TRUE(RectNear(tabs->TabFrame(0), onSide(side, 11, 111)));
		EXPECT_TRUE(RectNear(tabs->TabFrame(1), onSide(side, 111, 211)));
		tabs->SetTabWidth(BButtonWidth::FromLabel);
		EXPECT_EQ(tabs->TabWidth(), BButtonWidth::FromLabel);
		EXPECT_TRUE(RectNear(tabs->TabFrame(0), onSide(side, 11, 11 + general + 20)));
		EXPECT_TRUE(RectNear(tabs->TabFrame(1),
			onSide(side, 11 + general + 20, 11 + general + 20 + advanced + 20)));
		tabs->SetTabWidth(BButtonWidth::FromWidest);
		const float widest = std::max(general, advanced) + 20;
		EXPECT_TRUE(RectNear(tabs->TabFrame(0), onSide(side, 11, 11 + widest)));
		EXPECT_TRUE(RectNear(tabs->TabFrame(1), onSide(side, 11 + widest, 11 + 2 * widest)));
	}
	EXPECT_EQ(tabs->TabFrame(-1), gfx::Rect{});
}


TEST(TabViewTest, ClicksAndKeysSelectPages)
{
	// What is tested: The tab view of the previous test: Select(1), a
	// click on the first tab, the right arrow, Enter, the left arrow and
	// Space; then the second tab's view replaced by SetView().
	// Success criteria: Only the selected page is shown (the other one is
	// a hidden card). The click selects the first tab; the arrow moves the
	// focus tab to the second (wrapping), Enter selects it; the left arrow
	// and Space go back to the first. The new view takes the second card,
	// is shown once the tab is selected, and is laid out like the others
	// (Haiku left it out of the card layout).
	// Why it matters: Tabs are switched with the mouse and the keyboard.
	//
	// Given
	BWindow window({0, 0, 400, 300}, "tabs");
	auto* tabs = static_cast<BTabView*>(window.AddChild(std::make_unique<BTabView>("tabs")));
	tabs->AddTab(Page("General", {100, 50}));
	tabs->AddTab(Page("Advanced", {80, 60}));
	BView* first = tabs->ViewForTab(0);
	BView* second = tabs->ViewForTab(1);
	tabs->ResizeTo(300, 200);
	tabs->Layout(true);

	// When / Then
	tabs->Select(1);
	EXPECT_TRUE(first->IsHidden());
	EXPECT_FALSE(second->IsHidden());
	EXPECT_EQ(tabs->FocusTab(), 1);
	tabs->MouseDown({20, 10});
	EXPECT_EQ(tabs->Selection(), 0);
	EXPECT_FALSE(first->IsHidden());
	tabs->KeyDown(std::string(1, keys::kRightArrow));
	EXPECT_EQ(tabs->FocusTab(), 1);
	EXPECT_EQ(tabs->Selection(), 0);
	tabs->KeyDown(std::string(1, keys::kEnter));
	EXPECT_EQ(tabs->Selection(), 1);
	tabs->KeyDown(std::string(1, keys::kLeftArrow));
	tabs->KeyDown(" ");
	EXPECT_EQ(tabs->Selection(), 0);

	tabs->TabAt(1)->SetView(Page("Expert", {80, 60}));
	BView* expert = tabs->ViewForTab(1);
	EXPECT_EQ(tabs->TabAt(1)->Label(), "Expert");
	EXPECT_EQ(expert->Parent(), tabs->ContainerView());
	tabs->Select(1);
	tabs->Layout(true);
	EXPECT_FALSE(expert->IsHidden());
	EXPECT_EQ(expert->Frame(), (gfx::Rect{0, 0, 294, 175}));
}


TEST(TabViewTest, AtAFrameTheTabOwnsItsHiddenPage)
{
	// What is tested: A tab view at (0, 0, 300, 200) outside a window with
	// two pages; Select(0), Select(1); RemoveTab() of both tabs; another
	// tab added and selected.
	// Success criteria: Without a window nothing is selected at first.
	// The container is Haiku's (3, 23, 297, 197). The selected page is the
	// container's only child; the other page belongs to its tab. RemoveTab()
	// gives the tab back with its view; after the last one there is no
	// selection, so a new tab can be selected again (Haiku kept the old
	// selection and refused).
	// Why it matters: Programs without layout use tab views at frames;
	// the pages must not leak or be destroyed twice.
	//
	// Given
	BTabView tabs({0, 0, 300, 200}, "tabs");
	tabs.AddTab(std::make_unique<BView>(gfx::Rect{0, 0, 50, 50}, "One", kFollowLeftTop,
		BViewFlag::WillDraw));
	tabs.AddTab(std::make_unique<BView>(gfx::Rect{0, 0, 50, 50}, "Two", kFollowLeftTop,
		BViewFlag::WillDraw));
	BView* one = tabs.ViewForTab(0);
	BView* two = tabs.ViewForTab(1);

	// When / Then
	EXPECT_EQ(tabs.Selection(), -1);
	EXPECT_EQ(tabs.ContainerView()->Frame(), (gfx::Rect{3, 23, 297, 197}));
	EXPECT_EQ(tabs.TabFrame(0), (gfx::Rect{11, 0, 112, 21}));
	tabs.Select(0);
	EXPECT_EQ(one->Parent(), tabs.ContainerView());
	EXPECT_EQ(two->Parent(), nullptr);
	tabs.Select(1);
	EXPECT_EQ(one->Parent(), nullptr);
	EXPECT_EQ(two->Parent(), tabs.ContainerView());
	EXPECT_EQ(tabs.ContainerView()->CountChildren(), 1);

	std::unique_ptr<BTab> removed = tabs.RemoveTab(1);
	ASSERT_NE(removed, nullptr);
	EXPECT_EQ(removed->View(), two);
	EXPECT_EQ(two->Parent(), nullptr);
	EXPECT_EQ(tabs.Selection(), 0);
	EXPECT_EQ(one->Parent(), tabs.ContainerView());
	(void)tabs.RemoveTab(0);
	EXPECT_EQ(tabs.Selection(), -1);
	EXPECT_EQ(tabs.CountTabs(), 0);
	tabs.AddTab(std::make_unique<BView>(gfx::Rect{0, 0, 50, 50}, "Three", kFollowLeftTop,
		BViewFlag::WillDraw));
	tabs.Select(0);
	EXPECT_EQ(tabs.Selection(), 0);
	EXPECT_EQ(tabs.ViewForTab(0)->Parent(), tabs.ContainerView());
}


} // namespace
