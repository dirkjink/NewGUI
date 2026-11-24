/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/PopUpMenu.h>
#include <ngui/Window.h>

#include "MenuPrivate.h"
#include "support/FakePlatform.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;
using test::FakePlatform;
using test::PixelBackend;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr int32 kItems = 60;


// A menu item that tells whether it is selected.
class SeenItem : public BMenuItem {
public:
	using BMenuItem::BMenuItem;
	using BMenuItem::IsSelected;
};


// A pop-up menu of 60 items "Item 0" to "Item 59" (messages 100 to 159),
// higher than the fake platform's screen of 600 pixels; item \a marked is
// marked.
std::unique_ptr<BPopUpMenu>
LongMenu(int32 marked)
{
	auto menu = std::make_unique<BPopUpMenu>("long", false, false);
	for (int32 i = 0; i < kItems; i++) {
		auto item = std::make_unique<SeenItem>("Item " + std::to_string(i),
			std::make_unique<BMessage>(static_cast<std::uint32_t>(100 + i)));
		item->SetMarked(i == marked);
		menu->AddItem(std::move(item));
	}
	return menu;
}


// A view that opens its long menu, kept open, where the mouse is pressed.
class LongMenuView : public BView {
public:
	explicit LongMenuView(int32 marked)
		:
		BView({0, 0, 400, 300}, "long menu", {}, {}),
		fMenu(LongMenu(marked))
	{
	}

	void MouseDown(gfx::Point where) override
	{
		(void)fMenu->Go(ConvertToScreen(where), {.autoInvoke = true, .keepOpen = true});
	}

	BPopUpMenu* Menu() const
	{
		return fMenu.get();
	}

private:
	std::unique_ptr<BPopUpMenu> fMenu;
};


// A window covered by a LongMenuView. It records the items chosen and
// answers kAsk with the menu's popup: its frame, the scroll value (the
// menu's bounds top), the steps, which scrollers are enabled, the selected
// item and its frame, and the bottom of the last item.
class LongMenuWindow : public BWindow {
public:
	explicit LongMenuWindow(int32 marked)
		:
		BWindow({0, 0, 400, 300}, "long menu")
	{
		fView = static_cast<LongMenuView*>(AddChild(std::make_unique<LongMenuView>(marked)));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what >= 100 && message->what < 100 + kItems) {
			fChosen.push_back(static_cast<std::int32_t>(message->what) - 100);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		BPopUpMenu* menu = fView->Menu();
		(void)reply.AddInt32("popups", CountPopups());
		if (auto* frame = CountPopups() > 0 ? dynamic_cast<MenuFrame*>(PopupAt(0)) : nullptr) {
			(void)reply.AddRect("frame", frame->Frame());
			float small = 0;
			float large = 0;
			frame->GetSteps(&small, &large);
			(void)reply.AddFloat("small", small);
			(void)reply.AddFloat("large", large);
			for (int32 i = 0; i < frame->CountChildren(); i++) {
				if (auto* scroller = dynamic_cast<MenuScroller*>(frame->ChildAt(i))) {
					(void)reply.AddBool(scroller->Frame().y0 < 10 ? "upper" : "lower",
						scroller->IsEnabled());
				}
			}
		}
		(void)reply.AddFloat("value", menu->Bounds().y0);
		(void)reply.AddFloat("visible", menu->Bounds().Height());
		(void)reply.AddFloat("last", menu->ItemAt(kItems - 1)->Frame().y1);
		if (BMenuItem* marked = menu->FindMarked())
			(void)reply.AddFloat("marked", marked->Frame().y0);
		for (int32 i = 0; i < kItems; i++) {
			if (static_cast<SeenItem*>(menu->ItemAt(i))->IsSelected()) {
				(void)reply.AddInt32("selected", i);
				(void)reply.AddRect("selectedFrame", menu->ItemAt(i)->Frame());
			}
		}
		for (std::int32_t chosen : fChosen)
			(void)reply.AddInt32("chosen", chosen);
		(void)message->SendReply(reply);
	}

private:
	LongMenuView* fView = nullptr;
	std::vector<std::int32_t> fChosen;
};


// What a LongMenuWindow answers.
struct ScrollState {
	std::int32_t popups = -1;
	gfx::Rect frame;
	float small = 0;
	float large = 0;
	bool upper = false;
	bool lower = false;
	float value = -1;
	float visible = 0;
	float last = 0;
	float marked = -1;
	std::int32_t selected = -1;
	gfx::Rect selectedFrame;
	std::vector<std::int32_t> chosen;
};


// A running application on the fake platform (a screen of 800 × 600) with
// a LongMenuWindow, the fixed text system and a ManualClock.
class RunningLongMenu {
public:
	explicit RunningLongMenu(int32 marked = -1)
		:
		fApp("application/x-vnd.ngui-menu-scrolling-test")
	{
		Clock::SetDefault(&fClock);
		TextSystem::SetDefault(&fText);
		fApp.SetPulseRate(Clock::Time::zero());
		EXPECT_TRUE(fApp.UsePlatform(std::make_unique<FakePlatform>()).has_value());
		fRunner = std::thread([this] { (void)fApp.Run(); });
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		auto window = std::make_unique<LongMenuWindow>(marked);
		EXPECT_TRUE(window->UseBackend(std::make_unique<PixelBackend>()).has_value());
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		BMessage activated(kWindowActivated);
		(void)activated.AddBool("active", true);
		(void)fWindow.SendMessage(activated);
	}

	~RunningLongMenu()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
		TextSystem::SetDefault(nullptr);
		Clock::SetDefault(nullptr);
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons)
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		(void)fWindow.SendMessage(message);
	}

	void Wheel(float deltaY, BModifierFlags modifiers = {})
	{
		BMessage message(kMouseWheelChanged);
		(void)message.AddFloat("be:wheel_delta_x", 0);
		(void)message.AddFloat("be:wheel_delta_y", deltaY);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		(void)fWindow.SendMessage(message);
	}

	void Key(char byte)
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		(void)fWindow.SendMessage(message);
	}

	ScrollState Ask()
	{
		// Twice: the first answer may come before the effects of the
		// messages the window posted to itself.
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		ScrollState state;
		state.popups = reply.GetInt32("popups", -1);
		state.frame = reply.GetRect("frame", {});
		state.small = reply.GetFloat("small", 0);
		state.large = reply.GetFloat("large", 0);
		state.upper = reply.GetBool("upper", false);
		state.lower = reply.GetBool("lower", false);
		state.value = reply.GetFloat("value", -1);
		state.visible = reply.GetFloat("visible", 0);
		state.last = reply.GetFloat("last", 0);
		state.marked = reply.GetFloat("marked", -1);
		state.selected = reply.GetInt32("selected", -1);
		state.selectedFrame = reply.GetRect("selectedFrame", {});
		for (std::int32_t i = 0; auto chosen = reply.FindInt32("chosen", i); i++)
			state.chosen.push_back(*chosen);
		return state;
	}

	ManualClock& Time()
	{
		return fClock;
	}

private:
	ManualClock fClock;
	test::FixedTextSystem fText;
	BApplication fApp;
	std::thread fRunner;
	BMessenger fWindow;
};


TEST(MenuScrollingTest, AMenuHigherThanTheScreenScrolls)
{
	// What is tested: On a screen of 800 × 600, a pop-up menu of 60 items
	// (higher than the screen), kept open, opened at (100, 100); the mouse
	// moved onto the lower scroller and left there for 5 ms; moved onto the
	// items and left there; the wheel turned down, then up with Shift; Page
	// Down until the end; the mouse on an item and, past the click time, a
	// click.
	// Success criteria: The popup fits the screen (its border one pixel
	// beyond it) and the menu sits between scrollers of 12 pixels; at the
	// top, only the lower scroller is enabled. Over the lower scroller the
	// menu scrolls by a small step (an item's height) at once and again
	// every 5 ms; over the items it stops. The wheel scrolls by a small
	// step, with Shift by a large one, not beyond the top. Page Down
	// scrolls by a large step (the visible height less a small step), at
	// most to the end, where the last item is visible and the lower
	// scroller disabled. The item under the mouse is
	// found in the scrolled menu and chosen by the click.
	// Why it matters: Long menus can be reached in full, as in Haiku.
	//
	// Given
	RunningLongMenu menu;

	// When
	menu.Mouse(kMouseDown, {100, 100}, 1);
	menu.Mouse(kMouseUp, {100, 100}, 0);
	const ScrollState opened = menu.Ask();
	menu.Mouse(kMouseMoved, {150, 595}, 0);
	const ScrollState once = menu.Ask();
	menu.Time().Advance(5ms);
	const ScrollState twice = menu.Ask();
	menu.Mouse(kMouseMoved, {150, 300}, 0);
	menu.Time().Advance(5ms);
	const ScrollState stopped = menu.Ask();
	menu.Wheel(1);
	const ScrollState wheeled = menu.Ask();
	menu.Wheel(-1, BModifier::Shift);
	const ScrollState back = menu.Ask();
	menu.Key(keys::kPageDown);
	const ScrollState paged = menu.Ask();
	for (int i = 0; i < 10; i++)
		menu.Key(keys::kPageDown);
	const ScrollState end = menu.Ask();
	menu.Mouse(kMouseMoved, {150, 300}, 0);
	const ScrollState over = menu.Ask();
	// past the click time, the click chooses (within it, it would be the
	// click that opened the menu)
	menu.Time().Advance(1s);
	menu.Mouse(kMouseDown, {150, 300}, 1);
	menu.Mouse(kMouseUp, {150, 300}, 0);
	const ScrollState chosen = menu.Ask();

	// Then
	EXPECT_EQ(opened.popups, 1);
	EXPECT_EQ(opened.frame.y0, -1);
	EXPECT_EQ(opened.frame.y1, 601);
	EXPECT_EQ(opened.visible, 600 - 2 * kMenuScrollerHeight);
	EXPECT_EQ(opened.value, 0);
	EXPECT_FALSE(opened.upper);
	EXPECT_TRUE(opened.lower);
	EXPECT_GT(opened.small, 0);
	EXPECT_EQ(opened.large, opened.visible - 1 - opened.small);

	EXPECT_EQ(once.value, opened.small);
	EXPECT_TRUE(once.upper);
	EXPECT_EQ(twice.value, 2 * opened.small);
	EXPECT_EQ(stopped.value, twice.value);
	EXPECT_EQ(wheeled.value, 3 * opened.small);
	EXPECT_EQ(back.value, 0);
	EXPECT_FALSE(back.upper);
	EXPECT_EQ(paged.value, std::min(opened.large, end.value));

	EXPECT_FALSE(end.lower);
	EXPECT_GE(end.value + end.visible, end.last);
	EXPECT_LT(end.value + end.visible - opened.small, end.last);

	// the menu's top on the screen is below the border and the upper
	// scroller
	const float local = 300 - (opened.frame.y0 + 1 + kMenuScrollerHeight) + end.value;
	EXPECT_GE(over.selected, 0);
	EXPECT_LE(over.selectedFrame.y0, local);
	EXPECT_GT(over.selectedFrame.y1, local);
	EXPECT_EQ(chosen.popups, 0);
	EXPECT_EQ(chosen.chosen, (std::vector<std::int32_t>{over.selected}));
}


TEST(MenuScrollingTest, KeysAndTheMarkedItemComeIntoView)
{
	// What is tested: On a screen of 800 × 600, the pop-up menu of 60 items
	// with item 20 marked, kept open, opened at (100, 100); then Up pressed
	// until the selection wraps from the first item to the last.
	// Success criteria: The menu opens scrolled to the marked item (its top
	// at the top of the visible part). The item the keys select is
	// scrolled into view: wrapping to the last item scrolls to the end.
	// Why it matters: What the user chooses with the keyboard, or chose
	// before, is visible in a long menu.
	//
	// Given
	RunningLongMenu menu(20);

	// When
	menu.Mouse(kMouseDown, {100, 100}, 1);
	menu.Mouse(kMouseUp, {100, 100}, 0);
	const ScrollState opened = menu.Ask();
	menu.Key(keys::kUpArrow);
	const ScrollState up = menu.Ask();
	for (int32 i = 0; i < up.selected + 1; i++)
		menu.Key(keys::kUpArrow);
	const ScrollState wrapped = menu.Ask();

	// Then
	EXPECT_EQ(opened.popups, 1);
	EXPECT_GT(opened.marked, 0);
	EXPECT_EQ(opened.value, opened.marked);
	EXPECT_GE(up.selected, 0);
	EXPECT_LE(up.selectedFrame.y0, up.value + up.visible);
	EXPECT_EQ(wrapped.selected, kItems - 1);
	EXPECT_FALSE(wrapped.lower);
	EXPECT_LE(wrapped.selectedFrame.y1, wrapped.value + wrapped.visible);
	EXPECT_GE(wrapped.selectedFrame.y0, wrapped.value);
}


} // namespace
