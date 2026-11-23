/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <string>
#include <vector>

#include <ngui/AbstractLayout.h>
#include <ngui/LayoutUtils.h>


namespace ngui::test {


// A layout for the tests of the layout machinery: every visible item is
// aligned in the whole layout area, on top of each other. Its minimum and
// preferred sizes are the largest of its items'; it records its hooks.
class StackLayout : public BAbstractLayout {
public:
	gfx::Size BaseMinSize() override
	{
		gfx::Size size = {0, 0};
		for (int32 i = 0; i < CountItems(); i++) {
			if (!ItemAt(i)->IsVisible())
				continue;
			const gfx::Size min = ItemAt(i)->MinSize();
			size = {std::max(size.w, min.w), std::max(size.h, min.h)};
		}
		return size;
	}

	gfx::Size BasePreferredSize() override
	{
		gfx::Size size = {0, 0};
		for (int32 i = 0; i < CountItems(); i++) {
			if (!ItemAt(i)->IsVisible())
				continue;
			const gfx::Size preferred = ItemAt(i)->PreferredSize();
			size = {std::max(size.w, preferred.w), std::max(size.h, preferred.h)};
		}
		return size;
	}

	int layouts = 0;
	int invalidations = 0;
	std::vector<std::string> events;

protected:
	void DoLayout() override
	{
		layouts++;
		for (int32 i = 0; i < CountItems(); i++) {
			if (ItemAt(i)->IsVisible())
				ItemAt(i)->AlignInFrame(LayoutArea());
		}
	}

	bool ItemAdded(BLayoutItem*, int32 atIndex) override
	{
		events.push_back("added " + std::to_string(atIndex));
		return true;
	}

	void ItemRemoved(BLayoutItem*, int32 fromIndex) override
	{
		events.push_back("removed " + std::to_string(fromIndex));
	}

	void LayoutInvalidated(bool) override
	{
		invalidations++;
	}
};


} // namespace ngui::test
