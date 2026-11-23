/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Menu.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/MenuItem.h>
#include <ngui/Message.h>
#include <ngui/SeparatorItem.h>
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


} // namespace
