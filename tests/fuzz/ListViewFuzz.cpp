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
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/ListView.h>
#include <ngui/Window.h>


namespace {


// An item of a fixed height that draws nothing; its key orders it.
class FuzzItem : public ngui::BListItem {
public:
	FuzzItem(float height, std::uint8_t key)
		:
		fKey(key),
		fFixedHeight(height)
	{
	}

	void DrawItem(ngui::BView*, ngui::DrawContext&, ngui::gfx::Rect, const ngui::gfx::Color&,
		bool) override
	{
	}

	void Update(ngui::BView*, const ngui::TextSystem&) override
	{
		SetHeight(fFixedHeight);
	}

	std::uint8_t Key() const
	{
		return fKey;
	}

private:
	std::uint8_t fKey;
	float fFixedHeight;
};


std::unique_ptr<FuzzItem>
MakeItem(std::uint8_t byte)
{
	// heights 0 to 31 in quarter pixels, some disabled
	auto item = std::make_unique<FuzzItem>(static_cast<float>(byte % 128) / 4.0f, byte);
	if ((byte & 0x80) != 0)
		item->SetEnabled(false);
	return item;
}


} // namespace


NGUI_FUZZ_TARGET(ListView)
{
	// What is tested: A list view of 100 × 60 in a window, changed by
	// arbitrary bytes taken in pairs: an operation and its argument, which
	// adds, removes, moves, swaps, replaces, sorts and empties items,
	// selects, extends and deselects them, switches the list type and
	// presses the selection keys; items of 0 to 31.75 pixels, some
	// disabled.
	// Success criteria: No crash or sanitizer report; after every
	// operation CurrentSelection() lists exactly the selected items in
	// order; the items lie one below the other from 0, each as high as its
	// rounded-up height, and IndexOf() finds each with a height at its
	// middle.
	// Why it matters: The list keeps its first and last selected index
	// through every change; when they drift, selections are reported wrong
	// or items are read out of range.
	//
	// Given
	ngui::BWindow window({0, 0, 200, 100}, "fuzz");
	auto& list = static_cast<ngui::BListView&>(*window.AddChild(
		std::make_unique<ngui::BListView>(ngui::gfx::Rect{0, 0, 100, 60}, "list",
			ngui::BListViewType::MultipleSelection)));

	for (std::size_t i = 0; i + 1 < data.size(); i += 2) {
		const std::uint8_t op = data[i];
		const std::uint8_t arg = data[i + 1];
		const ngui::int32 count = list.CountItems();
		const ngui::int32 index = count > 0 ? arg % count : 0;
		const ngui::int32 other = count > 0 ? (arg / 16) % count : 0;

		// When
		switch (op % 16) {
			case 0:
			case 1:
				(void)list.AddItem(MakeItem(arg), op % 2 == 0 ? list.CountItems()
					: arg % (count + 1));
				break;
			case 2:
				(void)list.RemoveItem(index);
				break;
			case 3:
				list.Select(index, (op & 0x10) != 0);
				break;
			case 4:
				list.Select(other < index ? other : index, other < index ? index : other,
					(op & 0x10) != 0);
				break;
			case 5:
				list.Deselect(index);
				break;
			case 6:
				if ((op & 0x10) != 0)
					list.DeselectAll();
				else
					list.DeselectExcept(other < index ? other : index,
						other < index ? index : other);
				break;
			case 7:
				(void)list.MoveItem(index, other);
				break;
			case 8:
				(void)list.SwapItems(index, other);
				break;
			case 9:
			{
				std::unique_ptr<FuzzItem> item = MakeItem(arg);
				if ((op & 0x10) != 0 && item->IsEnabled())
					item->Select();
				(void)list.ReplaceItem(index, std::move(item));
				break;
			}
			case 10:
				(void)list.RemoveItems(index, other);
				break;
			case 11:
				list.SetListType((op & 0x10) != 0 ? ngui::BListViewType::SingleSelection
					: ngui::BListViewType::MultipleSelection);
				break;
			case 12:
			{
				const char keys[] = {ngui::keys::kUpArrow, ngui::keys::kDownArrow,
					ngui::keys::kHome, ngui::keys::kEnd, ngui::keys::kPageUp,
					ngui::keys::kPageDown};
				list.KeyDown(std::string(1, keys[arg % sizeof(keys)]));
				break;
			}
			case 13:
				list.SortItems([](const ngui::BListItem* a, const ngui::BListItem* b) {
					return static_cast<int>(static_cast<const FuzzItem*>(a)->Key())
						- static_cast<int>(static_cast<const FuzzItem*>(b)->Key());
				});
				break;
			case 14:
				if (arg == 0)
					list.MakeEmpty();
				else
					list.ScrollTo(index);
				break;
			case 15:
				list.ScrollToSelection();
				break;
		}

		// Then
		std::vector<ngui::int32> selected;
		for (ngui::int32 j = 0; j < list.CountItems(); j++) {
			if (list.ItemAt(j)->IsSelected())
				selected.push_back(j);
		}
		std::vector<ngui::int32> reported;
		for (ngui::int32 j = 0; list.CurrentSelection(j) >= 0
			&& j <= list.CountItems(); j++) {
			reported.push_back(list.CurrentSelection(j));
		}
		NGUI_FUZZ_CHECK(reported == selected);

		float top = 0;
		for (ngui::int32 j = 0; j < list.CountItems(); j++) {
			const ngui::gfx::Rect frame = list.ItemFrame(j);
			const float height = std::ceil(list.ItemAt(j)->Height());
			NGUI_FUZZ_CHECK(frame.y0 == top);
			NGUI_FUZZ_CHECK(frame.y1 == top + height);
			if (height > 0)
				NGUI_FUZZ_CHECK(list.IndexOf(ngui::gfx::Point{5, top + height / 2}) == j);
			top += height;
		}
	}
}
