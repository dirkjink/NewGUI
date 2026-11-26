/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ngui/ColumnListView.h>
#include <ngui/ColumnTypes.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Window.h>


namespace {


// A row of 0 to 15.5 pixels with its key as field 0 and, for some keys,
// as field 1.
std::unique_ptr<ngui::BRow>
MakeRow(std::uint8_t key)
{
	auto row = std::make_unique<ngui::BRow>(static_cast<float>(key % 32) / 2.0f);
	row->SetField(std::make_unique<ngui::BStringField>(std::string(1, char('a' + key % 26))), 0);
	if ((key & 0x40) != 0)
		row->SetField(std::make_unique<ngui::BStringField>(std::to_string(key / 8)), 1);
	return row;
}


struct Shown {
	ngui::BRow*			row;
	ngui::BRow*			parent;
	bool				visible;
};


// All rows depth first, with their parents and whether they are shown.
void
Collect(const ngui::BColumnListView& list, ngui::BRow* parent, bool visible,
	std::vector<Shown>* rows)
{
	for (ngui::int32 i = 0; i < list.CountRows(parent); i++) {
		ngui::BRow* row = list.RowAt(i, parent);
		rows->push_back({row, parent, visible});
		Collect(list, row, visible && row->IsExpanded(), rows);
	}
}


} // namespace


NGUI_FUZZ_TARGET(ColumnListView)
{
	// What is tested: A column list view of 200 × 120 with two string
	// columns in a window, changed by arbitrary bytes taken in pairs: an
	// operation and its argument, which adds rows (at the end, anywhere,
	// under another row), removes one or several, expands, collapses,
	// selects, deselects, swaps rows, changes and updates a field, sorts by
	// either column, clears the sort columns, toggles sorting, presses the
	// keys, sets the focus row, switches the selection mode, hides and
	// moves columns, sets the latch width and clears the list; rows of 0 to
	// 15.5 pixels, some without the second field.
	// Success criteria: No crash, leak or sanitizer report; after every
	// operation each row of the tree is shown exactly when all its parents
	// are expanded, the shown rows lie one below the other from 0 and are
	// found there by RowAt(); FindParent() and IndexOf() agree with the
	// tree; only shown rows are selected, CurrentSelection() lists exactly
	// them in order, at most one in a single selection list; the focus row
	// is shown or none.
	// Why it matters: The list owns a tree of rows and keeps pointers into
	// it (focus, anchor, the pressed latch); when they drift, rows are
	// lost, drawn wrongly or used after they were freed.
	//
	// Given
	ngui::BWindow window({0, 0, 300, 200}, "fuzz");
	auto& list = static_cast<ngui::BColumnListView&>(*window.AddChild(
		std::make_unique<ngui::BColumnListView>(ngui::gfx::Rect{0, 0, 200, 120}, "list",
			ngui::kFollowLeftTop, ngui::BViewFlags{})));
	list.AddColumn(std::make_unique<ngui::BStringColumn>("A", 60, 10, 200), 0);
	list.AddColumn(std::make_unique<ngui::BStringColumn>("B", 60, 10, 200), 1);

	for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
		const std::uint8_t op = data[i];
		const std::uint8_t arg = data[i + 1];
		std::vector<Shown> before;
		Collect(list, nullptr, true, &before);
		ngui::BRow* some = before.empty() ? nullptr : before[arg % before.size()].row;
		ngui::BRow* other = before.empty() ? nullptr
			: before[(op / 16u + arg / 16u) % before.size()].row;

		// When
		switch (op % 16) {
			case 0:
				(void)list.AddRow(MakeRow(arg));
				break;
			case 1:
				(void)list.AddRow(MakeRow(arg), static_cast<ngui::int32>(op / 16) - 1,
					(op & 0x80) != 0 ? other : nullptr);
				break;
			case 2:
				(void)list.RemoveRow(some);
				break;
			case 3:
				list.ExpandOrCollapse(some, (arg & 1) == 0);
				break;
			case 4:
				list.AddToSelection(some);
				break;
			case 5:
				if ((op & 0x10) != 0)
					list.DeselectAll();
				else
					list.Deselect(some);
				break;
			case 6:
				if (some != nullptr && other != nullptr) {
					ngui::BRow* someParent = nullptr;
					ngui::BRow* otherParent = nullptr;
					(void)list.FindParent(some, &someParent, nullptr);
					(void)list.FindParent(other, &otherParent, nullptr);
					(void)list.SwapRows(list.IndexOf(some), list.IndexOf(other), someParent,
						otherParent);
				}
				break;
			case 7:
				if (some != nullptr) {
					some->SetField(std::make_unique<ngui::BStringField>(std::string(1,
						char('a' + arg % 7))), (op & 0x10) != 0 ? 1 : 0);
					list.UpdateRow(some);
				}
				break;
			case 8:
				list.SetSortColumn(list.ColumnAt(arg % 2), (op & 0x10) != 0, (arg & 2) != 0);
				break;
			case 9:
				if ((op & 0x10) != 0)
					list.ClearSortColumns();
				else
					list.SetSortingEnabled(!list.SortingEnabled());
				break;
			case 10:
			{
				const char keys[] = {ngui::keys::kUpArrow, ngui::keys::kDownArrow,
					ngui::keys::kLeftArrow, ngui::keys::kRightArrow, ngui::keys::kHome,
					ngui::keys::kEnd, ngui::keys::kPageUp, ngui::keys::kPageDown,
					ngui::keys::kSpace};
				list.KeyDown(std::string(1, keys[arg % sizeof(keys)]));
				break;
			}
			case 11:
				list.SetFocusRow(some, (op & 0x10) != 0);
				break;
			case 12:
				list.SetSelectionMode((arg & 1) != 0 ? ngui::BListViewType::SingleSelection
					: ngui::BListViewType::MultipleSelection);
				break;
			case 13:
			{
				std::vector<ngui::BRow*> rows = {some, other};
				(void)list.RemoveRows(rows);
				break;
			}
			case 14:
				if ((op & 0x10) != 0)
					list.SetColumnVisible(arg % 2, !list.IsColumnVisible(arg % 2));
				else if ((op & 0x20) != 0)
					list.MoveColumn(list.ColumnAt(arg % 2), (arg / 2) % 3);
				else
					list.SetLatchWidth(static_cast<float>(arg % 24));
				break;
			case 15:
				if (arg == 0)
					list.Clear();
				else
					list.ScrollTo(some);
				break;
		}

		// Then
		std::vector<Shown> rows;
		Collect(list, nullptr, true, &rows);
		std::vector<ngui::BRow*> selected;
		float top = 0;
		for (const Shown& shown : rows) {
			ngui::gfx::Rect rect;
			NGUI_FUZZ_CHECK(list.GetRowRect(shown.row, &rect) == shown.visible);
			ngui::BRow* parent = nullptr;
			const bool hasParent = list.FindParent(shown.row, &parent, nullptr);
			NGUI_FUZZ_CHECK(hasParent == (shown.parent != nullptr));
			NGUI_FUZZ_CHECK(parent == shown.parent);
			NGUI_FUZZ_CHECK(list.RowAt(list.IndexOf(shown.row), shown.parent) == shown.row);
			if (!shown.visible) {
				NGUI_FUZZ_CHECK(!shown.row->IsSelected());
				NGUI_FUZZ_CHECK(list.FocusRow() != shown.row);
				continue;
			}
			NGUI_FUZZ_CHECK(rect.y0 == top);
			if (shown.row->Height() > 0)
				NGUI_FUZZ_CHECK(list.RowAt(ngui::gfx::Point{1, top}) == shown.row);
			top += shown.row->Height();
			if (shown.row->IsSelected())
				selected.push_back(shown.row);
		}

		std::vector<ngui::BRow*> reported;
		for (ngui::BRow* row = list.CurrentSelection(); row != nullptr
			&& reported.size() <= rows.size(); row = list.CurrentSelection(row)) {
			reported.push_back(row);
		}
		NGUI_FUZZ_CHECK(reported == selected);
		if (list.SelectionMode() == ngui::BListViewType::SingleSelection)
			NGUI_FUZZ_CHECK(selected.size() <= 1);
	}
}
