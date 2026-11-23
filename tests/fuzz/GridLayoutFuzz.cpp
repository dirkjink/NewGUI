/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <ngui/GridLayout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/View.h>


namespace {


using namespace ngui;


// Reads the input byte by byte; 0 once it is used up.
class Bytes {
public:
	explicit Bytes(std::span<const std::uint8_t> data)
		:
		fData(data)
	{
	}

	int Next()
	{
		return fNext < fData.size() ? fData[fNext++] : 0;
	}

	bool AtEnd() const
	{
		return fNext >= fData.size();
	}

private:
	std::span<const std::uint8_t> fData;
	std::size_t fNext = 0;
};


bool
Overlap(const gfx::Rect& a, const gfx::Rect& b)
{
	return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}


} // namespace


NGUI_FUZZ_TARGET(GridLayout)
{
	// What is tested: A grid layout with spacings 0 to 7 in a view, filled
	// from the input with views at cells of up to 6 × 6 spanning up to 3
	// columns and rows, with minimum and maximum sizes, and column limits;
	// laid out at a size from the input, then at its minimum size.
	// Success criteria: No crash or sanitizer report; no view has a
	// negative size; at a size of at least the minimum every view lies in
	// the owner; views of different items never overlap.
	// Why it matters: Forms are grids built from application data; no
	// combination of cells and sizes may break the layout or put views on
	// top of each other.
	//
	// Given
	Bytes bytes(data);
	BView owner({0, 0, 100, 100}, "owner");
	auto* grid = static_cast<BGridLayout*>(owner.SetLayout(std::make_unique<BGridLayout>(
		(float)(bytes.Next() % 8), (float)(bytes.Next() % 8))));
	const float width = (float)(bytes.Next() * 2);
	const float height = (float)(bytes.Next() * 2);
	std::vector<BView*> views;
	while (!bytes.AtEnd()) {
		const int kind = bytes.Next() % 4;
		if (kind == 3) {
			const int32 column = bytes.Next() % 6;
			grid->SetMinColumnWidth(column, (float)(bytes.Next() % 64));
			grid->SetMaxColumnWidth(column, (float)(64 + bytes.Next()));
			continue;
		}
		const int32 column = bytes.Next() % 6;
		const int32 row = bytes.Next() % 6;
		const int32 columns = 1 + bytes.Next() % 3;
		const int32 rows = 1 + bytes.Next() % 3;
		BLayoutItem* item = grid->AddView(std::make_unique<BView>("v"), column, row,
			columns, rows);
		if (item == nullptr)
			continue;
		BView* view = item->View();
		const float minWidth = (float)(bytes.Next() % 80);
		const float minHeight = (float)(bytes.Next() % 80);
		view->SetExplicitMinSize({minWidth, minHeight});
		if (kind == 1) {
			view->SetExplicitMaxSize({minWidth + (float)bytes.Next(),
				minHeight + (float)bytes.Next()});
		}
		views.push_back(view);
	}

	// When / Then
	const gfx::Size min = owner.MinSize();
	for (const gfx::Size size : {gfx::Size{width, height}, min}) {
		owner.ResizeTo(size.w, size.h);
		owner.Layout(true);
		owner.Relayout();
		const bool fits = size.w >= min.w && size.h >= min.h;
		for (std::size_t i = 0; i < views.size(); i++) {
			const gfx::Rect frame = views[i]->Frame();
			NGUI_FUZZ_CHECK(frame.Width() >= 0 && frame.Height() >= 0);
			if (fits) {
				NGUI_FUZZ_CHECK(frame.x0 >= 0 && frame.y0 >= 0);
				NGUI_FUZZ_CHECK(frame.x1 <= size.w && frame.y1 <= size.h);
			}
			for (std::size_t k = 0; k < i; k++)
				NGUI_FUZZ_CHECK(!Overlap(frame, views[k]->Frame()));
		}
	}
}
