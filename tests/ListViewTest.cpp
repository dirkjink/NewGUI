/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ListView.h>

#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/ScrollBar.h>
#include <ngui/ScrollView.h>
#include <ngui/StringItem.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kSelected = FourCharCode("sel ");
constexpr std::uint32_t kInvoked = FourCharCode("inv ");


std::string
TextAt(const BListView& list, int32 index)
{
	const auto* item = dynamic_cast<const BStringItem*>(list.ItemAt(index));
	return item != nullptr ? item->Text() : "?";
}


std::vector<int32>
Selection(const BListView& list)
{
	std::vector<int32> selection;
	for (int32 i = 0; list.CurrentSelection(i) >= 0; i++)
		selection.push_back(list.CurrentSelection(i));
	return selection;
}


TEST(ListViewTest, ItemsStackAndKeepTheirSelection)
{
	// What is tested: With the fixed text system (7 px per byte, lines of
	// 9 + 3 + 1): a multiple selection list of 100 × 50 with "zero" and
	// "two", attached to a window, then "one" inserted at 1 and "three"
	// appended; items 1 and 3 selected; then item 0 removed, item 2 moved to
	// 0, items 0 and 2 swapped, item 1 replaced, the items sorted, all but
	// one removed and the list emptied.
	// Success criteria: A string item is a line and 4 pixels high, 17; the
	// items lie one below the other, IndexOf() finds them by y, and the
	// list's preferred size is the widest item by all of them. The selected
	// indices follow the items through removing, moving and swapping; a
	// replaced item gives back the old one and its selection; sorting
	// deselects.
	// Why it matters: The list keeps item geometry and selection
	// consistent through every change, as Haiku's does.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 100}, "lists");
	auto owned = std::make_unique<BListView>(gfx::Rect{0, 0, 100, 50}, "list",
		BListViewType::MultipleSelection);
	BListView& list = *owned;
	list.AddItem(std::make_unique<BStringItem>("zero"));
	list.AddItem(std::make_unique<BStringItem>("two"));

	// When
	window.AddChild(std::move(owned));
	list.AddItem(std::make_unique<BStringItem>("one"), 1);
	list.AddItem(std::make_unique<BStringItem>("three"));
	float width = 0;
	float height = 0;
	list.GetPreferredSize(&width, &height);
	const gfx::Rect second = list.ItemFrame(1);
	list.Select(1);
	list.Select(3, true);
	const std::vector<int32> selected = Selection(list);
	const std::unique_ptr<BListItem> removed = list.RemoveItem(0);
	const std::vector<int32> afterRemove = Selection(list);
	const bool moved = list.MoveItem(2, 0);
	const std::vector<int32> afterMove = Selection(list);
	const bool swapped = list.SwapItems(0, 2);
	const std::vector<int32> afterSwap = Selection(list);
	const std::string swappedFirst = TextAt(list, 0);
	const std::unique_ptr<BListItem> replaced
		= list.ReplaceItem(1, std::make_unique<BStringItem>("uno"));
	const std::vector<int32> afterReplace = Selection(list);
	list.SortItems([](const BListItem* a, const BListItem* b) {
		return std::strcmp(static_cast<const BStringItem*>(a)->Text().c_str(),
			static_cast<const BStringItem*>(b)->Text().c_str());
	});
	const std::vector<std::string> sorted
		= {TextAt(list, 0), TextAt(list, 1), TextAt(list, 2)};
	const std::vector<int32> afterSort = Selection(list);
	const bool removedSome = list.RemoveItems(1, 5);
	const int32 left = list.CountItems();
	list.MakeEmpty();

	// Then
	EXPECT_EQ(second, (gfx::Rect{0, 17, 100, 34}));
	EXPECT_EQ(list.IsEmpty(), true);
	EXPECT_EQ(width, 7 * 5 + list.Look().DefaultLabelSpacing(fixed));
	EXPECT_EQ(height, 4 * 17);
	EXPECT_EQ(selected, (std::vector<int32>{1, 3}));
	ASSERT_NE(removed, nullptr);
	EXPECT_EQ(static_cast<BStringItem&>(*removed).Text(), "zero");
	EXPECT_EQ(afterRemove, (std::vector<int32>{0, 2}));
	EXPECT_TRUE(moved);
	EXPECT_EQ(afterMove, (std::vector<int32>{0, 1}));
	EXPECT_TRUE(swapped);
	EXPECT_EQ(swappedFirst, "two");
	EXPECT_EQ(afterSwap, (std::vector<int32>{1, 2}));
	ASSERT_NE(replaced, nullptr);
	EXPECT_EQ(static_cast<BStringItem&>(*replaced).Text(), "one");
	EXPECT_EQ(afterReplace, (std::vector<int32>{2}));
	EXPECT_EQ(sorted, (std::vector<std::string>{"three", "two", "uno"}));
	EXPECT_TRUE(afterSort.empty());
	EXPECT_TRUE(removedSome);
	EXPECT_EQ(left, 1);
	TextSystem::SetDefault(nullptr);
}


TEST(ListViewTest, ItemsAreFoundByTheirPlace)
{
	// What is tested: IndexOf() of a list with three items of 17 pixels at
	// y 0, 16.5, 17, 50 and 51; IndexOf() of an item and of one not in the
	// list; ItemFrame() of an index out of range.
	// Success criteria: The frames are half-open: 16.5 is in item 0, 17 in
	// item 1, 51 below the last item (-1). An item not in the list has index
	// -1; an index out of range has an empty frame.
	// Why it matters: Clicks are mapped to items by IndexOf().
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 100}, "lists");
	BListView& list = static_cast<BListView&>(*window.AddChild(
		std::make_unique<BListView>(gfx::Rect{0, 0, 100, 50}, "list")));
	for (const char* text : {"a", "b", "c"})
		list.AddItem(std::make_unique<BStringItem>(text));
	BStringItem stranger("x");

	// When / Then
	EXPECT_EQ(list.IndexOf(gfx::Point{5, 0}), 0);
	EXPECT_EQ(list.IndexOf(gfx::Point{5, 16.5f}), 0);
	EXPECT_EQ(list.IndexOf(gfx::Point{5, 17}), 1);
	EXPECT_EQ(list.IndexOf(gfx::Point{5, 50}), 2);
	EXPECT_EQ(list.IndexOf(gfx::Point{5, 51}), -1);
	EXPECT_EQ(list.IndexOf(list.ItemAt(2)), 2);
	EXPECT_EQ(list.IndexOf(&stranger), -1);
	EXPECT_EQ(list.ItemFrame(3).Height(), 0);
	EXPECT_EQ(list.ItemAt(3), nullptr);
	TextSystem::SetDefault(nullptr);
}


// A window with a list of 100 × 50 (100 × 102 if item 2 is disabled, with
// room for all) and six items "Item 0" to "Item 5" (item 2 disabled if
// asked). It records the selection (kSelected) and
// invocation (kInvoked) messages as "s" or "i" followed by the indices,
// and answers kAsk with them and the list's scroll offset.
class ListWindow : public BWindow {
public:
	ListWindow(BListViewType type, bool disableItem2)
		:
		BWindow({0, 0, 200, 150}, "lists")
	{
		const float height = disableItem2 ? 102.0f : 50.0f;
		auto list = std::make_unique<BListView>(gfx::Rect{0, 0, 100, height}, "list", type);
		for (int i = 0; i < 6; i++) {
			auto item = std::make_unique<BStringItem>("Item " + std::to_string(i));
			if (disableItem2 && i == 2)
				item->SetEnabled(false);
			list->AddItem(std::move(item));
		}
		list->SetSelectionMessage(std::make_unique<BMessage>(kSelected));
		list->SetInvocationMessage(std::make_unique<BMessage>(kInvoked));
		fList = static_cast<BListView*>(AddChild(std::move(list)));
		fList->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSelected || message->what == kInvoked) {
			std::string record = message->what == kSelected ? "s" : "i";
			for (std::int32_t i = 0; auto index = message->FindInt32("index", i); i++)
				record += std::to_string(*index);
			fReceived.push_back(record);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (const std::string& record : fReceived)
			(void)reply.AddString("received", record);
		(void)reply.AddFloat("scroll", fList->Bounds().y0);
		(void)message->SendReply(reply);
	}

private:
	BListView* fList = nullptr;
	std::vector<std::string> fReceived;
};


// A running ListWindow, activated, with a ManualClock and the fixed text
// system for the duration of a test.
class RunningList {
public:
	RunningList(BListViewType type, bool disableItem2 = false)
	{
		Clock::SetDefault(&fClock);
		TextSystem::SetDefault(&fText);
		fWindow = *BLooper::Run(std::make_unique<ListWindow>(type, disableItem2));
		BMessage activated(kWindowActivated);
		(void)activated.AddBool("active", true);
		EXPECT_TRUE(fWindow.SendMessage(activated).has_value());
	}

	~RunningList()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		TextSystem::SetDefault(nullptr);
		Clock::SetDefault(nullptr);
	}

	void Key(std::string_view bytes, BModifierFlags modifiers = {})
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", bytes);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Mouse(std::uint32_t what, int32 item, std::int32_t buttons,
		BModifierFlags modifiers = {})
	{
		BMessage message(what);
		(void)message.AddPoint("where", {20, static_cast<float>(item) * 17 + 8});
		(void)message.AddInt32("buttons", buttons);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Click(int32 item, BModifierFlags modifiers = {})
	{
		Mouse(kMouseDown, item, 1, modifiers);
		Mouse(kMouseUp, item, 0, modifiers);
		fClock.Advance(1s);
	}

	// The records so far, and the scroll offset.
	std::vector<std::string> Received(float* scroll = nullptr)
	{
		// the list posts its messages to the window: the first answer may
		// come before them
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		std::vector<std::string> received;
		for (std::int32_t i = 0; auto record = reply.FindString("received", i); i++)
			received.emplace_back(*record);
		if (scroll != nullptr)
			*scroll = reply.GetFloat("scroll", -1);
		return received;
	}

	ManualClock& Time()
	{
		return fClock;
	}

private:
	ManualClock fClock;
	test::FixedTextSystem fText;
	BMessenger fWindow;
};


TEST(ListViewTest, OneItemIsSelectedWithMouseAndKeys)
{
	// What is tested: In a single selection list of six items of 17 pixels
	// in 50 pixels: a click on item 1 and a second one at once; the mouse
	// pressed on item 0, moved to item 2 and released there; then Down,
	// Home, End and Enter.
	// Success criteria: The click selects item 1; the second click is a
	// double click and invokes. Pressing selects item 0, the selection
	// follows the mouse without a message and is reported when it is
	// released on item 2. Down selects item 3 and scrolls it into view
	// (bottom 68 − 50 = 18); Home selects item 0 at the top, End item 5 at
	// the bottom (102 − 50 = 52); Enter invokes with item 5.
	// Why it matters: Choosing from a list with mouse and keyboard works as
	// in Haiku.
	//
	// Given
	RunningList list(BListViewType::SingleSelection);

	// When
	list.Mouse(kMouseDown, 1, 1);
	list.Mouse(kMouseUp, 1, 0);
	list.Click(1);
	const std::vector<std::string> clicked = list.Received();
	list.Mouse(kMouseDown, 0, 1);
	list.Mouse(kMouseMoved, 2, 1);
	const std::vector<std::string> pressed = list.Received();
	list.Mouse(kMouseUp, 2, 0);
	const std::vector<std::string> released = list.Received();
	list.Key(std::string(1, keys::kDownArrow));
	float down = -1;
	const std::vector<std::string> keyed = list.Received(&down);
	list.Key(std::string(1, keys::kHome));
	float home = -1;
	(void)list.Received(&home);
	list.Key(std::string(1, keys::kEnd));
	list.Key(std::string(1, keys::kEnter));
	float end = -1;
	const std::vector<std::string> all = list.Received(&end);

	// Then
	EXPECT_EQ(clicked, (std::vector<std::string>{"s1", "i1"}));
	EXPECT_EQ(pressed, (std::vector<std::string>{"s1", "i1", "s0"}));
	EXPECT_EQ(released, (std::vector<std::string>{"s1", "i1", "s0", "s2"}));
	EXPECT_EQ(keyed.back(), "s3");
	EXPECT_EQ(down, 18);
	EXPECT_EQ(home, 0);
	EXPECT_EQ(end, 52);
	EXPECT_EQ(all, (std::vector<std::string>{"s1", "i1", "s0", "s2", "s3", "s0", "s5", "i5"}));
}


TEST(ListViewTest, SeveralItemsAreSelectedWithShiftAndCommand)
{
	// What is tested: In a multiple selection list of six items with item 2
	// disabled, all visible: a click on item 0, Shift+click on item 3, Command+click on
	// item 1, a click on the disabled item 2; then Down, Shift+Down twice,
	// Shift+Up and Enter.
	// Success criteria: Shift extends the selection to item 3, leaving the
	// disabled item out; Command toggles item 1; a click on a disabled item
	// deselects all. Down with nothing selected selects the first item;
	// Shift+Down extends past the disabled item, Shift+Up takes the last one
	// back. Enter invokes with all selected indices.
	// Why it matters: Selecting several items works as in Haiku.
	//
	// Given
	RunningList list(BListViewType::MultipleSelection, true);

	// When
	list.Click(0);
	list.Click(3, BModifier::Shift);
	list.Click(1, BModifier::Command);
	list.Click(2);
	const std::vector<std::string> clicked = list.Received();
	list.Key(std::string(1, keys::kDownArrow));
	list.Key(std::string(1, keys::kDownArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kDownArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kUpArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kEnter));
	const std::vector<std::string> all = list.Received();

	// Then
	EXPECT_EQ(clicked, (std::vector<std::string>{"s0", "s013", "s03", "s"}));
	EXPECT_EQ(all, (std::vector<std::string>{"s0", "s013", "s03", "s", "s0", "s01",
		"s013", "s01", "i01"}));
}


TEST(ListViewTest, ShiftedKeysGrowAndShrinkTheSelection)
{
	// What is tested: In a multiple selection list of six items of 17
	// pixels in 50 pixels: Page Down twice and Page Up twice; then a click
	// on item 1, Shift+Down, Shift+Up twice and Space; a click on item 2,
	// Shift+Home and Shift+End.
	// Success criteria: Page Down scrolls by the list's height (50), but
	// not past the last item (102 − 50 = 52); Page Up goes back by 50 and
	// not above 0, without changing the selection. Shift+Home extends the
	// Shift+Down extends item 1 by item 2; the first Shift+Up takes item 2
	// back, the second extends upwards by item 0. Space invokes with the
	// selection. Shift+Home extends the selection from item 2 to the top,
	// Shift+End from there to the end.
	// Why it matters: Selecting ranges by keyboard and paging work as in
	// Haiku.
	//
	// Given
	RunningList list(BListViewType::MultipleSelection);
	const auto scrolled = [&] {
		float scroll = -1;
		(void)list.Received(&scroll);
		return scroll;
	};

	// When
	list.Key(std::string(1, keys::kPageDown));
	const float page = scrolled();
	list.Key(std::string(1, keys::kPageDown));
	const float last = scrolled();
	list.Key(std::string(1, keys::kPageUp));
	const float back = scrolled();
	list.Key(std::string(1, keys::kPageUp));
	const float top = scrolled();
	list.Click(1);
	list.Key(std::string(1, keys::kDownArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kUpArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kUpArrow), BModifier::Shift);
	list.Key(std::string(1, keys::kSpace));
	list.Click(2);
	list.Key(std::string(1, keys::kHome), BModifier::Shift);
	list.Key(std::string(1, keys::kEnd), BModifier::Shift);
	const std::vector<std::string> all = list.Received();

	// Then
	EXPECT_EQ(page, 50);
	EXPECT_EQ(last, 52);
	EXPECT_EQ(back, 2);
	EXPECT_EQ(top, 0);
	EXPECT_EQ(all, (std::vector<std::string>{"s1", "s12", "s1", "s01", "i01", "s2", "s012",
		"s012345"}));
}


TEST(ListViewTest, AddedListsKeepTheSelection)
{
	// What is tested: A list of three items with item 1 selected: two
	// items added before it with AddList(), one list at the end; lists
	// with an empty entry or at an index past the end.
	// Success criteria: The selected item stays selected and its index
	// moves by two, to 3; the list counts six items in order. A list with
	// an empty entry and one past the end are refused and change nothing.
	// Why it matters: Applications fill lists in batches; the selection
	// must follow its item.
	//
	// Given
	BListView view({0, 0, 100, 100}, "list", BListViewType::MultipleSelection);
	for (const char* label : {"c", "d", "e"})
		view.AddItem(std::make_unique<BStringItem>(label));
	view.Select(1);
	const auto items = [](std::initializer_list<const char*> labels) {
		std::vector<std::unique_ptr<BListItem>> list;
		for (const char* label : labels)
			list.push_back(std::make_unique<BStringItem>(label));
		return list;
	};

	// When
	const bool before = view.AddList(items({"a", "b"}), 0);
	const bool after = view.AddList(items({"f"}));
	std::vector<std::unique_ptr<BListItem>> broken = items({"x"});
	broken.push_back(nullptr);
	const bool withEmpty = view.AddList(std::move(broken));
	const bool pastTheEnd = view.AddList(items({"y"}), 7);

	// Then
	EXPECT_TRUE(before);
	EXPECT_TRUE(after);
	EXPECT_FALSE(withEmpty);
	EXPECT_FALSE(pastTheEnd);
	ASSERT_EQ(view.CountItems(), 6);
	std::string order;
	for (int32 i = 0; i < view.CountItems(); i++)
		order += static_cast<BStringItem*>(view.ItemAt(i))->Text();
	EXPECT_EQ(order, "abcdef");
	EXPECT_EQ(view.CurrentSelection(), 3);
	EXPECT_EQ(view.CurrentSelection(1), -1);
	EXPECT_TRUE(view.IsItemSelected(3));
}


TEST(ListViewTest, TheScrollBarCoversAllItems)
{
	// What is tested: A list of 100 × 50 in a BScrollView with a vertical
	// scroll bar, six items of 17 pixels added after attaching; then the
	// list focused.
	// Success criteria: The scroll range is all items' height less the
	// list's, 102 − 50; the proportion is the visible part, the small step
	// an item and the large step a page. Focusing the list highlights the
	// scroll view's border.
	// Why it matters: A long list is scrolled through its scroll view.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 200, 100}, "lists");
	auto owned = std::make_unique<BListView>(gfx::Rect{0, 0, 100, 50}, "list");
	BListView& list = *owned;
	auto& scroll = static_cast<BScrollView&>(*window.AddChild(
		std::make_unique<BScrollView>("scroll", std::move(owned), kFollowLeftTop,
			BViewFlags{}, false, true)));

	// When
	for (int i = 0; i < 6; i++)
		list.AddItem(std::make_unique<BStringItem>("Item " + std::to_string(i)));
	list.MakeFocus(true);
	BScrollBar* bar = scroll.ScrollBar(BOrientation::Vertical);
	ASSERT_NE(bar, nullptr);
	float min = -1;
	float max = -1;
	bar->GetRange(&min, &max);
	float smallStep = -1;
	float largeStep = -1;
	bar->GetSteps(&smallStep, &largeStep);

	// Then
	EXPECT_EQ(list.Bounds().Height(), 50);
	EXPECT_EQ(min, 0);
	EXPECT_EQ(max, 102 - 50);
	EXPECT_FLOAT_EQ(bar->Proportion(), 50.0f / 102);
	EXPECT_EQ(smallStep, 17);
	EXPECT_EQ(largeStep, 50);
	EXPECT_TRUE(scroll.IsBorderHighlighted());
	TextSystem::SetDefault(nullptr);
}


} // namespace
