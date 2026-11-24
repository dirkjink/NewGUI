/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/OutlineListView.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/StringItem.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");


std::string
TextOf(const BListItem* item)
{
	const auto* stringItem = dynamic_cast<const BStringItem*>(item);
	return stringItem != nullptr ? stringItem->Text() : "?";
}


// The texts of the visible items.
std::vector<std::string>
Visible(const BOutlineListView& list)
{
	std::vector<std::string> texts;
	for (int32 i = 0; i < list.CountItems(); i++)
		texts.push_back(TextOf(list.ItemAt(i)));
	return texts;
}


// The texts of all items.
std::vector<std::string>
Full(const BOutlineListView& list)
{
	std::vector<std::string> texts;
	for (int32 i = 0; i < list.FullListCountItems(); i++)
		texts.push_back(TextOf(list.FullListItemAt(i)));
	return texts;
}


// Kits > (Support), Interface > (BView, BWindow), Storage; Apps: the
// outline the tests begin with, Support added under Kits by AddUnder().
void
AddKits(BOutlineListView& list)
{
	list.AddItem(std::make_unique<BStringItem>("Kits"));
	list.AddItem(std::make_unique<BStringItem>("Interface", 1));
	list.AddItem(std::make_unique<BStringItem>("BView", 2));
	list.AddItem(std::make_unique<BStringItem>("BWindow", 2));
	list.AddItem(std::make_unique<BStringItem>("Storage", 1));
	list.AddItem(std::make_unique<BStringItem>("Apps"));
	list.AddUnder(std::make_unique<BStringItem>("Support"), list.FullListItemAt(0));
}


TEST(OutlineListViewTest, SubitemsAreShownAndHidden)
{
	// What is tested: With the fixed text system (items of 17 pixels): an
	// outline of seven items in a window: Kits with Support (added under it
	// by AddUnder()), Interface (with BView and BWindow) and Storage; Apps.
	// Then Interface collapsed, Kits collapsed and expanded again, and
	// Interface removed.
	// Success criteria: AddUnder() makes Support the first subitem of Kits,
	// one level deeper. Superitem(), CountItemsUnder() and ItemUnderAt()
	// follow the levels. Collapsing hides the subitems from the visible
	// items, which close up; expanding Kits shows Interface again but not
	// its subitems, as Interface stays collapsed. Removing Interface takes
	// its subitems with it.
	// Why it matters: The outline view keeps the full list and the visible
	// items in step, as Haiku's does.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 200}, "outline");
	auto& list = static_cast<BOutlineListView&>(*window.AddChild(
		std::make_unique<BOutlineListView>(gfx::Rect{0, 0, 150, 150}, "outline")));

	// When
	AddKits(list);
	BListItem* kits = list.FullListItemAt(0);
	BListItem* interface = list.FullListItemAt(2);
	const std::vector<std::string> all = Visible(list);
	const uint32 supportLevel = list.FullListItemAt(1)->OutlineLevel();
	BListItem* superOfBView = list.Superitem(list.FullListItemAt(3));
	const int32 direct = list.CountItemsUnder(kits, true);
	const int32 under = list.CountItemsUnder(kits, false);
	BListItem* second = list.ItemUnderAt(kits, true, 1);
	list.Collapse(interface);
	const std::vector<std::string> interfaceCollapsed = Visible(list);
	const gfx::Rect storageFrame = list.ItemFrame(3);
	list.Collapse(kits);
	const std::vector<std::string> kitsCollapsed = Visible(list);
	list.Expand(kits);
	const std::vector<std::string> expanded = Visible(list);
	const std::unique_ptr<BListItem> removed = list.RemoveItem(interface);

	// Then
	EXPECT_EQ(all, (std::vector<std::string>{"Kits", "Support", "Interface", "BView",
		"BWindow", "Storage", "Apps"}));
	EXPECT_EQ(supportLevel, 1u);
	EXPECT_EQ(superOfBView, interface);
	EXPECT_EQ(direct, 3);
	EXPECT_EQ(under, 5);
	EXPECT_EQ(second, interface);
	EXPECT_EQ(interfaceCollapsed, (std::vector<std::string>{"Kits", "Support", "Interface",
		"Storage", "Apps"}));
	EXPECT_EQ(storageFrame, (gfx::Rect{0, 3 * 17, 150, 4 * 17}));
	EXPECT_EQ(kitsCollapsed, (std::vector<std::string>{"Kits", "Apps"}));
	EXPECT_EQ(expanded, interfaceCollapsed);
	EXPECT_EQ(removed.get(), interface);
	EXPECT_EQ(Full(list), (std::vector<std::string>{"Kits", "Support", "Storage", "Apps"}));
	EXPECT_EQ(Visible(list), Full(list));
	EXPECT_EQ(list.CountItemsUnder(kits, false), 2);
	TextSystem::SetDefault(nullptr);
}


TEST(OutlineListViewTest, CollapsingPassesTheSelectionUp)
{
	// What is tested: In the outline of the first test, BView selected,
	// then Kits collapsed; an item of level 1 inserted at full list index
	// 2, collapsed, before Interface (level 1) and before BView (level 2)
	// once Kits is expanded again.
	// Success criteria: A selected subitem that is hidden passes its
	// selection to the collapsed item: Kits is selected. An item inserted
	// before deeper items becomes their superitem: inserted collapsed
	// before BView, it hides BView and BWindow.
	// Why it matters: The selection never stays on an item that cannot be
	// seen, and the outline follows the levels wherever items are added.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 200}, "outline");
	auto& list = static_cast<BOutlineListView&>(*window.AddChild(
		std::make_unique<BOutlineListView>(gfx::Rect{0, 0, 150, 150}, "outline")));
	AddKits(list);
	BListItem* kits = list.FullListItemAt(0);

	// When
	list.Select(3);
	const std::string selected = TextOf(list.ItemAt(list.CurrentSelection()));
	list.Collapse(kits);
	const int32 afterCollapse = list.CurrentSelection();
	const int32 fullAfterCollapse = list.FullListCurrentSelection();
	list.Expand(kits);
	(void)list.AddItem(std::make_unique<BStringItem>("Group", 1, false), 3);

	// Then
	EXPECT_EQ(selected, "BView");
	EXPECT_EQ(afterCollapse, 0);
	EXPECT_EQ(fullAfterCollapse, 0);
	EXPECT_FALSE(list.FullListItemAt(4)->IsSelected());
	EXPECT_EQ(Full(list), (std::vector<std::string>{"Kits", "Support", "Interface", "Group",
		"BView", "BWindow", "Storage", "Apps"}));
	EXPECT_EQ(Visible(list), (std::vector<std::string>{"Kits", "Support", "Interface",
		"Group", "Storage", "Apps"}));
	EXPECT_EQ(list.Superitem(list.FullListItemAt(4)), list.FullListItemAt(3));
	TextSystem::SetDefault(nullptr);
}


TEST(OutlineListViewTest, SortingAndSwappingKeepTheSubitems)
{
	// What is tested: The outline of the first test sorted by text; then
	// the visible Apps and Kits swapped, and Support with Apps (which are
	// not siblings); then MoveItem().
	// Success criteria: Sorting orders the items of each level among
	// themselves, each with its subitems: Apps before Kits, under Kits
	// Interface, Storage, Support, under Interface BView before BWindow.
	// Swapping moves each item with its subitems, in the full list and
	// among the visible items; items under different superitems are not
	// swapped, nor are items moved.
	// Why it matters: The tree stays whole through every reordering.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 200}, "outline");
	auto& list = static_cast<BOutlineListView&>(*window.AddChild(
		std::make_unique<BOutlineListView>(gfx::Rect{0, 0, 150, 150}, "outline")));
	AddKits(list);

	// When
	list.FullListSortItems([](const BListItem* a, const BListItem* b) {
		return TextOf(a).compare(TextOf(b));
	});
	const std::vector<std::string> sorted = Full(list);
	const bool swapped = list.SwapItems(0, 1);
	const bool notSiblings = list.SwapItems(1, 2);
	const bool moved = list.MoveItem(0, 3);

	// Then
	EXPECT_EQ(sorted, (std::vector<std::string>{"Apps", "Kits", "Interface", "BView",
		"BWindow", "Storage", "Support"}));
	EXPECT_TRUE(swapped);
	EXPECT_FALSE(notSiblings);
	EXPECT_FALSE(moved);
	EXPECT_EQ(Full(list), (std::vector<std::string>{"Kits", "Interface", "BView", "BWindow",
		"Storage", "Support", "Apps"}));
	EXPECT_EQ(Visible(list), Full(list));
	EXPECT_EQ(list.ItemFrame(6).y0, 6 * 17);
	TextSystem::SetDefault(nullptr);
}


// A window with an outline of the first test, Interface collapsed. It
// answers kAsk with the visible items and the selected one.
class OutlineWindow : public BWindow {
public:
	OutlineWindow()
		:
		BWindow({0, 0, 200, 200}, "outline")
	{
		auto list = std::make_unique<BOutlineListView>(gfx::Rect{0, 0, 150, 150}, "outline");
		AddKits(*list);
		list->Collapse(list->FullListItemAt(2));
		fList = static_cast<BOutlineListView*>(AddChild(std::move(list)));
		fList->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (const std::string& text : Visible(*fList))
			(void)reply.AddString("visible", text);
		const int32 selection = fList->CurrentSelection();
		(void)reply.AddString("selected", selection >= 0
			? TextOf(fList->ItemAt(selection)) : "");
		(void)message->SendReply(reply);
	}

private:
	BOutlineListView* fList = nullptr;
};


TEST(OutlineListViewTest, LatchesAndArrowsExpandAndCollapse)
{
	// What is tested: In a running window, the outline of the first test
	// with Interface collapsed (items of 17 pixels, the latch 13 pixels
	// from the indentation on): a click on Interface's latch; a click on
	// Interface's text; Left, Left, Right, Right, Right.
	// Success criteria: The latch click expands Interface without selecting
	// it; the text click selects it. Left collapses the selected Interface,
	// a second Left selects its superitem Kits; Right on the expanded Kits
	// selects its first subitem Support; Right on Support, which has no
	// subitems, does nothing.
	// Why it matters: Outlines are opened and walked with mouse and
	// keyboard as in Haiku.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BMessenger window = *BLooper::Run(std::make_unique<OutlineWindow>());
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	(void)window.SendMessage(activated);
	BOutlineListView probe("probe");
	const float indent = probe.Look().DefaultItemSpacing(fixed);
	const auto click = [&](float x, int32 item) {
		for (std::uint32_t what : {kMouseDown, kMouseUp}) {
			BMessage message(what);
			(void)message.AddPoint("where", {x, static_cast<float>(item) * 17 + 8});
			(void)message.AddInt32("buttons", what == kMouseDown ? 1 : 0);
			(void)window.SendMessage(message);
		}
	};
	const auto key = [&](char byte) {
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		(void)window.SendMessage(message);
	};
	const auto ask = [&] {
		BMessage reply;
		(void)window.SendMessage(BMessage(kAsk), &reply);
		std::vector<std::string> visible;
		for (std::int32_t i = 0; auto text = reply.FindString("visible", i); i++)
			visible.emplace_back(*text);
		return std::make_pair(visible, std::string(reply.GetString("selected", "?")));
	};

	// When
	click(indent + 5, 2);
	const auto latched = ask();
	click(indent + 40, 2);
	const auto selected = ask();
	key(keys::kLeftArrow);
	const auto collapsed = ask();
	key(keys::kLeftArrow);
	const auto up = ask();
	key(keys::kRightArrow);
	const auto down = ask();
	key(keys::kRightArrow);
	const auto leaf = ask();

	// Then
	EXPECT_EQ(latched.first, (std::vector<std::string>{"Kits", "Support", "Interface", "BView",
		"BWindow", "Storage", "Apps"}));
	EXPECT_EQ(latched.second, "");
	EXPECT_EQ(selected.second, "Interface");
	EXPECT_EQ(collapsed.first, (std::vector<std::string>{"Kits", "Support", "Interface",
		"Storage", "Apps"}));
	EXPECT_EQ(collapsed.second, "Interface");
	EXPECT_EQ(up.second, "Kits");
	EXPECT_EQ(down.second, "Support");
	EXPECT_EQ(leaf.second, "Support");
	EXPECT_EQ(leaf.first, collapsed.first);

	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
}


} // namespace
