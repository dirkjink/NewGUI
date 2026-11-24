/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/OutlineListView.h>
#include <ngui/Window.h>


namespace {


// An item of a fixed height that draws nothing; its key orders it.
class OutlineFuzzItem : public ngui::BListItem {
public:
	OutlineFuzzItem(std::uint8_t key)
		:
		BListItem(key % 4, (key & 0x10) != 0),
		fKey(key)
	{
	}

	void DrawItem(ngui::BView*, ngui::DrawContext&, ngui::gfx::Rect, const ngui::gfx::Color&,
		bool) override
	{
	}

	void Update(ngui::BView*, const ngui::TextSystem&) override
	{
		SetHeight(static_cast<float>(fKey % 32) / 2.0f);
	}

	std::uint8_t Key() const
	{
		return fKey;
	}

private:
	std::uint8_t fKey;
};


std::unique_ptr<OutlineFuzzItem>
MakeItem(std::uint8_t key)
{
	auto item = std::make_unique<OutlineFuzzItem>(key);
	if ((key & 0xe0) == 0xe0)
		item->SetEnabled(false);
	return item;
}


int
CompareKeys(const ngui::BListItem* a, const ngui::BListItem* b)
{
	return static_cast<int>(static_cast<const OutlineFuzzItem*>(a)->Key())
		- static_cast<int>(static_cast<const OutlineFuzzItem*>(b)->Key());
}


} // namespace


NGUI_FUZZ_TARGET(OutlineListView)
{
	// What is tested: An outline list view of 100 × 60 in a window,
	// changed by arbitrary bytes taken in pairs: an operation and its
	// argument, which adds items at levels 0 to 3 (at the end, anywhere,
	// under another item), removes items with their subitems, expands,
	// collapses, selects, deselects, swaps, replaces, moves and sorts
	// items, presses the selection keys and empties the list; items of 0
	// to 15.5 pixels, some collapsed, some disabled.
	// Success criteria: No crash, leak or sanitizer report; after every
	// operation the visible items are exactly those of the full list whose
	// superitems are all expanded, in its order; only visible items are
	// selected, and CurrentSelection() lists exactly them; the visible
	// items lie one below the other from 0.
	// Why it matters: The outline view keeps two lists and the ownership
	// of every item between them; when they drift, items are lost, shown
	// twice or freed while still listed.
	//
	// Given
	ngui::BWindow window({0, 0, 200, 100}, "fuzz");
	auto& list = static_cast<ngui::BOutlineListView&>(*window.AddChild(
		std::make_unique<ngui::BOutlineListView>(ngui::gfx::Rect{0, 0, 100, 60}, "outline",
			ngui::BListViewType::MultipleSelection)));

	for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
		const std::uint8_t op = data[i];
		const std::uint8_t arg = data[i + 1];
		const ngui::int32 full = list.FullListCountItems();
		const ngui::int32 fullIndex = full > 0 ? arg % full : 0;
		const ngui::int32 count = list.CountItems();
		const ngui::int32 index = count > 0 ? arg % count : 0;
		const ngui::int32 other = count > 0 ? (arg / 16) % count : 0;

		// When
		switch (op % 16) {
			case 0:
				(void)list.AddItem(MakeItem(arg));
				break;
			case 1:
				(void)list.AddItem(MakeItem(arg), (op / 16) % (full + 1));
				break;
			case 2:
				(void)list.AddUnder(MakeItem(arg), list.FullListItemAt((op / 16) % (full + 1)));
				break;
			case 3:
				(void)list.RemoveItem(fullIndex);
				break;
			case 4:
				list.Expand(list.FullListItemAt(fullIndex));
				break;
			case 5:
				list.Collapse(list.FullListItemAt(fullIndex));
				break;
			case 6:
				list.Select(index, (op & 0x10) != 0);
				break;
			case 7:
				list.Select(other < index ? other : index, other < index ? index : other,
					(op & 0x10) != 0);
				break;
			case 8:
				if ((op & 0x10) != 0)
					list.DeselectAll();
				else
					list.Deselect(index);
				break;
			case 9:
				(void)list.SwapItems(index, other);
				break;
			case 10:
			{
				std::unique_ptr<OutlineFuzzItem> item = MakeItem(arg);
				if ((op & 0x10) != 0 && item->IsEnabled())
					item->Select();
				(void)list.ReplaceItem(index, std::move(item));
				break;
			}
			case 11:
				(void)list.MoveItem(index, other);
				break;
			case 12:
				if ((op & 0x10) != 0)
					list.SortItems(CompareKeys);
				else
					list.SortItemsUnder(list.FullListItemAt(fullIndex), (op & 0x20) != 0,
						CompareKeys);
				break;
			case 13:
			{
				const char keys[] = {ngui::keys::kUpArrow, ngui::keys::kDownArrow,
					ngui::keys::kLeftArrow, ngui::keys::kRightArrow, ngui::keys::kHome,
					ngui::keys::kEnd};
				list.KeyDown(std::string(1, keys[arg % sizeof(keys)]));
				break;
			}
			case 14:
				(void)list.RemoveItems(fullIndex, (op / 16) % 4);
				break;
			case 15:
				if (arg == 0)
					list.MakeEmpty();
				else
					(void)list.CountItemsUnder(list.FullListItemAt(fullIndex), (op & 0x10) != 0);
				break;
		}

		// Then
		std::vector<const ngui::BListItem*> expected;
		std::vector<std::pair<ngui::uint32, bool>> ancestors;
		for (ngui::int32 j = 0; j < list.FullListCountItems(); j++) {
			const ngui::BListItem* item = list.FullListItemAt(j);
			while (!ancestors.empty() && ancestors.back().first >= item->OutlineLevel())
				ancestors.pop_back();
			const bool visible = ancestors.empty() || ancestors.back().second;
			ancestors.emplace_back(item->OutlineLevel(), visible && item->IsExpanded());
			if (visible)
				expected.push_back(item);
			else
				NGUI_FUZZ_CHECK(!item->IsSelected());
		}
		std::vector<const ngui::BListItem*> shown;
		std::vector<ngui::int32> selected;
		for (ngui::int32 j = 0; j < list.CountItems(); j++) {
			shown.push_back(list.ItemAt(j));
			if (list.ItemAt(j)->IsSelected())
				selected.push_back(j);
		}
		NGUI_FUZZ_CHECK(shown == expected);

		std::vector<ngui::int32> reported;
		for (ngui::int32 j = 0; list.CurrentSelection(j) >= 0 && j <= list.CountItems(); j++)
			reported.push_back(list.CurrentSelection(j));
		NGUI_FUZZ_CHECK(reported == selected);

		float top = 0;
		for (ngui::int32 j = 0; j < list.CountItems(); j++) {
			const ngui::gfx::Rect frame = list.ItemFrame(j);
			NGUI_FUZZ_CHECK(frame.y0 == top);
			top += std::ceil(list.ItemAt(j)->Height());
		}
	}
}
