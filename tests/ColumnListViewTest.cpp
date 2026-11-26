/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ColumnListView.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Clock.h>
#include <ngui/ColumnTypes.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/ScrollBar.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kSelected = FourCharCode("sel ");
constexpr std::uint32_t kInvoked = FourCharCode("inv ");

// With the fixed text system: rows of 13 + 4 pixels, titles of 13 + 6.
constexpr float kRow = 17;
constexpr float kTitles = 19;


// A row with the strings of fields 0, 1 ...
std::unique_ptr<BRow>
MakeRow(std::initializer_list<std::string_view> strings)
{
	auto row = std::make_unique<BRow>();
	int32 index = 0;
	for (std::string_view string : strings)
		row->SetField(std::make_unique<BStringField>(string), index++);
	return row;
}


std::string
NameOf(const BRow* row, int32 field = 0)
{
	if (row == nullptr)
		return "-";
	const auto* string = dynamic_cast<const BStringField*>(row->GetField(field));
	return string != nullptr ? string->String() : "?";
}


// The names of the top level rows (and with \a parent its subrows).
std::vector<std::string>
Names(const BColumnListView& list, BRow* parent = nullptr)
{
	std::vector<std::string> names;
	for (int32 i = 0; i < list.CountRows(parent); i++)
		names.push_back(NameOf(list.RowAt(i, parent)));
	return names;
}


std::string
SelectedNames(const BColumnListView& list)
{
	std::string names;
	for (BRow* row = list.CurrentSelection(); row != nullptr; row = list.CurrentSelection(row))
		names += (names.empty() ? "" : ",") + NameOf(row);
	return names;
}


// A list of 300 × 200 with the columns "Name" (100 pixels, field 0) and
// "Size" (60 pixels, field 1), in a window of 400 × 300.
class ListFixture {
public:
	ListFixture()
		:
		fWindow({0, 0, 400, 300}, "columns")
	{
		TextSystem::SetDefault(&fText);
		auto list = std::make_unique<BColumnListView>(gfx::Rect{0, 0, 300, 200}, "list",
			kFollowLeftTop, BViewFlag::Navigable);
		fList = static_cast<BColumnListView*>(fWindow.AddChild(std::move(list)));
		auto name = std::make_unique<BStringColumn>("Name", 100.0f, 20.0f, 400.0f);
		fName = name.get();
		fList->AddColumn(std::move(name), 0);
		auto size = std::make_unique<BStringColumn>("Size", 60.0f, 20.0f, 400.0f);
		fSize = size.get();
		fList->AddColumn(std::move(size), 1);
	}

	~ListFixture()
	{
		TextSystem::SetDefault(nullptr);
	}

	BColumnListView& List()
	{
		return *fList;
	}

	BColumn* Name() const
	{
		return fName;
	}

	BColumn* Size() const
	{
		return fSize;
	}

private:
	test::FixedTextSystem fText;
	BWindow fWindow;
	BColumnListView* fList = nullptr;
	BColumn* fName = nullptr;
	BColumn* fSize = nullptr;
};


TEST(ColumnListViewTest, RowsFormATreeBelowTheColumns)
{
	// What is tested: With the fixed text system: a list with the columns
	// Name (100 pixels) and Size (60), the rows a (with the subrows a1 and
	// a2) and b; a expanded; the Size column moved first and hidden, Name
	// taken out; a taken out, c added and swapped with b, a1 moved below
	// b; then the list cleared.
	// Success criteria: Rows are 17 pixels high and lie one below the
	// other from 0, subrows only while their parent is expanded; RowAt(),
	// IndexOf() and FindParent() find them in the tree and by y. The latch
	// area takes the first 15 pixels; the first visible column of a subrow
	// starts 15 pixels further in; ColumnAt() finds the visible columns by
	// x. A removed row comes back with its subrows; a row cannot be
	// swapped below itself.
	// Why it matters: Every drawing, click and key goes through this
	// geometry of rows and columns.
	//
	// Given
	ListFixture fixture;
	BColumnListView& list = fixture.List();
	ASSERT_TRUE(list.AddRow(MakeRow({"a", "10"})));
	BRow* a = list.RowAt(0);
	ASSERT_TRUE(list.AddRow(MakeRow({"a1", "1"}), a));
	ASSERT_TRUE(list.AddRow(MakeRow({"a2", "2"}), a));
	ASSERT_TRUE(list.AddRow(MakeRow({"b", "20"})));
	BRow* a1 = list.RowAt(0, a);
	BRow* a2 = list.RowAt(1, a);
	BRow* b = list.RowAt(1);

	// When
	gfx::Rect collapsedB;
	const bool collapsedA1Shown = list.GetRowRect(a1, nullptr);
	(void)list.GetRowRect(b, &collapsedB);
	list.ExpandOrCollapse(a, true);
	gfx::Rect expandedA1;
	gfx::Rect expandedB;
	(void)list.GetRowRect(a1, &expandedA1);
	(void)list.GetRowRect(b, &expandedB);
	BRow* parent = nullptr;
	bool parentVisible = false;
	const bool hasParent = list.FindParent(a2, &parent, &parentVisible);
	const bool topHasParent = list.FindParent(b, nullptr, nullptr);
	BRow* atY = list.RowAt(gfx::Point{5, 2 * kRow + 1});
	BRow* belowRows = list.RowAt(gfx::Point{5, 4 * kRow});
	const int32 indexOfA2 = list.IndexOf(a2);
	const int32 subrowsOfA = list.CountRows(a);
	const gfx::Rect a1Name = list.GetFieldRect(a1, fixture.Name());
	const gfx::Rect a1Size = list.GetFieldRect(a1, fixture.Size());
	BColumn* atLatch = list.ColumnAt(gfx::Point{10, 0});
	BColumn* atSize = list.ColumnAt(gfx::Point{120, 0});
	list.MoveColumn(fixture.Size(), 0);
	const gfx::Rect movedA1Size = list.GetFieldRect(a1, fixture.Size());
	list.SetColumnVisible(fixture.Size(), false);
	BColumn* afterHiding = list.ColumnAt(gfx::Point{20, 0});
	std::unique_ptr<BColumn> removedColumn = list.RemoveColumn(fixture.Name());
	std::unique_ptr<BRow> removedA = list.RemoveRow(a);
	ASSERT_TRUE(list.AddRow(MakeRow({"c", "30"})));
	const bool swapped = list.SwapRows(0, 1);
	const std::vector<std::string> afterSwap = Names(list);
	ASSERT_TRUE(list.AddRow(std::move(removedA), 0));
	BRow* again = list.RowAt(0);
	const int32 subrowsAgain = list.CountRows(again);
	const bool swappedBelowItself = list.SwapRows(0, 0, nullptr, again);
	const bool movedSubrow = list.SwapRows(1, 0, nullptr, again);
	const std::vector<std::string> afterSubrowSwap = Names(list, again);
	const std::string againName = NameOf(again);
	list.Clear();

	// Then
	EXPECT_FALSE(collapsedA1Shown);
	EXPECT_EQ(collapsedB.y0, kRow);
	EXPECT_EQ(collapsedB.y1, 2 * kRow);
	EXPECT_EQ(expandedA1.y0, kRow);
	EXPECT_EQ(expandedB.y0, 3 * kRow);
	EXPECT_EQ(atY, a2);
	EXPECT_EQ(belowRows, nullptr);
	EXPECT_EQ(indexOfA2, 1);
	EXPECT_EQ(subrowsOfA, 2);
	EXPECT_TRUE(hasParent);
	EXPECT_EQ(parent, a);
	EXPECT_TRUE(parentVisible);
	EXPECT_FALSE(topHasParent);
	EXPECT_EQ(a1Name, (gfx::Rect{30, kRow, 115, 2 * kRow}));
	EXPECT_EQ(a1Size, (gfx::Rect{115, kRow, 175, 2 * kRow}));
	EXPECT_EQ(atLatch, nullptr);
	EXPECT_EQ(atSize, fixture.Size());
	EXPECT_EQ(movedA1Size, (gfx::Rect{30, kRow, 75, 2 * kRow}));
	EXPECT_EQ(afterHiding, fixture.Name());
	ASSERT_NE(removedColumn, nullptr);
	EXPECT_EQ(removedColumn->LogicalFieldNum(), 0);
	EXPECT_EQ(list.CountColumns(), 1);
	EXPECT_TRUE(swapped);
	EXPECT_EQ(afterSwap, (std::vector<std::string>{"c", "b"}));
	EXPECT_EQ(againName, "a");
	EXPECT_EQ(subrowsAgain, 2);
	EXPECT_FALSE(swappedBelowItself);
	EXPECT_TRUE(movedSubrow);
	EXPECT_EQ(afterSubrowSwap, (std::vector<std::string>{"c", "a2"}));
	EXPECT_EQ(list.CountRows(), 0);
}


TEST(ColumnListViewTest, RowsSortByTheSortColumns)
{
	// What is tested: Rows b/2, A/3, c/1 and a/1 (Name/Size), b with the
	// subrows z and y; sorted by Name ascending, then by Size descending
	// alone, then by Name as a second column; d/2 and e (without a size)
	// added, a's name changed to f; then the sort columns cleared, g
	// appended and h inserted at 0 with sorting disabled.
	// Success criteria: Names sort without regard to case, then by bytes
	// (A before a); subrows are sorted too. A single sort column replaces
	// the others and sorting is stable; a second one decides between
	// equal rows. Added rows are sorted in, a row without the field sorts
	// last; UpdateRow() sorts a changed row anew. Without sort columns the
	// order stays and rows go where they are put.
	// Why it matters: A click on a title must order the rows the way the
	// user expects, and keep them in order as rows come and change.
	//
	// Given
	ListFixture fixture;
	BColumnListView& list = fixture.List();
	const std::pair<const char*, const char*> rows[] = {{"b", "2"}, {"A", "3"}, {"c", "1"},
		{"a", "1"}};
	for (const auto& [name, size] : rows)
		ASSERT_TRUE(list.AddRow(MakeRow({name, size})));
	BRow* b = list.RowAt(0);
	ASSERT_TRUE(list.AddRow(MakeRow({"z", "0"}), b));
	ASSERT_TRUE(list.AddRow(MakeRow({"y", "0"}), b));

	// When
	list.SetSortColumn(fixture.Name(), false, true);
	const std::vector<std::string> byName = Names(list);
	const std::vector<std::string> subrows = Names(list, b);
	list.SetSortColumn(fixture.Size(), false, false);
	const std::vector<std::string> bySize = Names(list);
	list.SetSortColumn(fixture.Name(), true, true);
	const std::vector<std::string> bySizeAndName = Names(list);
	ASSERT_TRUE(list.AddRow(MakeRow({"d", "2"}), 0));
	ASSERT_TRUE(list.AddRow(MakeRow({"e"})));
	const std::vector<std::string> afterAdding = Names(list);
	BRow* a = list.RowAt(3);
	a->SetField(std::make_unique<BStringField>("f"), 0);
	list.UpdateRow(a);
	const std::vector<std::string> afterUpdate = Names(list);
	const int32 sortColumns = list.CountSortColumns();
	const bool sizeAscending = list.IsSortAscending(list.SortColumnAt(0));
	list.ClearSortColumns();
	ASSERT_TRUE(list.AddRow(MakeRow({"g", "9"})));
	list.SetSortingEnabled(false);
	ASSERT_TRUE(list.AddRow(MakeRow({"h", "9"}), 0));

	// Then
	EXPECT_EQ(byName, (std::vector<std::string>{"A", "a", "b", "c"}));
	EXPECT_EQ(subrows, (std::vector<std::string>{"y", "z"}));
	EXPECT_EQ(bySize, (std::vector<std::string>{"A", "b", "a", "c"}));
	EXPECT_EQ(bySizeAndName, (std::vector<std::string>{"A", "b", "a", "c"}));
	EXPECT_EQ(afterAdding, (std::vector<std::string>{"A", "b", "d", "a", "c", "e"}));
	EXPECT_EQ(afterUpdate, (std::vector<std::string>{"A", "b", "d", "c", "f", "e"}));
	EXPECT_EQ(sortColumns, 2);
	EXPECT_FALSE(sizeAscending);
	EXPECT_EQ(Names(list), (std::vector<std::string>{"h", "A", "b", "d", "c", "f", "e", "g"}));
}


// A BColumnListView that counts SelectionChanged().
class CountingList : public BColumnListView {
public:
	CountingList()
		:
		BColumnListView(gfx::Rect{0, 0, 300, 200}, "list", kFollowLeftTop, BViewFlags{})
	{
	}

	void SelectionChanged() override
	{
		fChanges++;
		BColumnListView::SelectionChanged();
	}

	int Changes() const
	{
		return fChanges;
	}

private:
	int fChanges = 0;
};


TEST(ColumnListViewTest, OnlyVisibleRowsAreSelected)
{
	// What is tested: A multiple selection list of the rows r0 to r3, r1
	// with the subrow r1a (expanded): r0, r1a and r3 added to the
	// selection and r3 again; r1 collapsed; r3 deselected; the selection
	// mode set to single, then r2 added; r2 made the focus row and
	// removed; r1a added to the selection while hidden; the list cleared.
	// Success criteria: CurrentSelection() goes through the selection in
	// the order of the rows; each real change calls SelectionChanged()
	// once, selecting a selected row does not. Collapsing deselects the
	// subrow; a single selection keeps the first row and replaces it; a
	// removed row is deselected and the focus moves to the row now in its
	// place; a hidden row cannot be selected; clearing deselects.
	// Why it matters: The selection is what the application acts on; it
	// must never contain rows the user cannot see.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 400, 300}, "columns");
	auto& list = static_cast<CountingList&>(*window.AddChild(std::make_unique<CountingList>()));
	list.AddColumn(std::make_unique<BStringColumn>("Name", 100.0f, 20.0f, 400.0f), 0);
	for (const char* name : {"r0", "r1", "r2", "r3"})
		ASSERT_TRUE(list.AddRow(MakeRow({name})));
	BRow* r1 = list.RowAt(1);
	ASSERT_TRUE(list.AddRow(MakeRow({"r1a"}), r1));
	BRow* r1a = list.RowAt(0, r1);
	list.ExpandOrCollapse(r1, true);

	// When
	list.AddToSelection(list.RowAt(0));
	list.AddToSelection(list.RowAt(3));
	list.AddToSelection(r1a);
	list.AddToSelection(list.RowAt(3));
	const std::string selected = SelectedNames(list);
	const int changesAfterSelecting = list.Changes();
	list.ExpandOrCollapse(r1, false);
	const std::string afterCollapse = SelectedNames(list);
	list.Deselect(list.RowAt(3));
	list.SetSelectionMode(BListViewType::SingleSelection);
	const std::string single = SelectedNames(list);
	list.AddToSelection(list.RowAt(2));
	const std::string replaced = SelectedNames(list);
	list.SetFocusRow(list.RowAt(2));
	std::unique_ptr<BRow> removed = list.RemoveRow(list.RowAt(2));
	const std::string afterRemove = SelectedNames(list);
	const std::string focusAfterRemove = NameOf(list.FocusRow());
	list.AddToSelection(r1a);
	const std::string hidden = SelectedNames(list);
	list.AddToSelection(list.RowAt(0));
	const int changesBeforeClear = list.Changes();
	list.Clear();

	// Then
	EXPECT_EQ(selected, "r0,r1a,r3");
	EXPECT_EQ(changesAfterSelecting, 3);
	EXPECT_EQ(afterCollapse, "r0,r3");
	EXPECT_EQ(single, "r0");
	EXPECT_EQ(replaced, "r2");
	ASSERT_NE(removed, nullptr);
	EXPECT_FALSE(removed->IsSelected());
	EXPECT_EQ(afterRemove, "");
	EXPECT_EQ(focusAfterRemove, "r3");
	EXPECT_EQ(hidden, "");
	EXPECT_EQ(changesBeforeClear, 8);
	EXPECT_EQ(list.Changes(), 9);
	EXPECT_EQ(list.CurrentSelection(), nullptr);
	EXPECT_EQ(list.FocusRow(), nullptr);
	TextSystem::SetDefault(nullptr);
}


// A list that records each change of the selection and each invocation
// as "s" or "i" followed by the selected names, when they happen.
class RecordingList : public BColumnListView {
public:
	RecordingList(gfx::Rect frame, std::vector<std::string>* records)
		:
		BColumnListView(frame, "list", kFollowLeftTop, BViewFlag::Navigable, BBorderStyle::None,
			false),
		fRecords(records)
	{
	}

	void SelectionChanged() override
	{
		fRecords->push_back("s " + SelectedNames(*this));
		BColumnListView::SelectionChanged();
	}

	void ItemInvoked() override
	{
		fRecords->push_back("i " + SelectedNames(*this));
		BColumnListView::ItemInvoked();
	}

private:
	std::vector<std::string>* fRecords;
};


// A window with a RecordingList at 0, 0 of 300 × height (no border, no
// horizontal scroll bar), multiple selection, the column Name, the rows
// "Row 0" to "Row 5" and below Row 1 "Row 1a" and "Row 1b" (collapsed). It
// counts the selection (kSelected) and invocation (kInvoked) messages and
// answers kAsk with the records, the count, the names of the top level
// rows and the scroll offset.
class ListWindow : public BWindow {
public:
	explicit ListWindow(float height)
		:
		BWindow({0, 0, 400, 300}, "columns")
	{
		auto list = std::make_unique<RecordingList>(gfx::Rect{0, 0, 300, height}, &fRecords);
		list->AddColumn(std::make_unique<BStringColumn>("Name", 100.0f, 20.0f, 400.0f), 0);
		for (int i = 0; i < 6; i++)
			list->AddRow(MakeRow({"Row " + std::to_string(i)}));
		BRow* row1 = list->RowAt(1);
		list->AddRow(MakeRow({"Row 1a"}), row1);
		list->AddRow(MakeRow({"Row 1b"}), row1);
		list->SetSelectionMessage(std::make_unique<BMessage>(kSelected));
		list->SetInvocationMessage(std::make_unique<BMessage>(kInvoked));
		fList = static_cast<BColumnListView*>(AddChild(std::move(list)));
		fList->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSelected || message->what == kInvoked) {
			fMessages++;
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (const std::string& record : fRecords)
			(void)reply.AddString("received", record);
		(void)reply.AddInt32("messages", fMessages);
		for (const std::string& name : Names(*fList))
			(void)reply.AddString("name", name);
		(void)reply.AddFloat("scroll", fList->ScrollView()->Bounds().y0);
		(void)message->SendReply(reply);
	}

private:
	BColumnListView* fList = nullptr;
	std::vector<std::string> fRecords;
	int32 fMessages = 0;
};


// A running ListWindow, activated, with a ManualClock and the fixed text
// system for the duration of a test.
class RunningList {
public:
	explicit RunningList(float height = 200)
	{
		Clock::SetDefault(&fClock);
		TextSystem::SetDefault(&fText);
		fWindow = *BLooper::Run(std::make_unique<ListWindow>(height));
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

	void Key(char byte, BModifierFlags modifiers = {})
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", std::string(1, byte));
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons,
		BModifierFlags modifiers = {})
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	// A click at \a x into visible row \a row (-1: the titles).
	void Click(int32 row, BModifierFlags modifiers = {}, float x = 50)
	{
		const gfx::Point where = {x, row < 0 ? kTitles / 2
			: kTitles + static_cast<float>(row) * kRow + kRow / 2};
		Mouse(kMouseDown, where, 1, modifiers);
		Mouse(kMouseUp, where, 0, modifiers);
		fClock.Advance(1s);
	}

	// The records so far, the top level names and the scroll offset; each
	// record must have sent its message.
	std::vector<std::string> Received(std::vector<std::string>* names = nullptr,
		float* scroll = nullptr)
	{
		// the list posts its messages to the window: the first answer may
		// come before them
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		std::vector<std::string> received;
		for (std::int32_t i = 0; auto record = reply.FindString("received", i); i++)
			received.emplace_back(*record);
		if (names != nullptr) {
			names->clear();
			for (std::int32_t i = 0; auto name = reply.FindString("name", i); i++)
				names->emplace_back(*name);
		}
		if (scroll != nullptr)
			*scroll = reply.GetFloat("scroll", -1);
		EXPECT_EQ(reply.GetInt32("messages", -1), static_cast<int32>(received.size()));
		return received;
	}

private:
	ManualClock fClock;
	test::FixedTextSystem fText;
	BMessenger fWindow;
};


TEST(ColumnListViewTest, RowsAreChosenWithTheMouse)
{
	// What is tested: In a running multiple selection list: a click on
	// Row 0, a Shift click on Row 2, a Command click on Row 1; a click on
	// the latch of Row 1, then on Row 1a, then on the latch again; two
	// quick clicks on Row 3; a click below the rows; then two clicks on the
	// title Name.
	// Success criteria: A click selects the row alone, Shift selects the
	// range from it, Command toggles. The latch expands and collapses
	// without changing the selection, except that collapsing deselects the
	// subrow. A double click invokes with the row selected; a click below
	// the rows deselects. The first click on the title keeps the ascending
	// order, the second reverses it.
	// Why it matters: These are the gestures every column list is used
	// with.
	//
	// Given
	RunningList list;

	// When
	list.Click(0);
	list.Click(2, BModifier::Shift);
	list.Click(1, BModifier::Command);
	list.Click(1, {}, 7);
	list.Click(2);
	list.Click(1, {}, 7);
	const gfx::Point row3 = {50, kTitles + 3 * kRow + kRow / 2};
	list.Mouse(kMouseDown, row3, 1);
	list.Mouse(kMouseUp, row3, 0);
	list.Mouse(kMouseDown, row3, 1);
	list.Mouse(kMouseUp, row3, 0);
	list.Click(7);
	std::vector<std::string> ascending;
	(void)list.Received(&ascending);
	list.Click(-1);
	std::vector<std::string> sortedOnce;
	(void)list.Received(&sortedOnce);
	list.Click(-1);
	std::vector<std::string> sortedTwice;
	const std::vector<std::string> received = list.Received(&sortedTwice);

	// Then
	EXPECT_EQ(received, (std::vector<std::string>{"s Row 0", "s Row 0,Row 1,Row 2",
		"s Row 0,Row 2", "s Row 1a", "s ", "s Row 3", "i Row 3", "s "}));
	EXPECT_EQ(sortedOnce, ascending);
	EXPECT_EQ(sortedTwice, (std::vector<std::string>{"Row 5", "Row 4", "Row 3", "Row 2",
		"Row 1", "Row 0"}));
}


TEST(ColumnListViewTest, RowsAreChosenWithTheKeys)
{
	// What is tested: In a running multiple selection list showing four
	// rows (68 pixels): Down without a focus row, Shift Down, End, Home;
	// Down to Row 1, Right twice, Left twice; Control Down, Space, Enter;
	// Page Down.
	// Success criteria: Down starts at the first row; the keys select the
	// focus row alone, Shift extends the range, Control only moves the
	// focus and Space then toggles that row. End scrolls the last row into
	// view (6 × 17 − 68 = 34), Home back to 0. Right expands Row 1 and then
	// moves into its subrows, Left goes back to the parent and then
	// collapses it. Enter invokes with the selection; Page Down moves a
	// page (four rows) on, at most to the last row.
	// Why it matters: A column list must be usable without a mouse.
	//
	// Given
	RunningList list(kTitles + 4 * kRow);

	// When
	list.Key(keys::kDownArrow);
	list.Key(keys::kDownArrow, BModifier::Shift);
	list.Key(keys::kEnd);
	float atEnd = 0;
	(void)list.Received(nullptr, &atEnd);
	list.Key(keys::kHome);
	float atHome = -1;
	(void)list.Received(nullptr, &atHome);
	list.Key(keys::kDownArrow);
	list.Key(keys::kRightArrow);
	list.Key(keys::kRightArrow);
	list.Key(keys::kLeftArrow);
	list.Key(keys::kLeftArrow);
	list.Key(keys::kDownArrow, BModifier::Control);
	list.Key(keys::kSpace);
	list.Key(keys::kEnter);
	list.Key(keys::kPageDown);
	const std::vector<std::string> received = list.Received();

	// Then
	EXPECT_EQ(atEnd, 6 * kRow - 4 * kRow);
	EXPECT_EQ(atHome, 0);
	EXPECT_EQ(received, (std::vector<std::string>{"s Row 0", "s Row 0,Row 1", "s Row 5",
		"s Row 0", "s Row 1", "s Row 1a", "s Row 1", "s Row 1,Row 2", "i Row 1,Row 2",
		"s Row 5"}));
}


TEST(ColumnListViewTest, ScrollBarsFollowRowsAndColumns)
{
	// What is tested: A list of 300 × 100 with a horizontal scroll bar and
	// the columns Name and Size of 200 pixels each, then ten rows; the
	// rows scrolled to 30, 20; then all rows but one removed.
	// Success criteria: The vertical bar ranges over the rows beyond the
	// view, the horizontal one over the columns and the latch area beyond
	// it; the titles scroll horizontally with the rows. When the rows fit
	// again, the vertical range is 0 and the rows are back at the top.
	// Why it matters: Rows and titles must stay aligned, and no row may
	// be out of reach.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BWindow window({0, 0, 400, 300}, "columns");
	auto& list = static_cast<BColumnListView&>(*window.AddChild(
		std::make_unique<BColumnListView>(gfx::Rect{0, 0, 300, 100}, "list", kFollowLeftTop,
			BViewFlags{})));
	list.AddColumn(std::make_unique<BStringColumn>("Name", 200.0f, 20.0f, 400.0f), 0);
	list.AddColumn(std::make_unique<BStringColumn>("Size", 200.0f, 20.0f, 400.0f), 1);
	for (int i = 0; i < 10; i++)
		ASSERT_TRUE(list.AddRow(MakeRow({"r" + std::to_string(i), "1"})));
	BView* rows = list.ScrollView();
	BView* titles = list.FindView("titles");
	ASSERT_NE(titles, nullptr);

	// When
	float verticalMax = 0;
	float horizontalMax = 0;
	rows->ScrollBar(BOrientation::Vertical)->GetRange(nullptr, &verticalMax);
	rows->ScrollBar(BOrientation::Horizontal)->GetRange(nullptr, &horizontalMax);
	const float rowsHeight = rows->Bounds().Height();
	const float rowsWidth = rows->Bounds().Width();
	rows->ScrollTo({30, 20});
	const float titlesLeft = titles->Bounds().x0;
	std::vector<BRow*> all;
	for (int32 i = 1; i < list.CountRows(); i++)
		all.push_back(list.RowAt(i));
	const std::vector<std::unique_ptr<BRow>> removed = list.RemoveRows(all);
	float verticalAfter = -1;
	rows->ScrollBar(BOrientation::Vertical)->GetRange(nullptr, &verticalAfter);

	// Then
	EXPECT_EQ(verticalMax, 10 * kRow - rowsHeight);
	EXPECT_EQ(horizontalMax, 15 + 400 - rowsWidth);
	EXPECT_EQ(titlesLeft, 30);
	EXPECT_EQ(removed.size(), 9u);
	EXPECT_EQ(verticalAfter, 0);
	EXPECT_EQ(rows->Bounds().y0, 0);
	TextSystem::SetDefault(nullptr);
}


// A column that records the mouse hooks it gets.
class EventColumn : public BStringColumn {
public:
	explicit EventColumn(std::vector<std::string>* events)
		:
		BStringColumn("Events", 100, 20, 400),
		fEvents(events)
	{
		SetWantsEvents(true);
	}

	void MouseDown(BColumnListView*, BRow* row, BField*, gfx::Rect fieldRect, gfx::Point,
		int32 buttons) override
	{
		fEvents->push_back("down " + NameOf(row) + " " + std::to_string(fieldRect.x0) + " "
			+ std::to_string(buttons));
	}

	void MouseMoved(BColumnListView*, BRow* row, BField*, gfx::Rect, gfx::Point point,
		int32 buttons, BMouseTransit) override
	{
		fEvents->push_back("moved " + NameOf(row) + " " + std::to_string(int(point.x)) + " "
			+ std::to_string(buttons));
	}

	void MouseUp(BColumnListView*, BRow* row, BField*) override
	{
		fEvents->push_back("up " + NameOf(row));
	}

private:
	std::vector<std::string>* fEvents;
};


// A running window with a list at 0, 0 of 300 × 200 (no border) with the
// columns Name and Events (which wants events, field 0) and the rows a and
// b. kAsk is answered with the events and the selected row; kTrack enables
// mouse tracking.
class EventWindow : public BWindow {
public:
	EventWindow()
		:
		BWindow({0, 0, 400, 300}, "columns")
	{
		auto list = std::make_unique<BColumnListView>(gfx::Rect{0, 0, 300, 200}, "list",
			kFollowLeftTop, BViewFlags{});
		list->AddColumn(std::make_unique<BStringColumn>("Name", 100.0f, 20.0f, 400.0f), 0);
		list->AddColumn(std::make_unique<EventColumn>(&fEvents), 0);
		list->AddRow(MakeRow({"a"}));
		list->AddRow(MakeRow({"b"}));
		fList = static_cast<BColumnListView*>(AddChild(std::move(list)));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kTrack) {
			fList->SetMouseTrackingEnabled(true);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (const std::string& event : fEvents)
			(void)reply.AddString("event", event);
		(void)reply.AddString("selected", NameOf(fList->CurrentSelection()));
		(void)message->SendReply(reply);
	}

	static constexpr std::uint32_t kTrack = FourCharCode("trak");

private:
	BColumnListView* fList = nullptr;
	std::vector<std::string> fEvents;
};


TEST(ColumnListViewTest, ColumnsThatWantEventsGetTheMouse)
{
	// What is tested: A running window with a list of the columns Name and
	// Events (which wants events) and the rows a and b: the mouse pressed
	// on b in Events, moved and released; moved over a without a button,
	// first without mouse tracking, then with it; pressed on a in Name.
	// Success criteria: The column gets MouseDown() with the row, its
	// field's left edge and the buttons, MouseMoved() while pressed and
	// MouseUp() with the row; without a button only with mouse tracking
	// enabled; a press in another column reaches it not. The press still
	// selects the row.
	// Why it matters: Columns with controls in their fields (check boxes,
	// links) rely on these hooks.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BMessenger window = *BLooper::Run(std::make_unique<EventWindow>());
	// a point of the rows in window coordinates
	auto send = [&](std::uint32_t what, gfx::Point where, std::int32_t buttons) {
		BMessage message(what);
		(void)message.AddPoint("where", {where.x, kTitles + where.y});
		(void)message.AddInt32("buttons", buttons);
		EXPECT_TRUE(window.SendMessage(message).has_value());
	};

	// When
	send(kMouseDown, {150, kRow + 5}, 1);
	send(kMouseMoved, {160, kRow + 5}, 1);
	send(kMouseUp, {160, kRow + 5}, 0);
	send(kMouseMoved, {170, 5}, 0);
	EXPECT_TRUE(window.SendMessage(BMessage(EventWindow::kTrack)).has_value());
	send(kMouseMoved, {171, 5}, 0);
	send(kMouseDown, {50, 5}, 1);
	send(kMouseUp, {50, 5}, 0);
	BMessage reply;
	(void)window.SendMessage(BMessage(kAsk), &reply);
	std::vector<std::string> events;
	for (std::int32_t i = 0; auto event = reply.FindString("event", i); i++)
		events.emplace_back(*event);
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);

	// Then
	EXPECT_EQ(events, (std::vector<std::string>{"down b 115.000000 1", "moved b 160 1",
		"up b", "moved a 171 0"}));
	EXPECT_EQ(reply.GetString("selected", ""), "a");
	TextSystem::SetDefault(nullptr);
}


} // namespace
