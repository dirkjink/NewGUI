/*
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/SplitView.cpp (commit b1d497bb69),
 * without archiving and cursors (see SplitView.h).
 */


#include <ngui/SplitView.h>

#include <ngui/ControlLook.h>
#include <ngui/Cursor.h>
#include <ngui/Palette.h>

#include "SplitLayout.h"


namespace ngui {


BSplitView::BSplitView(BOrientation orientation, float spacing)
	:
	BView(std::string_view{}, BViewFlag::WillDraw | BViewFlag::FullUpdateOnResize
		| BViewFlag::InvalidateAfterLayout)
{
	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));
	auto layout = std::make_unique<BSplitLayout>(orientation, spacing);
	fSplitLayout = layout.get();
	BView::SetLayout(std::move(layout));
}


BSplitView::~BSplitView()
{
}


void
BSplitView::SetInsets(float left, float top, float right, float bottom)
{
	// NewGUI: the layout resolves spacing constants when it uses them.
	fSplitLayout->SetInsets(left, top, right, bottom);
}


void
BSplitView::SetInsets(float horizontal, float vertical)
{
	fSplitLayout->SetInsets(horizontal, vertical, horizontal, vertical);
}


void
BSplitView::SetInsets(float insets)
{
	fSplitLayout->SetInsets(insets, insets, insets, insets);
}


void
BSplitView::GetInsets(float* left, float* top, float* right, float* bottom) const
{
	fSplitLayout->GetInsets(left, top, right, bottom);
}


float
BSplitView::Spacing() const
{
	return fSplitLayout->Spacing();
}


void
BSplitView::SetSpacing(float spacing)
{
	fSplitLayout->SetSpacing(spacing);
}


BOrientation
BSplitView::Orientation() const
{
	return fSplitLayout->Orientation();
}


void
BSplitView::SetOrientation(BOrientation orientation)
{
	fSplitLayout->SetOrientation(orientation);
}


float
BSplitView::SplitterSize() const
{
	return fSplitLayout->SplitterSize();
}


void
BSplitView::SetSplitterSize(float size)
{
	fSplitLayout->SetSplitterSize(size);
}


int32
BSplitView::CountItems() const
{
	return fSplitLayout->CountItems();
}


float
BSplitView::ItemWeight(int32 index) const
{
	return fSplitLayout->ItemWeight(index);
}


float
BSplitView::ItemWeight(BLayoutItem* item) const
{
	return fSplitLayout->ItemWeight(item);
}


void
BSplitView::SetItemWeight(int32 index, float weight, bool invalidateLayout)
{
	fSplitLayout->SetItemWeight(index, weight, invalidateLayout);
}


void
BSplitView::SetItemWeight(BLayoutItem* item, float weight)
{
	fSplitLayout->SetItemWeight(item, weight);
}


bool
BSplitView::IsCollapsible(int32 index) const
{
	return fSplitLayout->IsCollapsible(index);
}


void
BSplitView::SetCollapsible(bool collapsible)
{
	fSplitLayout->SetCollapsible(collapsible);
}


void
BSplitView::SetCollapsible(int32 index, bool collapsible)
{
	fSplitLayout->SetCollapsible(index, collapsible);
}


void
BSplitView::SetCollapsible(int32 first, int32 last, bool collapsible)
{
	fSplitLayout->SetCollapsible(first, last, collapsible);
}


bool
BSplitView::IsItemCollapsed(int32 index) const
{
	return fSplitLayout->IsItemCollapsed(index);
}


void
BSplitView::SetItemCollapsed(int32 index, bool collapsed)
{
	fSplitLayout->SetItemCollapsed(index, collapsed);
}


BLayoutItem*
BSplitView::AddChild(std::unique_ptr<BView> child, float weight)
{
	return fSplitLayout->AddView(std::move(child), weight);
}


BLayoutItem*
BSplitView::AddChild(int32 index, std::unique_ptr<BView> child, float weight)
{
	return fSplitLayout->AddView(index, std::move(child), weight);
}


BLayoutItem*
BSplitView::AddChild(std::unique_ptr<BLayoutItem> child, float weight)
{
	return fSplitLayout->AddItem(std::move(child), weight);
}


BLayoutItem*
BSplitView::AddChild(int32 index, std::unique_ptr<BLayoutItem> child, float weight)
{
	return fSplitLayout->AddItem(index, std::move(child), weight);
}


void
BSplitView::AttachedToWindow()
{
	BView::AttachedToWindow();

	// AdoptParentColors()
	if (Parent() != nullptr && Parent()->ViewColor().has_value())
		SetViewColor(Parent()->ViewColor());
}


void
BSplitView::Draw(DrawContext& context)
{
	// draw the splitters
	int32 draggedSplitterIndex = fSplitLayout->DraggedSplitter();
	int32 count = fSplitLayout->CountItems();
	for (int32 i = 0; i < count - 1; i++) {
		gfx::Rect frame = fSplitLayout->SplitterItemFrame(i);
		DrawSplitter(context, frame, Orientation(), draggedSplitterIndex == i);
	}
}


void
BSplitView::MouseDown(gfx::Point where)
{
	SetMouseEventMask(BEventMask::Pointer);

	if (fSplitLayout->StartDraggingSplitter(where))
		Invalidate();
}


void
BSplitView::MouseUp(gfx::Point /*where*/)
{
	if (fSplitLayout->StopDraggingSplitter()) {
		Relayout();
		Invalidate();
	}
}


void
BSplitView::MouseMoved(gfx::Point where, BMouseTransit /*transit*/,
	const BMessage* /*message*/)
{
	BCursor cursor(BCursorID::SystemDefault);

	int32 splitterIndex = fSplitLayout->DraggedSplitter();

	if (splitterIndex >= 0 || fSplitLayout->IsAboveSplitter(where)) {
		if (Orientation() == BOrientation::Vertical)
			cursor = BCursor(BCursorID::ResizeNorthSouth);
		else
			cursor = BCursor(BCursorID::ResizeEastWest);
	}

	if (splitterIndex >= 0) {
		if (fSplitLayout->DragSplitter(where))
			Invalidate();
	}

	SetViewCursor(cursor);
}


BLayout*
BSplitView::SetLayout(std::unique_ptr<BLayout> /*layout*/)
{
	// not allowed
	return nullptr;
}


void
BSplitView::DrawSplitter(DrawContext& context, gfx::Rect frame, BOrientation orientation,
	bool pressed)
{
	// Haiku's _DrawDefaultSplitter()
	ControlStateFlags flags;
	if (pressed)
		flags |= ControlState::Activated;
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	Look().DrawSplitter(context, frame, base, orientation, flags, BorderFlags{});
}


} // namespace ngui
