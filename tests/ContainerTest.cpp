/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Box.h>

#include <chrono>
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


} // namespace
