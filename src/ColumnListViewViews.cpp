/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "ColumnListViewPrivate.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iterator>

#include <ngui/ControlLook.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>


namespace ngui {


using namespace std::chrono_literals;


namespace {


// Haiku's default double click distance and click speed (get_click_speed()),
// as BListView's
constexpr float kDoubleClickThreshold = 6.0f;
constexpr Clock::Time kClickSpeed = 500ms;

// The sort indicator: its size and the space right of it.
constexpr float kSortIndicatorSize = 7.0f;
constexpr float kSortIndicatorMargin = 4.0f;


int32
Buttons(const BView& view)
{
	const BMessage* current = view.Window() != nullptr ? view.Window()->CurrentMessage()
		: nullptr;
	return current != nullptr ? current->GetInt32("buttons", 0) : 0;
}


void
FillLine(gfx::Canvas& canvas, const gfx::Rect& rect, const gfx::Color& color)
{
	canvas.FillRect(rect, gfx::Paint{.color = color});
}


} // namespace


// #pragma mark - ColumnTitleView


ColumnTitleView::ColumnTitleView(BColumnListView* master)
	:
	BView({0, 0, 1, 1}, "titles", kFollowLeftTop, BViewFlag::WillDraw),
	fMaster(master)
{
	SetViewColor(std::nullopt);
}


void
ColumnTitleView::Draw(DrawContext& context)
{
	const gfx::Rect bounds = Bounds();
	gfx::Canvas& canvas = context.canvas;
	const gfx::Color base = fMaster->_Color(BColumnListViewColor::HeaderBackground,
		context.palette);
	const gfx::Color text = fMaster->_Color(BColumnListViewColor::HeaderText, context.palette);
	const gfx::Color line = fMaster->_Color(BColumnListViewColor::SeparatorLine,
		context.palette);
	const gfx::Color light = fMaster->_Color(BColumnListViewColor::SeparatorBorder,
		context.palette);

	gfx::Rect background = bounds;
	Look().DrawButtonBackground(context, background, base);

	float left = fMaster->fLatchWidth;
	for (const std::unique_ptr<BColumn>& column : fMaster->fColumns) {
		if (!column->fVisible)
			continue;

		const gfx::Rect rect = {left, bounds.y0, left + column->fWidth, bounds.y1};
		left += column->fWidth;
		if (rect.x1 <= bounds.x0 || rect.x0 >= bounds.x1)
			continue;

		if (column.get() == fPressed && fPressedInside) {
			gfx::Rect pressed = rect;
			Look().DrawButtonBackground(context, pressed, base, ControlState::Clicked);
		}

		gfx::Rect titleRect = rect;
		const auto sort = std::find(fMaster->fSortColumns.begin(), fMaster->fSortColumns.end(),
			column.get());
		if (fMaster->fSortingEnabled && sort != fMaster->fSortColumns.end()
			&& rect.Width() > kSortIndicatorSize + 2 * kSortIndicatorMargin) {
			// the first sort column darker than the others
			const float tint = sort == fMaster->fSortColumns.begin() ? kDarken4Tint
				: kDarken2Tint;
			const float x = rect.x1 - kSortIndicatorMargin - kSortIndicatorSize;
			const float y = std::floor(rect.y0 + (rect.Height() - kSortIndicatorSize) / 2);
			gfx::Rect arrow = {x, y, x + kSortIndicatorSize, y + kSortIndicatorSize};
			Look().DrawArrowShape(context, arrow, base,
				column->fSortAscending ? ArrowDirection::Up : ArrowDirection::Down, {}, tint);
			titleRect.x1 = x;
		}

		if (column->fShowHeading) {
			canvas.Save();
			canvas.ClipRect(titleRect);
			column->DrawTitle(this, context, titleRect, text);
			canvas.Restore();
		}

		FillLine(canvas, {rect.x1 - 1, rect.y0, rect.x1, rect.y1 - 1}, line);
		FillLine(canvas, {rect.x1, rect.y0, rect.x1 + 1, rect.y1 - 1}, light);
	}

	FillLine(canvas, {bounds.x0, bounds.y1 - 1, bounds.x1, bounds.y1}, line);
}


void
ColumnTitleView::MouseDown(gfx::Point where)
{
	fPressed = fMaster->ColumnAt(gfx::Point{where.x, 0});
	if (fPressed == nullptr)
		return BView::MouseDown(where);

	fPressedInside = true;
	SetMouseEventMask(BEventMask::Pointer);
	Invalidate();
}


void
ColumnTitleView::MouseUp(gfx::Point where)
{
	if (fPressed == nullptr)
		return BView::MouseUp(where);

	BColumn* column = fPressed;
	const bool inside = fPressedInside;
	fPressed = nullptr;
	fPressedInside = false;
	Invalidate();
	if (inside)
		fMaster->_TitleClicked(column, fMaster->_Modifiers());
}


void
ColumnTitleView::MouseMoved(gfx::Point where, BMouseTransit transit,
	const BMessage* dragMessage)
{
	if (fPressed == nullptr)
		return BView::MouseMoved(where, transit, dragMessage);

	const gfx::Rect bounds = Bounds();
	const bool inside = where.y >= bounds.y0 && where.y < bounds.y1
		&& fMaster->ColumnAt(gfx::Point{where.x, 0}) == fPressed;
	if (inside != fPressedInside) {
		fPressedInside = inside;
		Invalidate();
	}
}


BColumn*
ColumnTitleView::PressedColumn() const
{
	return fPressed;
}


void
ColumnTitleView::ForgetColumn(const BColumn* column)
{
	if (fPressed == column) {
		fPressed = nullptr;
		fPressedInside = false;
	}
}


// #pragma mark - ColumnRowView


ColumnRowView::ColumnRowView(BColumnListView* master)
	:
	BView({0, 0, 1, 1}, "rows", kFollowLeftTop, BViewFlag::WillDraw | BViewFlag::FrameEvents),
	fMaster(master)
{
	SetViewColor(std::nullopt);
}


void
ColumnRowView::Draw(DrawContext& context)
{
	const gfx::Rect bounds = Bounds();
	gfx::Canvas& canvas = context.canvas;
	const Palette& palette = context.palette;
	FillLine(canvas, bounds, fMaster->_Color(BColumnListViewColor::Background, palette));

	const std::vector<BColumnListView::VisibleRow>& rows = fMaster->_VisibleRows();
	if (rows.empty())
		return;

	const bool highlighted = _Highlighted();
	const gfx::Color selection = fMaster->_Color(highlighted
		? BColumnListViewColor::Selection : BColumnListViewColor::NonFocusSelection, palette);
	const gfx::Color divider = fMaster->_Color(BColumnListViewColor::RowDivider, palette);
	const gfx::Color text = fMaster->_Color(BColumnListViewColor::Text, palette);
	const gfx::Color selectedText = fMaster->_Color(BColumnListViewColor::SelectionText,
		palette);
	const gfx::Color focus = palette.Get(BColorWhich::KeyboardNavigation);

	// the first row reaching into the bounds
	auto first = std::upper_bound(rows.begin(), rows.end(), bounds.y0,
		[](float value, const BColumnListView::VisibleRow& row) { return value < row.top; });
	if (first != rows.begin())
		first = std::prev(first);

	for (auto shown = first; shown != rows.end() && shown->top < bounds.y1; ++shown) {
		BRow* row = shown->row;
		const gfx::Rect rowRect = {bounds.x0, shown->top, bounds.x1,
			shown->top + row->fHeight};
		if (rowRect.y1 <= bounds.y0 || rowRect.IsEmpty())
			continue;

		if (row->fIsSelected)
			FillLine(canvas, rowRect, selection);
		FillLine(canvas, {rowRect.x0, rowRect.y1 - 1, rowRect.x1, rowRect.y1}, divider);

		if (row->HasLatch() && fMaster->fLatchWidth > 0) {
			BLatchType type = row->fIsExpanded ? BLatchType::Open : BLatchType::Closed;
			if (row == fLatchRow && fLatchPressed)
				type = BLatchType::Pressed;
			fMaster->DrawLatch(this, context, fMaster->_LatchRect(*shown), type, row);
		}

		for (const std::unique_ptr<BColumn>& column : fMaster->fColumns) {
			if (!column->fVisible)
				continue;
			const gfx::Rect fieldRect = fMaster->_FieldRect(*shown, column.get());
			if (fieldRect.IsEmpty() || fieldRect.x1 <= bounds.x0 || fieldRect.x0 >= bounds.x1)
				continue;
			BField* field = row->GetField(column->fFieldID);
			if (field == nullptr || !column->AcceptsField(field))
				continue;

			canvas.Save();
			canvas.ClipRect(fieldRect);
			column->DrawField(field, this, context, fieldRect,
				row->fIsSelected ? selectedText : text);
			canvas.Restore();
		}

		if (row == fMaster->fFocusRow && highlighted) {
			FillLine(canvas, {rowRect.x0, rowRect.y0, rowRect.x1, rowRect.y0 + 1}, focus);
			FillLine(canvas, {rowRect.x0, rowRect.y1 - 1, rowRect.x1, rowRect.y1}, focus);
			FillLine(canvas, {rowRect.x0, rowRect.y0, rowRect.x0 + 1, rowRect.y1}, focus);
			FillLine(canvas, {rowRect.x1 - 1, rowRect.y0, rowRect.x1, rowRect.y1}, focus);
		}
	}
}


void
ColumnRowView::MouseDown(gfx::Point where)
{
	const BModifierFlags modifiers = fMaster->_Modifiers();
	if (!fMaster->IsFocus())
		fMaster->MakeFocus(true);

	const BColumnListView::VisibleRow* shown = fMaster->_VisibleRowAt(where.y);
	if (shown != nullptr && shown->row->HasLatch() && fMaster->fLatchWidth > 0
		&& fMaster->_LatchRect(*shown).Contains(where)) {
		fLatchRow = shown->row;
		fLatchPressed = true;
		SetMouseEventMask(BEventMask::Pointer);
		Invalidate();
		return;
	}

	if (shown == nullptr) {
		if (!modifiers.Has(BModifier::Shift) && !modifiers.Has(BModifier::Command))
			fMaster->DeselectAll();
		return BView::MouseDown(where);
	}

	BRow* row = shown->row;
	BColumn* column = fMaster->ColumnAt(where);
	if (column != nullptr && column->fWantsEvents) {
		fEventColumn = column;
		fEventRow = row;
		column->MouseDown(fMaster, row, row->GetField(column->fFieldID),
			fMaster->_FieldRect(*shown, column), where, Buttons(*this));
		SetMouseEventMask(BEventMask::Pointer);
	}

	const Clock::Time now = Clock::Default().Now();
	const bool doubleClick = fLastClickTime.has_value() && now - *fLastClickTime < kClickSpeed
		&& fLastClickRow == row && std::fabs(where.x - fLastClickPoint.x) < kDoubleClickThreshold
		&& std::fabs(where.y - fLastClickPoint.y) < kDoubleClickThreshold;
	if (doubleClick) {
		fLastClickTime.reset();
		fLastClickRow = nullptr;
		fMaster->ItemInvoked();
		return;
	}

	fLastClickTime = now;
	fLastClickRow = row;
	fLastClickPoint = where;
	fMaster->_ClickRow(row, modifiers);
}


void
ColumnRowView::MouseUp(gfx::Point where)
{
	if (fLatchRow != nullptr) {
		BRow* row = fLatchRow;
		const bool inside = fLatchPressed;
		fLatchRow = nullptr;
		fLatchPressed = false;
		Invalidate();
		if (inside)
			fMaster->ExpandOrCollapse(row, !row->fIsExpanded);
		return;
	}

	if (fEventColumn != nullptr) {
		BColumn* column = fEventColumn;
		BRow* row = fEventRow;
		fEventColumn = nullptr;
		fEventRow = nullptr;
		column->MouseUp(fMaster, row, row->GetField(column->fFieldID));
		return;
	}

	BView::MouseUp(where);
}


void
ColumnRowView::MouseMoved(gfx::Point where, BMouseTransit transit,
	const BMessage* dragMessage)
{
	if (fLatchRow != nullptr) {
		const int32 index = fMaster->_VisibleIndexOf(fLatchRow);
		const bool inside = index >= 0 && fMaster->_LatchRect(
			fMaster->_VisibleRows()[static_cast<std::size_t>(index)]).Contains(where);
		if (inside != fLatchPressed) {
			fLatchPressed = inside;
			Invalidate();
		}
		return;
	}

	if (fEventColumn != nullptr) {
		fEventColumn->MouseMoved(fMaster, fEventRow, fEventRow->GetField(fEventColumn->fFieldID),
			fMaster->GetFieldRect(fEventRow, fEventColumn), where, Buttons(*this), transit);
		return;
	}

	if (fMaster->fMouseTracking) {
		const BColumnListView::VisibleRow* shown = fMaster->_VisibleRowAt(where.y);
		BColumn* column = fMaster->ColumnAt(where);
		if (shown != nullptr && column != nullptr && column->fWantsEvents) {
			column->MouseMoved(fMaster, shown->row, shown->row->GetField(column->fFieldID),
				fMaster->_FieldRect(*shown, column), where, Buttons(*this), transit);
		}
	}

	BView::MouseMoved(where, transit, dragMessage);
}


void
ColumnRowView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	fMaster->_FixScrollBars();
}


void
ColumnRowView::ForgetRow(const BRow* row)
{
	if (row == nullptr || fLatchRow == row) {
		fLatchRow = nullptr;
		fLatchPressed = false;
	}
	if (row == nullptr || fEventRow == row) {
		fEventRow = nullptr;
		fEventColumn = nullptr;
	}
	if (row == nullptr || fLastClickRow == row) {
		fLastClickRow = nullptr;
		fLastClickTime.reset();
	}
}


void
ColumnRowView::ForgetColumn(const BColumn* column)
{
	if (fEventColumn == column) {
		fEventColumn = nullptr;
		fEventRow = nullptr;
	}
}


bool
ColumnRowView::_Highlighted() const
{
	return fMaster->IsFocus() && Window() != nullptr && Window()->IsActive();
}


// #pragma mark - ColumnScrollBar


ColumnScrollBar::ColumnScrollBar(BView* target, BView* titles)
	:
	BScrollBar(gfx::Rect{0, 0, 14, 14}, "horizontal", target, 0.0f, 0.0f,
		BOrientation::Horizontal),
	fTitles(titles)
{
}


void
ColumnScrollBar::ValueChanged(float newValue)
{
	BScrollBar::ValueChanged(newValue);
	fTitles->ScrollTo({Value(), 0});
}


} // namespace ngui
