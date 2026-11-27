/*
 * Copyright 2010, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/GroupView.cpp, GridView.cpp and
 * CardView.cpp (commit b1d497bb69), without archiving. AdoptSystemColors() is the panel
 * background as view color.
 */


#include <ngui/CardView.h>
#include <ngui/GridView.h>
#include <ngui/GroupView.h>

#include <ngui/Palette.h>


namespace ngui {


BGroupView::BGroupView(BOrientation orientation, float spacing)
	:
	BGroupView("", orientation, spacing)
{
}


BGroupView::BGroupView(std::string_view name, BOrientation orientation,
	float spacing)
	:
	BView(name, BViewFlags())
{
	SetViewUIColor(BColorWhich::PanelBackground);
	BView::SetLayout(std::make_unique<BGroupLayout>(orientation, spacing));
}


BGroupView::~BGroupView()
{
}


BLayout*
BGroupView::SetLayout(std::unique_ptr<BLayout> layout)
{
	// only BGroupLayouts are allowed
	if (dynamic_cast<BGroupLayout*>(layout.get()) == nullptr)
		return nullptr;

	return BView::SetLayout(std::move(layout));
}


BGroupLayout*
BGroupView::GroupLayout() const
{
	return dynamic_cast<BGroupLayout*>(GetLayout());
}


// #pragma mark - BGridView


BGridView::BGridView(float horizontalSpacing, float verticalSpacing)
	:
	BGridView("", horizontalSpacing, verticalSpacing)
{
}


BGridView::BGridView(std::string_view name, float horizontalSpacing,
	float verticalSpacing)
	:
	BView(name, BViewFlags())
{
	SetViewUIColor(BColorWhich::PanelBackground);
	BView::SetLayout(std::make_unique<BGridLayout>(horizontalSpacing,
		verticalSpacing));
}


BGridView::~BGridView()
{
}


BLayout*
BGridView::SetLayout(std::unique_ptr<BLayout> layout)
{
	// only BGridLayouts are allowed
	if (dynamic_cast<BGridLayout*>(layout.get()) == nullptr)
		return nullptr;

	return BView::SetLayout(std::move(layout));
}


BGridLayout*
BGridView::GridLayout() const
{
	return dynamic_cast<BGridLayout*>(GetLayout());
}


// #pragma mark - BCardView


BCardView::BCardView(std::string_view name)
	:
	BView(name, BViewFlags())
{
	SetViewUIColor(BColorWhich::PanelBackground);
	BView::SetLayout(std::make_unique<BCardLayout>());
}


BCardView::~BCardView()
{
}


BLayout*
BCardView::SetLayout(std::unique_ptr<BLayout> layout)
{
	if (dynamic_cast<BCardLayout*>(layout.get()) == nullptr)
		return nullptr;

	return BView::SetLayout(std::move(layout));
}


BCardLayout*
BCardView::CardLayout() const
{
	return static_cast<BCardLayout*>(GetLayout());
}


} // namespace ngui
