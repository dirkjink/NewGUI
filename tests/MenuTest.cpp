/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Menu.h>

#include <atomic>
#include <cmath>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Button.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuItem.h>
#include <ngui/LayoutUtils.h>
#include <ngui/MenuField.h>
#include <ngui/Message.h>
#include <ngui/PopUpMenu.h>
#include <ngui/SeparatorItem.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


std::unique_ptr<BMenuItem>
Item(std::string_view label, std::uint32_t what, uint32 shortcut = 0,
	BModifierFlags modifiers = {})
{
	return std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(what), shortcut,
		modifiers);
}


TEST(MenuTest, ItemsAreLaidOutAsInHaiku)
{
	// What is tested: The sizes of a column menu with "New" (Command+N),
	// "Open", a separator, a submenu "Recent" and "Quit" (Command+Q); of a
	// row with "File" and "Edit"; and a column menu in a window, laid out
	// by the window's layout pass; with the placeholder font (12 px, 7.2 px
	// per character).
	// Success criteria: The item margins are Haiku's for a label spacing of
	// 6: 14, 2, 20, 0. An item is 12 + 2 = 14 high (15 rows), the separator
	// max(4, 8) + 2 (11 rows), so the column is 71 high. Its width is the
	// widest content plus margins, "Recent" 44 + 34 = 78, plus the Alt
	// symbol (16 + 1) for the Command shortcuts and half an item height (7)
	// for the submenu arrow: 102, 103 pixels. The row puts "File" and
	// "Edit" side by side: 129 × 15. In the window the menu "Copy"/"Cut"
	// is laid out to 64 × 30, its first item (0, 0, 64, 15).
	// Why it matters: Menus size themselves to their items; menu bars and
	// pop-up menus are placed with these sizes.
	//
	// Given
	BMenu menu("File");
	menu.AddItem(Item("New", 1, 'N', BModifier::Command));
	menu.AddItem(Item("Open", 2));
	menu.AddSeparatorItem();
	auto recent = std::make_unique<BMenu>("Recent");
	recent->AddItem(Item("a.txt", 3));
	menu.AddItem(std::move(recent));
	menu.AddItem(Item("Quit", 4, 'Q', BModifier::Command));
	BMenu row("bar", BMenuLayout::InRow);
	row.AddItem(Item("File", 5));
	row.AddItem(Item("Edit", 6));
	BWindow window({0, 0, 300, 300}, "menus");
	auto* edit = static_cast<BMenu*>(window.AddChild(std::make_unique<BMenu>("Edit")));
	edit->AddItem(Item("Cut", 7));
	edit->AddItem(Item("Copy", 8));

	// When
	window.Layout(false);

	// Then
	float left, top, right, bottom;
	menu.GetItemMargins(&left, &top, &right, &bottom);
	EXPECT_EQ((std::vector<float>{left, top, right, bottom}),
		(std::vector<float>{14, 2, 20, 0}));
	EXPECT_EQ(menu.PreferredSize(), (gfx::Size{103, 71}));
	EXPECT_EQ(menu.MinSize(), menu.MaxSize());
	EXPECT_EQ(row.PreferredSize(), (gfx::Size{129, 15}));
	EXPECT_EQ(edit->Frame(), (gfx::Rect{0, 0, 64, 30}));
	EXPECT_EQ(edit->ItemAt(0)->Frame(), (gfx::Rect{0, 0, 64, 15}));
	EXPECT_EQ(edit->ItemAt(1)->Frame(), (gfx::Rect{0, 15, 64, 30}));
	EXPECT_EQ(menu.ItemAt(3)->Label(), "Recent");
	EXPECT_EQ(menu.SubmenuAt(3)->Superitem(), menu.ItemAt(3));
	EXPECT_EQ(menu.SubmenuAt(3)->Supermenu(), &menu);
}


TEST(MenuTest, ItemsCanBeFoundMovedAndRemoved)
{
	// What is tested: FindItem() by label and by command (also in a
	// submenu), IndexOf(), RemoveItem() by item and by index, RemoveItems(),
	// MoveItem(), SwapItems(), SortItems().
	// Success criteria: Items in submenus are found; a removed item is
	// given back without a menu and the others move up; moving, swapping
	// and sorting change the order as asked; RemoveItems() destroys them.
	// Why it matters: Programs build and change their menus at run time
	// (recent files, window lists).
	//
	// Given
	BMenu menu("menu");
	menu.AddItem(Item("Charlie", 3));
	menu.AddItem(Item("Alpha", 1));
	auto sub = std::make_unique<BMenu>("Sub");
	sub->AddItem(Item("Bravo", 2));
	menu.AddItem(std::move(sub));
	menu.AddItem(Item("Delta", 4));

	// When / Then
	EXPECT_EQ(menu.FindItem("Bravo"), menu.SubmenuAt(2)->ItemAt(0));
	EXPECT_EQ(menu.FindItem(std::uint32_t{4}), menu.ItemAt(3));
	EXPECT_EQ(menu.FindItem("Echo"), nullptr);
	EXPECT_EQ(menu.IndexOf(menu.SubmenuAt(2)), 2);

	std::unique_ptr<BMenuItem> alpha = menu.RemoveItem(menu.ItemAt(1));
	ASSERT_NE(alpha, nullptr);
	EXPECT_EQ(alpha->Menu(), nullptr);
	EXPECT_EQ(menu.CountItems(), 3);
	EXPECT_EQ(menu.ItemAt(1)->Label(), "Sub");
	EXPECT_TRUE(menu.AddItem(std::move(alpha), 0));

	EXPECT_TRUE(menu.MoveItem(0, 3));
	EXPECT_EQ(menu.ItemAt(3)->Label(), "Alpha");
	EXPECT_TRUE(menu.SwapItems(0, 3));
	EXPECT_EQ(menu.ItemAt(0)->Label(), "Alpha");
	EXPECT_EQ(menu.ItemAt(3)->Label(), "Charlie");
	menu.SortItems([](const BMenuItem* a, const BMenuItem* b) {
		return a->Label().compare(b->Label());
	});
	std::vector<std::string> labels;
	for (int32 i = 0; i < menu.CountItems(); i++)
		labels.push_back(menu.ItemAt(i)->Label());
	EXPECT_EQ(labels, (std::vector<std::string>{"Alpha", "Charlie", "Delta", "Sub"}));

	std::unique_ptr<BMenuItem> delta = menu.RemoveItem(2);
	EXPECT_EQ(delta->Label(), "Delta");
	EXPECT_TRUE(menu.RemoveItems(0, 10));
	EXPECT_EQ(menu.CountItems(), 0);
	EXPECT_EQ(menu.RemoveItem(0), nullptr);
}


TEST(MenuTest, MarksFollowTheRadioMode)
{
	// What is tested: A submenu "Size" in radio mode with its label taken
	// from the marked item: marking "Small", then "Large"; an item added
	// already marked; a menu not in radio mode with two marked items; a
	// disabled menu.
	// Success criteria: In radio mode marking an item unmarks the others
	// and the superitem shows its label; an item added marked takes the
	// mark. Without radio mode both stay marked. A disabled menu disables
	// its items and its superitem; a separator can never be enabled.
	// Why it matters: Radio menus are Haiku's choice of one value (font
	// size, sort order); the label of a menu field follows them.
	//
	// Given
	BMenu menu("menu");
	auto size = std::make_unique<BMenu>("Size");
	size->SetLabelFromMarked(true);
	size->AddItem(Item("Small", 1));
	size->AddItem(Item("Large", 2));
	BMenu* sizes = size.get();
	menu.AddItem(std::move(size));
	BMenuItem* superitem = menu.ItemAt(0);
	BMenu checks("checks");
	checks.AddItem(Item("Bold", 3));
	checks.AddItem(Item("Italic", 4));

	// When / Then
	EXPECT_TRUE(sizes->IsRadioMode());
	EXPECT_EQ(superitem->Label(), "Size");
	sizes->ItemAt(0)->SetMarked(true);
	EXPECT_EQ(superitem->Label(), "Small");
	sizes->ItemAt(1)->SetMarked(true);
	EXPECT_FALSE(sizes->ItemAt(0)->IsMarked());
	EXPECT_EQ(sizes->FindMarkedIndex(), 1);
	EXPECT_EQ(superitem->Label(), "Large");
	auto huge = Item("Huge", 5);
	huge->SetMarked(true);
	sizes->AddItem(std::move(huge));
	EXPECT_EQ(sizes->FindMarked()->Label(), "Huge");
	EXPECT_FALSE(sizes->ItemAt(1)->IsMarked());

	checks.ItemAt(0)->SetMarked(true);
	checks.ItemAt(1)->SetMarked(true);
	EXPECT_TRUE(checks.ItemAt(0)->IsMarked());
	EXPECT_TRUE(checks.ItemAt(1)->IsMarked());

	sizes->SetEnabled(false);
	EXPECT_FALSE(sizes->ItemAt(0)->IsEnabled());
	EXPECT_FALSE(superitem->IsEnabled());
	EXPECT_TRUE(menu.IsEnabled());
	BSeparatorItem separator;
	separator.SetEnabled(true);
	EXPECT_FALSE(separator.IsEnabled());
}


TEST(MenuTest, InvokingSendsTheMessageToTheWindow)
{
	// What is tested: A menu in a window whose items have no target, and
	// an item targeting a running looper: Invoke() of an enabled item,
	// of an item in a radio menu, of a disabled item and of one without a
	// message.
	// Success criteria: Attached, the items target their window. The
	// looper gets the message with "index" and "source"; in radio mode the
	// invoked item becomes marked. A disabled item and one without a
	// message fail and send nothing; as in Haiku, the one without a message
	// is marked before it fails.
	// Why it matters: Choosing a menu item is how most commands reach a
	// program.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	std::mutex lock;
	std::int32_t index = -1;
	const BMenuItem* source = nullptr;
	auto looper = std::make_unique<RecordingLooper>(record);
	looper->action = [&](BMessage& message) {
		std::scoped_lock guard(lock);
		index = message.GetInt32("index", -1);
		source = static_cast<const BMenuItem*>(*message.FindPointer("source"));
	};
	const BMessenger target = *BLooper::Run(std::move(looper));
	BWindow window({0, 0, 300, 300}, "menus");
	auto* menu = static_cast<BMenu*>(window.AddChild(std::make_unique<BMenu>("menu")));
	menu->SetRadioMode(true);
	menu->AddItem(Item("First", 1));
	menu->AddItem(Item("Second", 2));
	menu->AddItem(std::make_unique<BMenuItem>("Empty", nullptr));
	BMenuItem* second = menu->ItemAt(1);
	second->SetTarget(target);

	// When
	const bool targetsWindow = menu->ItemAt(0)->Messenger() == BMessenger(&window);
	const bool sent = second->Invoke().has_value();
	const bool secondMarked = second->IsMarked();
	menu->ItemAt(0)->SetEnabled(false);
	const bool disabledSent = menu->ItemAt(0)->Invoke().has_value();
	const bool emptySent = menu->ItemAt(2)->Invoke().has_value();
	ASSERT_TRUE(record->WaitFor(1));
	ASSERT_TRUE(target.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(target.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_TRUE(targetsWindow);
	EXPECT_TRUE(sent);
	EXPECT_FALSE(disabledSent);
	EXPECT_FALSE(emptySent);
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"looper:2"}));
	EXPECT_EQ(index, 1);
	EXPECT_EQ(source, second);
	EXPECT_TRUE(secondMarked);
	EXPECT_TRUE(menu->ItemAt(2)->IsMarked());
}


// Whether \a item is selected: IsSelected() is protected, as in Haiku.
struct SelectedItem : BMenuItem {
	static bool Of(const BMenuItem* item)
	{
		return (item->*(&SelectedItem::IsSelected))();
	}
};


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kRemoveShortcut = FourCharCode("RMSC");


// A window with a menu bar "File" (New, Open, separator, Recent > a.txt,
// b.txt, Quit) and "Edit" (Cut) at the top, triggers enabled; the items
// send 1 .. 6 to it. Below the bar a button "button" has the focus. With
// \a shortcuts, New has Command+N, Open Command+O but is disabled, and the
// window has Command+K (7) and D without Command (8). It records what it
// gets and answers kAsk with the open popups, whether the bar tracks, the
// frame of the last popup, its menu hooks, the selected items from the bar
// down and the focus view's name. kRemoveShortcut removes Command+K.
class MenuWindow : public BWindow {
public:
	explicit MenuWindow(bool shortcuts)
		:
		BWindow({0, 0, 400, 300}, "menus")
	{
		auto bar = std::make_unique<BMenuBar>(gfx::Rect{0, 0, 400, 20}, "bar");
		auto file = std::make_unique<BMenu>("File");
		file->AddItem(shortcuts ? Item("New", 1, 'N', BModifier::Command) : Item("New", 1));
		file->AddItem(shortcuts ? Item("Open", 2, 'O', BModifier::Command) : Item("Open", 2));
		if (shortcuts)
			file->ItemAt(1)->SetEnabled(false);
		file->AddSeparatorItem();
		auto recent = std::make_unique<BMenu>("Recent");
		recent->AddItem(Item("a.txt", 3));
		recent->AddItem(Item("b.txt", 4));
		recent->SetTriggersEnabled(true);
		file->AddItem(std::move(recent));
		file->AddItem(Item("Quit", 5));
		file->SetTriggersEnabled(true);
		auto edit = std::make_unique<BMenu>("Edit");
		edit->AddItem(Item("Cut", 6));
		bar->AddItem(std::move(file));
		bar->AddItem(std::move(edit));
		fBar = static_cast<BMenuBar*>(AddChild(std::move(bar)));
		AddChild(std::make_unique<BButton>(gfx::Rect{10, 100, 90, 130}, "button", "Button",
			nullptr))->MakeFocus(true);
		if (shortcuts) {
			AddShortcut('K', BModifier::Command, std::make_unique<BMessage>(7));
			AddShortcut('D', BModifier::NoCommand, std::make_unique<BMessage>(8));
		}
	}

	void MenusBeginning() override
	{
		fBeginnings++;
	}

	void MenusEnded() override
	{
		fEnds++;
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what >= 1 && message->what <= 8) {
			fChosen.push_back(static_cast<std::int32_t>(message->what));
			return;
		}
		if (message->what == kRemoveShortcut) {
			RemoveShortcut('k', BModifier::Command);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		(void)reply.AddInt32("popups", CountPopups());
		(void)reply.AddBool("tracking", fBar->IsTracking());
		if (CountPopups() > 0)
			(void)reply.AddRect("last", PopupAt(CountPopups() - 1)->Frame());
		for (std::int32_t chosen : fChosen)
			(void)reply.AddInt32("chosen", chosen);
		(void)reply.AddInt32("beginnings", fBeginnings);
		(void)reply.AddInt32("ends", fEnds);
		// the selected items, from the bar down through the open menus
		for (BMenu* menu = fBar; menu != nullptr;) {
			BMenu* next = nullptr;
			for (std::int32_t i = 0; i < menu->CountItems(); i++) {
				BMenuItem* item = menu->ItemAt(i);
				if (!SelectedItem::Of(item))
					continue;
				(void)reply.AddString("selected", item->Label());
				if (item->Submenu() != nullptr && item->Submenu()->Window() != nullptr)
					next = item->Submenu();
			}
			menu = next;
		}
		(void)reply.AddString("focus", CurrentFocus() != nullptr ? CurrentFocus()->Name() : "");
		(void)reply.AddBool("hasK", HasShortcut('K', BModifier::Command));
		(void)message->SendReply(reply);
	}

private:
	BMenuBar* fBar = nullptr;
	std::vector<std::int32_t> fChosen;
	int fBeginnings = 0;
	int fEnds = 0;
};


// The state a MenuWindow answers.
struct MenuState {
	std::int32_t popups = -1;
	bool tracking = false;
	gfx::Rect last;
	std::vector<std::int32_t> chosen;
	std::int32_t beginnings = 0;
	std::int32_t ends = 0;
	std::vector<std::string> selected;
	std::string focus;
	bool hasK = false;
};


class RunningMenus {
public:
	explicit RunningMenus(bool shortcuts = false)
		:
		fWindow(*BLooper::Run(std::make_unique<MenuWindow>(shortcuts)))
	{
	}

	~RunningMenus()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons)
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Click(gfx::Point where)
	{
		Mouse(kMouseDown, where, 1);
		Mouse(kMouseUp, where, 0);
	}

	void Key(char byte, BModifierFlags modifiers = {})
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(BMessage(what)).has_value());
	}

	void Deactivate()
	{
		BMessage message(kWindowActivated);
		(void)message.AddBool("active", false);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	MenuState Ask()
	{
		// Twice: messages the window sent itself while handling the events
		// (a chosen item's) are in its queue before the second question.
		BMessage reply;
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		MenuState state;
		state.popups = reply.GetInt32("popups", -1);
		state.tracking = reply.GetBool("tracking", false);
		state.last = reply.GetRect("last", {});
		for (std::int32_t i = 0; auto chosen = reply.FindInt32("chosen", i); i++)
			state.chosen.push_back(*chosen);
		state.beginnings = reply.GetInt32("beginnings", 0);
		state.ends = reply.GetInt32("ends", 0);
		for (std::int32_t i = 0; auto selected = reply.FindString("selected", i); i++)
			state.selected.emplace_back(*selected);
		state.focus = reply.GetString("focus", "?");
		state.hasK = reply.GetBool("hasK", false);
		return state;
	}

private:
	BMessenger fWindow;
};


TEST(MenuBarTest, PressDragAndReleaseChooses)
{
	// What is tested: The menu bar of a running window (placeholder font):
	// the mouse pressed on "File", dragged onto "Open" and released there.
	// Success criteria: The bar is 17 high and "File" (0, 0, 46, 17). The
	// press opens the File menu in a popup below its title, with Haiku's
	// one pixel border: at (0, 16), 88 × 73 (the menu 86 × 71). Releasing
	// on "Open" sends its message (2) to the window and closes the popup;
	// MenusBeginning() and MenusEnded() are called once each.
	// Why it matters: This is the most common way to use a menu.
	//
	// Given
	RunningMenus menus;

	// When
	menus.Mouse(kMouseDown, {10, 8}, 1);
	const MenuState pressed = menus.Ask();
	menus.Mouse(kMouseMoved, {20, 40}, 1);
	menus.Mouse(kMouseUp, {20, 40}, 0);
	const MenuState released = menus.Ask();

	// Then
	EXPECT_EQ(pressed.popups, 1);
	EXPECT_TRUE(pressed.tracking);
	EXPECT_EQ(pressed.last, (gfx::Rect{0, 16, 88, 89}));
	EXPECT_EQ(released.popups, 0);
	EXPECT_FALSE(released.tracking);
	EXPECT_EQ(released.chosen, (std::vector<std::int32_t>{2}));
	EXPECT_EQ(released.beginnings, 1);
	EXPECT_EQ(released.ends, 1);
}


TEST(MenuBarTest, StickyModeOpensSubmenusAndClosesOutside)
{
	// What is tested: A click (press and release) on "File", the mouse
	// moved onto "Recent", then a click on "b.txt" of its submenu; again a
	// click on "File" and one outside the menus; again a click on "File" and
	// Escape; again and the window deactivated; a click on "File" while its
	// menu is open.
	// Success criteria: A click on a title keeps its menu open (sticky
	// mode). Over "Recent" its submenu opens at once beside it: at (86, 58),
	// 73 × 32. A click on "b.txt" chooses it (4). A click outside the menus,
	// Escape, the window becoming inactive and a click on the open menu's
	// title close the menus without choosing anything.
	// Why it matters: Click-click navigation and every way to cancel a menu
	// must work as in Haiku.
	//
	// Given
	RunningMenus menus;

	// When / Then
	menus.Click({10, 8});
	EXPECT_TRUE(menus.Ask().tracking);
	EXPECT_EQ(menus.Ask().popups, 1);
	menus.Mouse(kMouseMoved, {20, 64}, 0);
	const MenuState submenu = menus.Ask();
	EXPECT_EQ(submenu.popups, 2);
	EXPECT_EQ(submenu.last, (gfx::Rect{86, 58, 159, 90}));
	menus.Mouse(kMouseMoved, {100, 80}, 0);
	menus.Click({100, 80});
	MenuState chosen = menus.Ask();
	EXPECT_FALSE(chosen.tracking);
	EXPECT_EQ(chosen.popups, 0);
	EXPECT_EQ(chosen.chosen, (std::vector<std::int32_t>{4}));

	menus.Click({10, 8});
	menus.Click({390, 290});
	EXPECT_FALSE(menus.Ask().tracking);

	menus.Click({10, 8});
	EXPECT_TRUE(menus.Ask().tracking);
	menus.Key(keys::kEscape);
	EXPECT_FALSE(menus.Ask().tracking);

	menus.Click({10, 8});
	menus.Deactivate();
	EXPECT_FALSE(menus.Ask().tracking);

	menus.Click({10, 8});
	menus.Click({10, 8});
	const MenuState toggled = menus.Ask();
	EXPECT_FALSE(toggled.tracking);
	EXPECT_EQ(toggled.popups, 0);
	EXPECT_EQ(toggled.chosen, (std::vector<std::int32_t>{4}));
	EXPECT_EQ(toggled.beginnings, 5);
	EXPECT_EQ(toggled.ends, 5);
}


TEST(MenuBarTest, TheNavigationAreaDelaysSwitching)
{
	// What is tested: With a ManualClock: the File menu open (sticky),
	// "Recent" selected with its submenu, then the mouse moved down onto
	// "Quit" in the direction of the submenu, (60, 74); the clock advanced
	// by 0.5 s and then past one second.
	// Success criteria: In Haiku's navigation area (the triangle towards
	// the submenu) "Recent" stays selected and its submenu open, also after
	// 0.5 s; after Haiku's timeout of one second "Quit" is selected and the
	// submenu closes, though the mouse did not move.
	// Why it matters: The navigation area lets the user reach a submenu
	// diagonally without it closing on the way.
	//
	// Given
	ManualClock clock;
	Clock::SetDefault(&clock);
	{
		RunningMenus menus;
		menus.Click({10, 8});
		menus.Mouse(kMouseMoved, {20, 64}, 0);

		// When
		menus.Mouse(kMouseMoved, {60, 74}, 0);
		const MenuState moved = menus.Ask();
		clock.Advance(500ms);
		const MenuState halfway = menus.Ask();
		clock.Advance(600ms);
		const MenuState later = menus.Ask();

		// Then
		EXPECT_EQ(moved.popups, 2);
		EXPECT_EQ(halfway.popups, 2);
		EXPECT_EQ(later.popups, 1);
		EXPECT_TRUE(later.tracking);
	}
	Clock::SetDefault(nullptr);
}


TEST(MenuBarTest, TheKeyboardNavigatesTheMenus)
{
	// What is tested: Command+Escape, then Down three times, Right, Down,
	// Left, Left, Down and Enter; the button below the bar has the focus.
	// Success criteria: Command+Escape opens the File menu (sticky, nothing
	// selected in it) and the bar takes the focus. Down moves through the
	// enabled items and skips the separator; "Recent" is selected without
	// opening. Right opens its submenu with "a.txt" selected; Left closes it
	// again, "Recent" stays selected. Left in the File menu goes to the
	// previous menu of the bar, wrapping around to "Edit"; Down selects its
	// only item. Enter chooses it (6), closes the menus and gives the focus
	// back to the button.
	// Why it matters: Menus must be usable without a mouse, as in Haiku.
	//
	// Given
	RunningMenus menus;
	const std::string before = menus.Ask().focus;

	// When
	menus.Key(keys::kEscape, BModifier::Command);
	const MenuState opened = menus.Ask();
	menus.Key(keys::kDownArrow);
	menus.Key(keys::kDownArrow);
	menus.Key(keys::kDownArrow);
	const MenuState recent = menus.Ask();
	menus.Key(keys::kRightArrow);
	const MenuState submenu = menus.Ask();
	menus.Key(keys::kDownArrow);
	menus.Key(keys::kLeftArrow);
	const MenuState left = menus.Ask();
	menus.Key(keys::kLeftArrow);
	menus.Key(keys::kDownArrow);
	const MenuState edit = menus.Ask();
	menus.Key(keys::kEnter);
	const MenuState chosen = menus.Ask();

	// Then
	EXPECT_EQ(before, "button");
	EXPECT_TRUE(opened.tracking);
	EXPECT_EQ(opened.popups, 1);
	EXPECT_EQ(opened.selected, (std::vector<std::string>{"File"}));
	EXPECT_EQ(opened.focus, "bar");
	EXPECT_EQ(recent.popups, 1);
	EXPECT_EQ(recent.selected, (std::vector<std::string>{"File", "Recent"}));
	EXPECT_EQ(submenu.popups, 2);
	EXPECT_EQ(submenu.selected, (std::vector<std::string>{"File", "Recent", "a.txt"}));
	EXPECT_EQ(left.popups, 1);
	EXPECT_EQ(left.selected, (std::vector<std::string>{"File", "Recent"}));
	EXPECT_EQ(edit.popups, 1);
	EXPECT_EQ(edit.selected, (std::vector<std::string>{"Edit", "Cut"}));
	EXPECT_FALSE(chosen.tracking);
	EXPECT_EQ(chosen.popups, 0);
	EXPECT_EQ(chosen.chosen, (std::vector<std::int32_t>{6}));
	EXPECT_EQ(chosen.focus, "button");
	EXPECT_EQ(chosen.beginnings, 1);
	EXPECT_EQ(chosen.ends, 1);
}


TEST(MenuBarTest, TriggersAndShortcutsChoose)
{
	// What is tested: Items with shortcuts (New: Command+N; Open:
	// Command+O, disabled) and window shortcuts (Command+K, D without
	// Command); the keys typed with the menus closed: Command+n,
	// Command+Shift+n, Command+o, Command+k, d, Command+d; then Command+K
	// removed and typed again; then Command+Escape and the triggers "r" and
	// "b".
	// Success criteria: Command+n chooses New (1), in either case of the
	// letter; with Shift it is another shortcut, which does not exist. The
	// disabled Open is not chosen. Command+k sends 7 to the window, d
	// without Command 8; Command+d is not that shortcut. A removed shortcut
	// is gone. MenusBeginning() and MenusEnded() come with each key typed
	// with Command, as in Haiku. The trigger "r" opens the Recent submenu
	// with "a.txt" selected; "b" chooses b.txt (4).
	// Why it matters: Shortcuts and triggers are the fast way to the
	// commands of an application.
	//
	// Given
	RunningMenus menus(true);

	// When
	menus.Key('n', BModifier::Command);
	menus.Key('n', BModifier::Command | BModifier::Shift);
	menus.Key('o', BModifier::Command);
	menus.Key('k', BModifier::Command);
	menus.Key('d');
	menus.Key('d', BModifier::Command);
	const MenuState shortcuts = menus.Ask();
	menus.Send(kRemoveShortcut);
	menus.Key('k', BModifier::Command);
	const MenuState removed = menus.Ask();
	menus.Key(keys::kEscape, BModifier::Command);
	menus.Key('r');
	const MenuState triggered = menus.Ask();
	menus.Key('b');
	const MenuState chosen = menus.Ask();

	// Then
	EXPECT_EQ(shortcuts.chosen, (std::vector<std::int32_t>{1, 7, 8}));
	EXPECT_FALSE(shortcuts.tracking);
	EXPECT_EQ(shortcuts.beginnings, 5);
	EXPECT_EQ(shortcuts.ends, 5);
	EXPECT_FALSE(removed.hasK);
	EXPECT_EQ(removed.chosen, (std::vector<std::int32_t>{1, 7, 8}));
	EXPECT_EQ(triggered.popups, 2);
	EXPECT_EQ(triggered.selected, (std::vector<std::string>{"File", "Recent", "a.txt"}));
	EXPECT_FALSE(chosen.tracking);
	EXPECT_EQ(chosen.chosen, (std::vector<std::int32_t>{1, 7, 8, 4}));
}


TEST(MenuBarTest, ItemShortcutsLiveWithTheirItems)
{
	// What is tested: A menu bar with an item "Cut" (X without Command) and
	// "Paste" (Command+P) in a window; then Cut's shortcut changed to Y,
	// Paste removed from its menu, the bar removed from the window.
	// Success criteria: Attaching installs both shortcuts in the window
	// (Paste's replaces nothing else); changing a shortcut moves it; a
	// removed item and a detached bar take theirs along, and the window no
	// longer has a key menu bar.
	// Why it matters: A shortcut left behind would invoke an item that is
	// no longer there.
	//
	// Given
	BWindow window({0, 0, 200, 100}, "shortcuts");
	auto bar = std::make_unique<BMenuBar>(gfx::Rect{0, 0, 200, 20}, "bar");
	auto edit = std::make_unique<BMenu>("Edit");
	BMenu* menu = edit.get();
	(void)menu->AddItem(Item("Cut", 1, 'X', BModifier::NoCommand));
	(void)menu->AddItem(Item("Paste", 2, 'P', BModifier::Command));
	BMenuItem* cut = menu->ItemAt(0);
	(void)bar->AddItem(std::move(edit));
	BView* added = window.AddChild(std::move(bar));

	// When
	const bool attachedX = window.HasShortcut('X', BModifier::NoCommand);
	const bool attachedP = window.HasShortcut('p', BModifier::Command);
	const bool keyMenuBar = window.KeyMenuBar() == added;
	cut->SetShortcut('Y', BModifier::NoCommand);
	const bool movedX = window.HasShortcut('X', BModifier::NoCommand);
	const bool movedY = window.HasShortcut('Y', BModifier::NoCommand);
	std::unique_ptr<BMenuItem> paste = menu->RemoveItem(1);
	const bool removedP = window.HasShortcut('P', BModifier::Command);
	std::unique_ptr<BView> detached = window.RemoveChild(added);
	const bool detachedY = window.HasShortcut('Y', BModifier::NoCommand);

	// Then
	EXPECT_TRUE(attachedX);
	EXPECT_TRUE(attachedP);
	EXPECT_TRUE(keyMenuBar);
	EXPECT_FALSE(movedX);
	EXPECT_TRUE(movedY);
	EXPECT_NE(paste, nullptr);
	EXPECT_FALSE(removedP);
	EXPECT_NE(detached, nullptr);
	EXPECT_FALSE(detachedY);
	EXPECT_EQ(window.KeyMenuBar(), nullptr);
	EXPECT_TRUE(window.HasShortcut('W', BModifier::Command));
}


constexpr std::uint32_t kPopUpMode = FourCharCode("PMOD");


// Pop-up menus destroyed so far.
std::atomic<int> sMenusDestroyed = 0;


// A pop-up menu that counts its destruction.
class CountingMenu : public BPopUpMenu {
public:
	CountingMenu()
		:
		BPopUpMenu("context", false, false)
	{
		AddItem(Item("One", 11));
		AddItem(Item("Two", 12));
		AddSeparatorItem();
		auto more = std::make_unique<BMenu>("More");
		more->AddItem(Item("Three", 13));
		AddItem(std::move(more));
		AddItem(Item("Four", 14));
	}

	~CountingMenu() override
	{
		sMenusDestroyed++;
	}
};


// A view that opens a pop-up menu where the mouse is pressed: its own
// (mode 0: auto invoke; mode 1: kept open) or a new one it hands over (mode
// 2, without auto invoke). It records the labels the menus report done
// ("-" for none).
class ContextView : public BView {
public:
	ContextView()
		:
		BView({0, 0, 400, 300}, "context", {}, {}),
		fMenu(std::make_unique<CountingMenu>())
	{
	}

	void MouseDown(gfx::Point where) override
	{
		const gfx::Point screen = ConvertToScreen(where);
		auto done = [this](BMenuItem* item) {
			fDone.push_back(item != nullptr ? item->Label() : "-");
		};
		if (mode == 2) {
			(void)BPopUpMenu::Go(std::make_unique<CountingMenu>(), screen, {}, done);
			return;
		}
		(void)fMenu->Go(screen, {.autoInvoke = true, .keepOpen = mode == 1}, done);
	}

	BPopUpMenu* Menu() const
	{
		return fMenu.get();
	}

	const std::vector<std::string>& Done() const
	{
		return fDone;
	}

	int mode = 0;

private:
	std::unique_ptr<CountingMenu> fMenu;
	std::vector<std::string> fDone;
};


// A window covered by a ContextView. It records the messages 11 .. 14 and
// answers kAsk with the open popups, whether its menu tracks, the frame of
// the last popup, the chosen messages and the labels done.
class PopUpWindow : public BWindow {
public:
	PopUpWindow()
		:
		BWindow({0, 0, 400, 300}, "pop-up")
	{
		fView = static_cast<ContextView*>(AddChild(std::make_unique<ContextView>()));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what >= 11 && message->what <= 14) {
			fChosen.push_back(static_cast<std::int32_t>(message->what));
			return;
		}
		if (message->what == kPopUpMode) {
			fView->mode = message->GetInt32("mode", 0);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		(void)reply.AddInt32("popups", CountPopups());
		(void)reply.AddBool("tracking", fView->Menu()->IsTracking());
		if (CountPopups() > 0)
			(void)reply.AddRect("last", PopupAt(CountPopups() - 1)->Frame());
		for (std::int32_t chosen : fChosen)
			(void)reply.AddInt32("chosen", chosen);
		for (const std::string& done : fView->Done())
			(void)reply.AddString("done", done);
		(void)message->SendReply(reply);
	}

private:
	ContextView* fView = nullptr;
	std::vector<std::int32_t> fChosen;
};


// The state a PopUpWindow answers.
struct PopUpState {
	std::int32_t popups = -1;
	bool tracking = false;
	gfx::Rect last;
	std::vector<std::int32_t> chosen;
	std::vector<std::string> done;
};


class RunningPopUps {
public:
	RunningPopUps()
		:
		fWindow(*BLooper::Run(std::make_unique<PopUpWindow>()))
	{
	}

	~RunningPopUps()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons)
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Click(gfx::Point where)
	{
		Mouse(kMouseDown, where, 1);
		Mouse(kMouseUp, where, 0);
	}

	void Key(char byte)
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Mode(std::int32_t mode)
	{
		BMessage message(kPopUpMode);
		(void)message.AddInt32("mode", mode);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	PopUpState Ask()
	{
		// Twice, as RunningMenus::Ask().
		BMessage reply;
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		PopUpState state;
		state.popups = reply.GetInt32("popups", -1);
		state.tracking = reply.GetBool("tracking", false);
		state.last = reply.GetRect("last", {});
		for (std::int32_t i = 0; auto chosen = reply.FindInt32("chosen", i); i++)
			state.chosen.push_back(*chosen);
		for (std::int32_t i = 0; auto done = reply.FindString("done", i); i++)
			state.done.emplace_back(*done);
		return state;
	}

private:
	BMessenger fWindow;
};


TEST(PopUpMenuTest, GoOpensWhereTheMouseIsAndChooses)
{
	// What is tested: With a ManualClock: the mouse pressed at (100, 100),
	// the clock advanced past the click time, the mouse dragged onto "Two"
	// and released; then a quick click at (100, 100) and a click outside the
	// menu; then the menu opened kept open and chosen with Down and Enter.
	// Success criteria: Go() opens the menu with its left top corner at the
	// point (its popup, with the border, at (99, 99)) and returns at once.
	// Released on "Two" after the click time, "Two" is invoked (12) and
	// reported done. Released within the click time the menu stays open;
	// a click outside closes it with nothing chosen. Kept open, the item
	// under the mouse ("One") is selected; Down and Enter choose the next,
	// "Two".
	// Why it matters: Context menus open where the user clicks and work
	// with the mouse and the keyboard, as in Haiku.
	//
	// Given
	ManualClock clock;
	Clock::SetDefault(&clock);
	{
		RunningPopUps popUps;

		// When
		popUps.Mouse(kMouseDown, {100, 100}, 1);
		const PopUpState opened = popUps.Ask();
		clock.Advance(1s);
		popUps.Mouse(kMouseMoved, {120, 123}, 1);
		popUps.Mouse(kMouseUp, {120, 123}, 0);
		const PopUpState dragged = popUps.Ask();
		popUps.Click({100, 100});
		const PopUpState clicked = popUps.Ask();
		popUps.Click({390, 290});
		const PopUpState outside = popUps.Ask();
		popUps.Mode(1);
		popUps.Click({100, 100});
		popUps.Key(keys::kDownArrow);
		popUps.Key(keys::kEnter);
		const PopUpState keys = popUps.Ask();

		// Then
		EXPECT_EQ(opened.popups, 1);
		EXPECT_TRUE(opened.tracking);
		EXPECT_EQ(opened.last.x0, 99);
		EXPECT_EQ(opened.last.y0, 99);
		EXPECT_EQ(dragged.popups, 0);
		EXPECT_FALSE(dragged.tracking);
		EXPECT_EQ(dragged.chosen, (std::vector<std::int32_t>{12}));
		EXPECT_EQ(dragged.done, (std::vector<std::string>{"Two"}));
		EXPECT_TRUE(clicked.tracking);
		EXPECT_EQ(clicked.popups, 1);
		EXPECT_FALSE(outside.tracking);
		EXPECT_EQ(outside.popups, 0);
		EXPECT_EQ(outside.done, (std::vector<std::string>{"Two", "-"}));
		EXPECT_FALSE(keys.tracking);
		EXPECT_EQ(keys.chosen, (std::vector<std::int32_t>{12, 12}));
		EXPECT_EQ(keys.done, (std::vector<std::string>{"Two", "-", "Two"}));
	}
	Clock::SetDefault(nullptr);
}


TEST(PopUpMenuTest, AHandedOverMenuDestroysItself)
{
	// What is tested: Menus handed over to Go(): one opened with a quick
	// click and closed by a click on "Four", one opened through the
	// submenu "More" and left open when the window quits; Go() of a menu
	// outside any window's message.
	// Success criteria: The chosen "Four" is reported done but not
	// invoked (no auto invoke); its menu is destroyed afterwards. The menu
	// left open is closed and destroyed with the window (three menus in
	// all, with the view's own). Go() outside a window's message fails and
	// opens nothing.
	// Why it matters: A context menu made on the fly must not outlive its
	// use, nor leave a popup behind (Haiku's SetAsyncAutoDestruct()).
	//
	// Given
	const int destroyedBefore = sMenusDestroyed;
	CountingMenu standalone;
	PopUpState chosen;
	PopUpState submenu;

	// When
	const bool outsideFails = !standalone.Go({10, 10}).has_value();
	{
		RunningPopUps popUps;
		popUps.Mode(2);
		popUps.Click({100, 100});
		popUps.Click({120, 168});
		chosen = popUps.Ask();
		popUps.Click({100, 100});
		popUps.Mouse(kMouseMoved, {120, 147}, 0);
		submenu = popUps.Ask();
	}
	const int destroyed = sMenusDestroyed - destroyedBefore;

	// Then
	EXPECT_TRUE(outsideFails);
	EXPECT_FALSE(standalone.IsTracking());
	EXPECT_EQ(chosen.popups, 0);
	EXPECT_TRUE(chosen.chosen.empty());
	EXPECT_EQ(chosen.done, (std::vector<std::string>{"Four"}));
	EXPECT_EQ(submenu.popups, 2);
	EXPECT_EQ(destroyed, 3);
}


// A pop-up menu Small, Medium, Large (21 .. 23), Medium marked.
std::unique_ptr<BPopUpMenu>
SizeMenu()
{
	auto menu = std::make_unique<BPopUpMenu>("size");
	menu->AddItem(Item("Small", 21));
	menu->AddItem(Item("Medium", 22));
	menu->AddItem(Item("Large", 23));
	menu->ItemAt(1)->SetMarked(true);
	return menu;
}


TEST(MenuFieldTest, TheBarShowsTheMarkedItemBesideTheLabel)
{
	// What is tested: A menu field for a layout, "Size:" with a pop-up menu
	// whose "Medium" is marked, laid out at its minimum size; one at a
	// frame of 200 × 24 with a fixed-size bar; one with a plain menu (not
	// radio) whose first item is disabled and second a separator; the
	// first field disabled.
	// Success criteria: The bar's item shows the marked label, or the first
	// enabled item that is not a separator. The bar begins two pixels right
	// of the divider, which is the label width plus the label spacing; the
	// field is two pixels larger than the bar on each side and as wide as
	// wanted. At a frame the divider is half the width (Haiku's 99) and the
	// fixed bar reaches to two pixels before the right edge. The field's
	// bar does not become the window's key menu bar. Disabling the field
	// disables its bar.
	// Why it matters: The menu field is Haiku's choice control; its
	// geometry must match Haiku's for the look to fit.
	//
	// Given
	BWindow window({0, 0, 300, 200}, "fields");
	auto* field = static_cast<BMenuField*>(window.AddChild(
		std::make_unique<BMenuField>("size", "Size:", SizeMenu())));
	auto* framed = static_cast<BMenuField*>(window.AddChild(std::make_unique<BMenuField>(
		gfx::Rect{0, 100, 200, 124}, "framed", "Label", SizeMenu(), true)));
	auto plain = std::make_unique<BMenu>("plain");
	plain->AddItem(Item("Off", 31));
	plain->ItemAt(0)->SetEnabled(false);
	plain->AddSeparatorItem();
	plain->AddItem(Item("On", 32));
	auto* first = static_cast<BMenuField*>(window.AddChild(
		std::make_unique<BMenuField>("plain", "Plain", std::move(plain))));

	// When
	const gfx::Size min = field->MinSize();
	field->ResizeTo(min.w, min.h);
	field->Layout(true);
	const gfx::Rect bar = field->MenuBar()->Frame();
	const gfx::Size barMin = field->MenuBar()->MinSize();
	const float divider = std::ceil(TextSystem::Default().StringWidth("Size:", FontRole::Plain))
		+ field->Look().DefaultLabelSpacing(TextSystem::Default());
	field->SetEnabled(false);

	// Then
	EXPECT_EQ(field->MenuItem()->Label(), "Medium");
	EXPECT_EQ(first->MenuItem()->Label(), "On");
	EXPECT_EQ(window.KeyMenuBar(), nullptr);
	EXPECT_EQ(field->Divider(), divider);
	EXPECT_EQ(bar.x0, divider + 2);
	EXPECT_EQ(bar.y0, 2);
	EXPECT_EQ(min.w, barMin.w + divider + 4);
	EXPECT_EQ(min.h, barMin.h + 4);
	EXPECT_EQ(field->MaxSize().w, kSizeUnlimited);
	EXPECT_EQ(framed->Divider(), 99);
	EXPECT_EQ(framed->MenuBar()->Frame().x0, 101);
	EXPECT_EQ(framed->MenuBar()->Frame().x1, 198);
	EXPECT_FALSE(field->IsEnabled());
	EXPECT_FALSE(field->MenuBar()->IsEnabled());
}


constexpr std::uint32_t kDisableField = FourCharCode("DSFD");


// A window with a menu field "Size:" at (10, 100), 200 wide, a fixed bar,
// with SizeMenu(); the field has the focus. It records the messages
// 21 .. 23 and answers kAsk with the field's label, whether it tracks, the
// open popups and the last one's frame, the bar item's frame and the menu
// items' frames on the screen while open, and the focus view's name.
class FieldWindow : public BWindow {
public:
	FieldWindow()
		:
		BWindow({0, 0, 400, 300}, "field")
	{
		fField = static_cast<BMenuField*>(AddChild(std::make_unique<BMenuField>(
			gfx::Rect{10, 100, 210, 124}, "field", "Size:", SizeMenu(), true)));
		fField->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what >= 21 && message->what <= 23) {
			fChosen.push_back(static_cast<std::int32_t>(message->what));
			return;
		}
		if (message->what == kDisableField) {
			fField->SetEnabled(false);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		(void)reply.AddString("label", fField->MenuItem()->Label());
		(void)reply.AddBool("tracking", fField->MenuBar()->IsTracking());
		(void)reply.AddInt32("popups", CountPopups());
		if (CountPopups() > 0)
			(void)reply.AddRect("last", PopupAt(CountPopups() - 1)->Frame());
		const gfx::Rect item = fField->MenuItem()->Frame();
		const gfx::Point itemOrigin = fField->MenuBar()->ConvertToScreen({item.x0, item.y0});
		(void)reply.AddRect("item", {itemOrigin.x, itemOrigin.y, itemOrigin.x + item.Width(),
			itemOrigin.y + item.Height()});
		BMenu* menu = fField->Menu();
		for (std::int32_t i = 0; i < menu->CountItems(); i++) {
			const gfx::Rect frame = menu->ItemAt(i)->Frame();
			const gfx::Point origin = menu->Window() != nullptr
				? menu->ConvertToScreen({frame.x0, frame.y0}) : gfx::Point{0, 0};
			(void)reply.AddRect("items", {origin.x, origin.y, origin.x + frame.Width(),
				origin.y + frame.Height()});
		}
		for (std::int32_t chosen : fChosen)
			(void)reply.AddInt32("chosen", chosen);
		(void)reply.AddString("focus", CurrentFocus() != nullptr ? CurrentFocus()->Name() : "");
		(void)message->SendReply(reply);
	}

private:
	BMenuField* fField = nullptr;
	std::vector<std::int32_t> fChosen;
};


// The state a FieldWindow answers.
struct FieldState {
	std::string label;
	bool tracking = false;
	std::int32_t popups = -1;
	gfx::Rect last;
	gfx::Rect item;
	std::vector<gfx::Rect> items;
	std::vector<std::int32_t> chosen;
	std::string focus;
};


class RunningField {
public:
	RunningField()
		:
		fWindow(*BLooper::Run(std::make_unique<FieldWindow>()))
	{
	}

	~RunningField()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons)
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Click(gfx::Point where)
	{
		Mouse(kMouseDown, where, 1);
		Mouse(kMouseUp, where, 0);
	}

	void Key(char byte)
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(BMessage(what)).has_value());
	}

	FieldState Ask()
	{
		// Twice, as RunningMenus::Ask().
		BMessage reply;
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		FieldState state;
		state.label = reply.GetString("label", "?");
		state.tracking = reply.GetBool("tracking", false);
		state.popups = reply.GetInt32("popups", -1);
		state.last = reply.GetRect("last", {});
		state.item = reply.GetRect("item", {});
		for (std::int32_t i = 0; auto item = reply.FindRect("items", i); i++)
			state.items.push_back(*item);
		for (std::int32_t i = 0; auto chosen = reply.FindInt32("chosen", i); i++)
			state.chosen.push_back(*chosen);
		state.focus = reply.GetString("focus", "?");
		return state;
	}

private:
	BMessenger fWindow;
};


gfx::Point
Center(const gfx::Rect& rect)
{
	return {(rect.x0 + rect.x1) / 2, (rect.y0 + rect.y1) / 2};
}


TEST(MenuFieldTest, ChoosingAnItemChangesTheLabel)
{
	// What is tested: A click on the bar of a menu field showing "Medium",
	// then a click on "Large" in the opened menu; Down, Up, Up and Enter
	// while the field has the focus; the field disabled and clicked.
	// Success criteria: The click opens the pop-up menu so that "Medium"
	// lies exactly over the bar's item, moved 8 pixels left as in Haiku
	// (for the check mark), and keeps it open (the release is in the
	// field). Choosing "Large" sends its message (23), closes the
	// menu and shows "Large". Down opens the menu without a selection; Up
	// selects the last item, then "Medium"; Enter chooses it (22), and the
	// field has the focus again. A disabled field does not open.
	// Why it matters: The menu field is how a user picks one of several
	// options, with the mouse or the keyboard.
	//
	// Given
	RunningField field;
	const FieldState before = field.Ask();

	// When
	field.Click(Center(before.item));
	const FieldState opened = field.Ask();
	ASSERT_EQ(opened.items.size(), 3u);
	field.Click(Center(opened.items[2]));
	const FieldState chosen = field.Ask();
	field.Key(keys::kDownArrow);
	const FieldState keyOpened = field.Ask();
	field.Key(keys::kUpArrow);
	field.Key(keys::kUpArrow);
	field.Key(keys::kEnter);
	const FieldState keyChosen = field.Ask();
	field.Send(kDisableField);
	field.Click(Center(before.item));
	const FieldState disabled = field.Ask();

	// Then
	EXPECT_EQ(before.label, "Medium");
	EXPECT_TRUE(opened.tracking);
	EXPECT_EQ(opened.popups, 1);
	EXPECT_EQ(opened.items[1].y0, before.item.y0);
	EXPECT_EQ(opened.items[1].x0, before.item.x0 - 8);
	EXPECT_EQ(opened.last.y0, opened.items[0].y0 - 1);
	EXPECT_FALSE(chosen.tracking);
	EXPECT_EQ(chosen.popups, 0);
	EXPECT_EQ(chosen.label, "Large");
	EXPECT_EQ(chosen.chosen, (std::vector<std::int32_t>{23}));
	EXPECT_TRUE(keyOpened.tracking);
	EXPECT_EQ(keyOpened.popups, 1);
	EXPECT_EQ(keyOpened.focus, "_mc_mb_");
	EXPECT_EQ(keyChosen.label, "Medium");
	EXPECT_EQ(keyChosen.chosen, (std::vector<std::int32_t>{23, 22}));
	EXPECT_EQ(keyChosen.focus, "field");
	EXPECT_FALSE(disabled.tracking);
	EXPECT_EQ(disabled.popups, 0);
}


} // namespace
