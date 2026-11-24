/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ToolTip.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/TextView.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "support/FakePlatform.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;
using test::FakePlatform;
using test::PixelBackend;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kDetach = FourCharCode("DTCH");
constexpr std::uint32_t kRemoveTip = FourCharCode("RMTP");


// A view with a tooltip for each half: "Left" and "Right".
class HalvesView : public BView {
public:
	HalvesView()
		:
		BView({10, 160, 110, 190}, "halves", {}, {}),
		fLeft(std::make_shared<BTextToolTip>("Left")),
		fRight(std::make_shared<BTextToolTip>("Right"))
	{
	}

	bool GetToolTipAt(gfx::Point point, std::shared_ptr<BToolTip>* tip) override
	{
		*tip = point.x < Bounds().Width() / 2 ? fLeft : fRight;
		return true;
	}

private:
	std::shared_ptr<BToolTip> fLeft;
	std::shared_ptr<BToolTip> fRight;
};


// A window at (100, 100) on the screen with views at y 10 ("tipped", the
// tooltip "Hello"), 60 ("plain", none, with a child without one), 110
// ("sticky", a sticky "Follow"), 160 (HalvesView) and 210 ("Parent", with
// a child without one), each 100 × 30 at x 10, and "Corner" in the bottom
// right corner. It answers kAsk with its popups and the tooltip's frame
// and text; kDetach removes the tipped view, kRemoveTip the sticky view's
// tooltip.
class TipWindow : public BWindow {
public:
	explicit TipWindow(gfx::Rect frame)
		:
		BWindow(frame, "tips")
	{
		BView* tipped = AddChild(std::make_unique<BView>(gfx::Rect{10, 10, 110, 40}, "tipped",
			BResizingMode{}, BViewFlags{}));
		tipped->SetToolTip("Hello");
		BView* plain = AddChild(std::make_unique<BView>(gfx::Rect{10, 60, 110, 90}, "plain",
			BResizingMode{}, BViewFlags{}));
		plain->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 50, 30}, "child",
			BResizingMode{}, BViewFlags{}));
		auto sticky = std::make_shared<BTextToolTip>("Follow");
		sticky->SetSticky(true);
		AddChild(std::make_unique<BView>(gfx::Rect{10, 110, 110, 140}, "sticky",
			BResizingMode{}, BViewFlags{}))->SetToolTip(sticky);
		AddChild(std::make_unique<HalvesView>());
		BView* parent = AddChild(std::make_unique<BView>(gfx::Rect{10, 210, 110, 240}, "parent",
			BResizingMode{}, BViewFlags{}));
		parent->SetToolTip("Parent");
		parent->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 50, 30}, "inner",
			BResizingMode{}, BViewFlags{}));
		// a view in the bottom right corner, for the screen's edges
		AddChild(std::make_unique<BView>(gfx::Rect{frame.Width() - 100, frame.Height() - 30,
			frame.Width(), frame.Height()}, "corner", BResizingMode{}, BViewFlags{}))
			->SetToolTip("Corner");
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kDetach) {
			fDetached = RemoveChild(FindView("tipped"));
			return;
		}
		if (message->what == kRemoveTip) {
			FindView("sticky")->SetToolTip(std::shared_ptr<BToolTip>());
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		(void)reply.AddInt32("popups", CountPopups());
		for (std::int32_t i = 0; i < CountPopups(); i++) {
			BView* popup = PopupAt(i);
			if (popup->Name() != "tool tip")
				continue;
			(void)reply.AddRect("frame", popup->Frame());
			if (auto* text = dynamic_cast<BTextView*>(popup->ChildAt(0))) {
				(void)reply.AddString("text", std::string(text->Text()));
				(void)reply.AddRect("textFrame", text->Frame());
			}
		}
		(void)message->SendReply(reply);
	}

private:
	std::unique_ptr<BView> fDetached;
};


// What a TipWindow answers.
struct TipState {
	std::int32_t popups = -1;
	gfx::Rect frame;
	std::string text;
	gfx::Rect textFrame;
};


// A running application on the fake platform (a screen of 800 × 600) with
// a TipWindow at \a frame, the fixed text system and a ManualClock.
class RunningTips {
public:
	explicit RunningTips(gfx::Rect frame = {100, 100, 500, 400})
		:
		fApp("application/x-vnd.ngui-tool-tip-test")
	{
		Clock::SetDefault(&fClock);
		TextSystem::SetDefault(&fText);
		fApp.SetPulseRate(Clock::Time::zero());
		auto platform = std::make_unique<FakePlatform>();
		fPlatform = platform.get();
		EXPECT_TRUE(fApp.UsePlatform(std::move(platform)).has_value());
		fRunner = std::thread([this] { (void)fApp.Run(); });
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		auto window = std::make_unique<TipWindow>(frame);
		EXPECT_TRUE(window->UseBackend(std::make_unique<PixelBackend>()).has_value());
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
	}

	~RunningTips()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
		TextSystem::SetDefault(nullptr);
		Clock::SetDefault(nullptr);
	}

	// The mouse moved to \a where in the window's content area.
	void Move(gfx::Point where, bool exited = false)
	{
		BMessage message(kMouseMoved);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", 0);
		if (exited)
			(void)message.AddBool("be:exited", true);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Key(char byte)
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(what).has_value());
	}

	// Time passes, after the window handled what was sent to it; what the
	// time ran has been handled when this returns.
	void Wait(Clock::Time duration)
	{
		(void)Ask();
		fClock.Advance(duration);
		(void)Ask();
	}

	TipState Ask()
	{
		// Twice: the first answer may come before the messages the window
		// posted to itself.
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		TipState state;
		state.popups = reply.GetInt32("popups", -1);
		state.frame = reply.GetRect("frame", {});
		state.text = reply.GetString("text", "");
		state.textFrame = reply.GetRect("textFrame", {});
		return state;
	}

	// Whether the last window made on the platform is a tooltip popup.
	bool LastIsToolTip()
	{
		const std::vector<test::FakeWindow*> windows = fPlatform->Windows();
		return !windows.empty() && windows.back()->IsPopup() && windows.back()->IsToolTip();
	}

private:
	ManualClock fClock;
	test::FixedTextSystem fText;
	BApplication fApp;
	FakePlatform* fPlatform = nullptr;
	std::thread fRunner;
	BMessenger fWindow;
};


TEST(ToolTipTest, TipsAndTheManagerHaveHaikusDefaults)
{
	// What is tested: A new BTextToolTip, its text changed; one of two
	// lines; a view given a text twice, then an empty one; the manager's
	// delays set beyond their limits.
	// Success criteria: Haiku's defaults: right of and below the mouse
	// (alignment Right, Bottom), at the big spacing (ceil(11 · 1.8) = 20)
	// in both directions, not sticky; the content is a read-only text view
	// without word wrap, not scrolled (also with two lines, where turning
	// off word wrap scrolled to the caret). A second text changes the view's text tooltip
	// instead of making a new one; an empty text removes it. The show delay
	// stays within 10 ms .. 3 s (750 ms by default), the hide delay within
	// 0 .. 500 ms (50 ms by default).
	// Why it matters: Applications rely on Haiku's defaults and limits.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextToolTip tip("Hello");
	BTextToolTip twoLines("One\nTwo");
	BView view({0, 0, 10, 10}, "view", BResizingMode{}, BViewFlags{});
	BToolTipManager* manager = BToolTipManager::Manager();
	const Clock::Time showDelay = manager->ShowDelay();
	const Clock::Time hideDelay = manager->HideDelay();

	// When
	tip.SetText("Changed");
	view.SetToolTip("First");
	const std::shared_ptr<BToolTip> first = view.ToolTip();
	view.SetToolTip("Second");
	const std::shared_ptr<BToolTip> second = view.ToolTip();
	view.SetToolTip("");
	manager->SetShowDelay(1ms);
	const Clock::Time shortestShow = manager->ShowDelay();
	manager->SetShowDelay(5s);
	const Clock::Time longestShow = manager->ShowDelay();
	manager->SetHideDelay(-1ms);
	const Clock::Time shortestHide = manager->HideDelay();
	manager->SetHideDelay(1s);
	const Clock::Time longestHide = manager->HideDelay();
	manager->SetShowDelay(showDelay);
	manager->SetHideDelay(hideDelay);

	// Then
	EXPECT_EQ(tip.Alignment(), (BAlignment{BHorizontalAlignment::Right,
		BVerticalAlignment::Bottom}));
	EXPECT_EQ(tip.MouseRelativeLocation(), (gfx::Point{20, 20}));
	EXPECT_FALSE(tip.IsSticky());
	auto* text = dynamic_cast<BTextView*>(tip.View());
	ASSERT_NE(text, nullptr);
	EXPECT_FALSE(text->IsEditable());
	EXPECT_FALSE(text->DoesWordWrap());
	EXPECT_EQ(text->Bounds().x0, 0);
	EXPECT_EQ(twoLines.View()->Bounds().x0, 0);
	EXPECT_EQ(twoLines.View()->Bounds().y0, 0);
	EXPECT_EQ(tip.Text(), "Changed");
	EXPECT_EQ(first, second);
	EXPECT_EQ(static_cast<BTextToolTip*>(second.get())->Text(), "Second");
	EXPECT_EQ(view.ToolTip(), nullptr);
	EXPECT_EQ(showDelay, 750ms);
	EXPECT_EQ(hideDelay, 50ms);
	EXPECT_EQ(shortestShow, 10ms);
	EXPECT_EQ(longestShow, 3s);
	EXPECT_EQ(shortestHide, 0ms);
	EXPECT_EQ(longestHide, 500ms);
	TextSystem::SetDefault(nullptr);
}


TEST(ToolTipTest, ARestingMouseShowsTheTip)
{
	// What is tested: In a window at (100, 100) on the screen, the mouse
	// moved to (20, 20) over a view with the tooltip "Hello" and resting
	// 749 ms, then 1 ms more; separately, resting over a view without a
	// tooltip.
	// Success criteria: Nothing shows before the show delay (750 ms). Then
	// a tooltip popup of the window shows "Hello": made as a tooltip of the
	// platform (no input). Its content starts 20 pixels right of and below
	// the mouse at (120, 120) on the screen, its border a pixel outside:
	// at (139, 139). It is the text view's preferred size (the text, 5 · 7
	// = 35 wide, and its insets) with half the item spacing (6) and the
	// border around it. Not editable, the text view gets the height for its
	// width (the text without insets, as in Haiku), centered: 2 pixels less,
	// one lower. Over a view without a tooltip nothing shows.
	// Why it matters: This is when and where Haiku shows tooltips.
	//
	// Given
	RunningTips tips;

	// When
	tips.Move({70, 70});
	tips.Wait(750ms);
	const TipState none = tips.Ask();
	tips.Move({20, 20});
	tips.Wait(749ms);
	const TipState early = tips.Ask();
	tips.Wait(1ms);
	const TipState shown = tips.Ask();
	const bool tooltipWindow = tips.LastIsToolTip();

	// Then
	EXPECT_EQ(none.popups, 0);
	EXPECT_EQ(early.popups, 0);
	ASSERT_EQ(shown.popups, 1);
	EXPECT_EQ(shown.text, "Hello");
	EXPECT_TRUE(tooltipWindow);
	EXPECT_EQ(shown.frame.x0, 139);
	EXPECT_EQ(shown.frame.y0, 139);
	EXPECT_EQ(shown.textFrame.x0, 7);
	EXPECT_EQ(shown.textFrame.y0, 8);
	EXPECT_EQ(shown.frame.Width(), shown.textFrame.Width() + 14);
	EXPECT_EQ(shown.frame.Height(), shown.textFrame.Height() + 2 + 14);
	EXPECT_GE(shown.textFrame.Width(), 35);
}


TEST(ToolTipTest, MovesAndKeysHideTheTip)
{
	// What is tested: The tooltip of the view at (10, 10) shown; the mouse
	// moved a pixel, then 49 ms and 1 ms pass; shown again and a key
	// pressed; shown again, the mouse moved and the tip shown again within
	// the hide delay (delays of 10 ms and 500 ms).
	// Success criteria: A move hides the tip after the hide delay (50 ms):
	// still there after 49 ms, gone after 50. A key hides it likewise. Shown
	// again while hidden, it stays.
	// Why it matters: A tooltip must not get in the way of the work; Haiku
	// hides it on every move and key.
	//
	// Given
	RunningTips tips;
	tips.Move({20, 20});
	tips.Wait(750ms);

	// When
	tips.Move({21, 20});
	tips.Wait(49ms);
	const TipState moved = tips.Ask();
	tips.Wait(1ms);
	const TipState gone = tips.Ask();
	tips.Wait(750ms);
	const TipState again = tips.Ask();
	tips.Key('a');
	tips.Wait(50ms);
	const TipState keyed = tips.Ask();
	BToolTipManager::Manager()->SetShowDelay(10ms);
	BToolTipManager::Manager()->SetHideDelay(500ms);
	tips.Move({22, 20});
	tips.Wait(10ms);
	tips.Move({23, 20});
	tips.Wait(10ms);
	tips.Wait(499ms);
	const TipState kept = tips.Ask();
	BToolTipManager::Manager()->SetShowDelay(750ms);
	BToolTipManager::Manager()->SetHideDelay(50ms);

	// Then
	EXPECT_EQ(moved.popups, 1);
	EXPECT_EQ(gone.popups, 0);
	EXPECT_EQ(again.popups, 1);
	EXPECT_EQ(keyed.popups, 0);
	EXPECT_EQ(kept.popups, 1);
	EXPECT_EQ(kept.text, "Hello");
}


TEST(ToolTipTest, AStickyTipFollowsUntilTheMouseLeaves)
{
	// What is tested: The sticky tooltip "Follow" of the view at (10, 110)
	// shown with the mouse at (20, 120); the mouse moved to (30, 125), a key
	// pressed; then the mouse moved out of the view to (20, 150); shown
	// again and the window's mouse leaving it; shown again and its tooltip
	// removed.
	// Success criteria: The sticky tip moves with the mouse by (10, 5) and
	// stays on the key. Leaving the view hides it, after the hide delay.
	// Leaving the window hides it too, and removing the tooltip from the
	// view.
	// Why it matters: Sticky tooltips (Haiku's SetSticky()) follow the mouse
	// over their view and must still go away.
	//
	// Given
	RunningTips tips;
	tips.Move({20, 120});
	tips.Wait(750ms);
	const TipState shown = tips.Ask();

	// When
	tips.Move({30, 125});
	tips.Key('a');
	tips.Wait(50ms);
	const TipState followed = tips.Ask();
	tips.Move({20, 150});
	tips.Wait(50ms);
	const TipState left = tips.Ask();
	tips.Move({20, 120});
	tips.Wait(750ms);
	tips.Move({500, 120}, true);
	tips.Wait(50ms);
	const TipState outside = tips.Ask();
	tips.Move({20, 120});
	tips.Wait(750ms);
	const TipState shownAgain = tips.Ask();
	tips.Send(kRemoveTip);
	tips.Wait(50ms);
	const TipState removed = tips.Ask();

	// Then
	ASSERT_EQ(shown.popups, 1);
	EXPECT_EQ(shown.text, "Follow");
	ASSERT_EQ(followed.popups, 1);
	EXPECT_EQ(followed.frame.x0, shown.frame.x0 + 10);
	EXPECT_EQ(followed.frame.y0, shown.frame.y0 + 5);
	EXPECT_EQ(left.popups, 0);
	EXPECT_EQ(outside.popups, 0);
	EXPECT_EQ(shownAgain.popups, 1);
	EXPECT_EQ(removed.popups, 0);
}


TEST(ToolTipTest, TipsComeFromTheViewUnderTheMouse)
{
	// What is tested: The mouse resting over a child without a tooltip in a
	// view without one, and in a view with "Parent"; over the left and then
	// the right half of a view whose GetToolTipAt() gives "Left" and
	// "Right"; over the view with "Hello", which is then removed from the
	// window.
	// Success criteria: Without any tooltip nothing shows; a child without
	// one shows its parent's (kMouseIdle goes on to the parent). The halves
	// show their own tips: moving to the right half hides "Left", and
	// resting there shows "Right". A tip closes at once when its view
	// leaves the window.
	// Why it matters: Lists and other views with items give a tooltip per
	// item (GetToolTipAt()); a tip must not outlive its view.
	//
	// Given
	RunningTips tips;

	// When
	tips.Move({20, 70});
	tips.Wait(750ms);
	const TipState none = tips.Ask();
	tips.Move({20, 220});
	tips.Wait(750ms);
	const TipState parent = tips.Ask();
	tips.Move({20, 170});
	tips.Wait(750ms);
	const TipState left = tips.Ask();
	tips.Move({100, 170});
	tips.Wait(750ms);
	const TipState right = tips.Ask();
	tips.Move({20, 20});
	tips.Wait(750ms);
	const TipState hello = tips.Ask();
	tips.Send(kDetach);
	const TipState detached = tips.Ask();

	// Then
	EXPECT_EQ(none.popups, 0);
	EXPECT_EQ(parent.text, "Parent");
	EXPECT_EQ(left.text, "Left");
	EXPECT_EQ(right.popups, 1);
	EXPECT_EQ(right.text, "Right");
	EXPECT_EQ(hello.text, "Hello");
	EXPECT_EQ(detached.popups, 0);
}


TEST(ToolTipTest, AtTheScreensEdgeTheTipGoesToTheOtherSide)
{
	// What is tested: A window filling the bottom right of the 800 × 600
	// screen, (500, 400, 800, 600); the mouse resting at (290, 190) in it,
	// (790, 590) on the screen, over a view with the tooltip "Corner".
	// Success criteria: Right of the mouse the tip does not fit: its
	// content ends 2 pixels inside the screen (Haiku's inclusive right edge
	// 799 less 2: 797, so 798 as a pixel count), the border a pixel
	// further. Below it does not fit either: it goes above, its
	// content ending 20 pixels above the mouse (570), the border a pixel
	// lower.
	// Why it matters: Haiku keeps tooltips on the screen, on the other side
	// of the mouse if needed.
	//
	// Given
	RunningTips tips({500, 400, 800, 600});

	// When
	tips.Move({290, 190});
	tips.Wait(750ms);
	const TipState shown = tips.Ask();

	// Then
	ASSERT_EQ(shown.popups, 1);
	EXPECT_EQ(shown.text, "Corner");
	EXPECT_EQ(shown.frame.x1, 798 + 1);
	EXPECT_EQ(shown.frame.y1, 570 + 1 + 1);
}


} // namespace
