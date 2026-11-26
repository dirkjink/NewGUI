/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ColumnListView.h>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <unordered_set>
#include <utility>

#include <ngui/Accessibility.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/Palette.h>
#include <ngui/ScrollBar.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "ColumnListViewPrivate.h"


namespace ngui {


namespace {


// The space left of text in a field (as BTitledColumn::kTextMargin).
constexpr float kTextMargin = 8.0f;

// What the titles add to a line of the plain font.
constexpr float kTitleExtra = 6.0f;

// The rows PreferredSize() makes room for.
constexpr float kPreferredRows = 8.0f;

// How much darker than the background the line below a row is.
constexpr float kRowDividerTint = 1.07f;


// A line of the plain font and 4 pixels, as BStringItem's.
float
DefaultRowHeight()
{
	const FontHeight height = TextSystem::Default().Height(FontRole::Plain);
	return std::ceil(height.ascent) + std::ceil(height.descent) + std::ceil(height.leading) + 4;
}


float
BorderSize(BBorderStyle border)
{
	switch (border) {
		case BBorderStyle::Fancy:
			return 2.0f;
		case BBorderStyle::Plain:
			return 1.0f;
		case BBorderStyle::None:
			break;
	}
	return 0.0f;
}


// Whether row is root or lies below it.
bool
IsInSubtree(const BRow* row, const BRow* root, BRow* const BRow::*parent)
{
	for (const BRow* current = row; current != nullptr; current = current->*parent) {
		if (current == root)
			return true;
	}
	return false;
}


} // namespace


float
ColumnTextBaseline(const gfx::Rect& rect, const TextSystem& text)
{
	const FontHeight height = text.Height(FontRole::Plain);
	return std::round(rect.y0 + (rect.Height() - height.ascent - height.descent) / 2
		+ height.ascent);
}


// #pragma mark - BField


BField::BField()
{
}


BField::~BField()
{
}


// #pragma mark - BRow


BRow::BRow()
	:
	fHeight(DefaultRowHeight())
{
}


BRow::BRow(float height)
	:
	fHeight(std::max(height, 0.0f))
{
}


BRow::~BRow()
{
}


bool
BRow::HasLatch() const
{
	return !fChildren.empty();
}


int32
BRow::CountFields() const
{
	return static_cast<int32>(fFields.size());
}


BField*
BRow::GetField(int32 logicalFieldIndex)
{
	if (logicalFieldIndex < 0 || logicalFieldIndex >= CountFields())
		return nullptr;
	return fFields[static_cast<std::size_t>(logicalFieldIndex)].get();
}


const BField*
BRow::GetField(int32 logicalFieldIndex) const
{
	if (logicalFieldIndex < 0 || logicalFieldIndex >= CountFields())
		return nullptr;
	return fFields[static_cast<std::size_t>(logicalFieldIndex)].get();
}


void
BRow::SetField(std::unique_ptr<BField> field, int32 logicalFieldIndex)
{
	if (logicalFieldIndex < 0)
		return;

	const auto index = static_cast<std::size_t>(logicalFieldIndex);
	if (index >= fFields.size())
		fFields.resize(index + 1);
	fFields[index] = std::move(field);
	Invalidate();
}


float
BRow::Height() const
{
	return fHeight;
}


bool
BRow::IsExpanded() const
{
	return fIsExpanded;
}


bool
BRow::IsSelected() const
{
	return fIsSelected;
}


void
BRow::Invalidate()
{
	if (fList != nullptr)
		fList->InvalidateRow(this);
}


// #pragma mark - BColumn


BColumn::BColumn(float width, float minWidth, float maxWidth, BHorizontalAlignment alignment)
	:
	fMinWidth(std::max(minWidth, 0.0f)),
	fMaxWidth(std::max(maxWidth, std::max(minWidth, 0.0f))),
	fAlignment(alignment)
{
	fWidth = std::clamp(width, fMinWidth, fMaxWidth);
}


BColumn::~BColumn()
{
}


float
BColumn::Width() const
{
	return fWidth;
}


void
BColumn::SetWidth(float width)
{
	width = std::clamp(width, fMinWidth, fMaxWidth);
	if (width == fWidth)
		return;

	fWidth = width;
	if (fList != nullptr)
		fList->_RowsChanged();
}


float
BColumn::MinWidth() const
{
	return fMinWidth;
}


float
BColumn::MaxWidth() const
{
	return fMaxWidth;
}


void
BColumn::DrawTitle(BView* /*owner*/, DrawContext& /*context*/, gfx::Rect /*rect*/,
	const gfx::Color& /*textColor*/)
{
}


void
BColumn::DrawField(BField* /*field*/, BView* /*owner*/, DrawContext& /*context*/,
	gfx::Rect /*rect*/, const gfx::Color& /*textColor*/)
{
}


int
BColumn::CompareFields(BField* /*field1*/, BField* /*field2*/)
{
	return 0;
}


void
BColumn::MouseMoved(BColumnListView* /*parent*/, BRow* /*row*/, BField* /*field*/,
	gfx::Rect /*fieldRect*/, gfx::Point /*point*/, int32 /*buttons*/, BMouseTransit /*transit*/)
{
}


void
BColumn::MouseDown(BColumnListView* /*parent*/, BRow* /*row*/, BField* /*field*/,
	gfx::Rect /*fieldRect*/, gfx::Point /*point*/, int32 /*buttons*/)
{
}


void
BColumn::MouseUp(BColumnListView* /*parent*/, BRow* /*row*/, BField* /*field*/)
{
}


void
BColumn::GetColumnName(std::string* into) const
{
	if (into != nullptr)
		into->clear();
}


void
BColumn::GetFieldText(BField* /*field*/, std::string* into) const
{
	if (into != nullptr)
		into->clear();
}


float
BColumn::GetPreferredWidth(BField* /*field*/, BView* /*parent*/) const
{
	return fWidth;
}


bool
BColumn::IsVisible() const
{
	return fVisible;
}


void
BColumn::SetVisible(bool visible)
{
	if (fList != nullptr)
		fList->SetColumnVisible(this, visible);
	else
		fVisible = visible;
}


bool
BColumn::WantsEvents() const
{
	return fWantsEvents;
}


void
BColumn::SetWantsEvents(bool wants)
{
	fWantsEvents = wants;
}


bool
BColumn::ShowHeading() const
{
	return fShowHeading;
}


void
BColumn::SetShowHeading(bool show)
{
	fShowHeading = show;
	if (fList != nullptr)
		fList->_RowsChanged();
}


BHorizontalAlignment
BColumn::Alignment() const
{
	return fAlignment;
}


void
BColumn::SetAlignment(BHorizontalAlignment alignment)
{
	fAlignment = alignment;
	if (fList != nullptr)
		fList->_RowsChanged();
}


int32
BColumn::LogicalFieldNum() const
{
	return fFieldID;
}


bool
BColumn::AcceptsField(const BField* /*field*/) const
{
	return true;
}


// #pragma mark - BColumnListView


BColumnListView::BColumnListView(gfx::Rect frame, std::string_view name,
	BResizingMode resizingMode, BViewFlags flags, BBorderStyle border,
	bool showHorizontalScrollBar)
	:
	BView(frame, name, resizingMode, flags | BViewFlag::WillDraw | BViewFlag::FrameEvents)
{
	_Init(border, showHorizontalScrollBar);
}


BColumnListView::BColumnListView(std::string_view name, BViewFlags flags, BBorderStyle border,
	bool showHorizontalScrollBar)
	:
	BView(name, flags | BViewFlag::WillDraw | BViewFlag::FrameEvents)
{
	_Init(border, showHorizontalScrollBar);
}


BColumnListView::~BColumnListView()
{
	// the child views go with BView; they must not look at the rows then
	fFocusRow = nullptr;
	fAnchorRow = nullptr;
	if (fRowView != nullptr)
		fRowView->ForgetRow(nullptr);
}


// #pragma mark - interaction


void
BColumnListView::ExpandOrCollapse(BRow* row, bool expand)
{
	if (row == nullptr || row->fIsExpanded == expand)
		return;

	row->fIsExpanded = expand;
	if (!_Owns(row))
		return;

	bool selectionChanged = false;
	if (!expand) {
		selectionChanged = _DeselectSubtree(row, false);
		if (fFocusRow != nullptr && fFocusRow != row
			&& IsInSubtree(fFocusRow, row, &BRow::fParent)) {
			fFocusRow = row;
		}
		if (fAnchorRow != nullptr && fAnchorRow != row
			&& IsInSubtree(fAnchorRow, row, &BRow::fParent)) {
			fAnchorRow = row;
		}
	}

	_RowsChanged();
	if (selectionChanged)
		_NotifySelection();
}


std::expected<void, Error>
BColumnListView::Invoke(const BMessage* message)
{
	if (message == nullptr)
		message = Message();
	if (message == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to send");

	BMessage clone = *message;
	(void)clone.AddInt64("when", Clock::Default().Now().count());
	(void)clone.AddPointer("source", this);
	return BInvoker::Invoke(&clone);
}


void
BColumnListView::ItemInvoked()
{
	(void)Invoke();
}


void
BColumnListView::SetInvocationMessage(std::unique_ptr<BMessage> message)
{
	SetMessage(std::move(message));
}


BMessage*
BColumnListView::InvocationMessage() const
{
	return Message();
}


uint32
BColumnListView::InvocationCommand() const
{
	return Command();
}


BRow*
BColumnListView::FocusRow() const
{
	return fFocusRow;
}


void
BColumnListView::SetFocusRow(int32 index, bool select)
{
	SetFocusRow(RowAt(index), select);
}


void
BColumnListView::SetFocusRow(BRow* row, bool select)
{
	if (row != nullptr && _VisibleIndexOf(row) < 0)
		return;

	fFocusRow = row;
	fAnchorRow = row;
	fRowView->Invalidate();
	if (select && row != nullptr && _SelectOnly(row))
		_NotifySelection();
}


void
BColumnListView::SetMouseTrackingEnabled(bool enabled)
{
	fMouseTracking = enabled;
}


// #pragma mark - selection


BListViewType
BColumnListView::SelectionMode() const
{
	return fSelectionMode;
}


void
BColumnListView::SetSelectionMode(BListViewType mode)
{
	fSelectionMode = mode;
	if (mode == BListViewType::SingleSelection) {
		BRow* first = CurrentSelection();
		if (first != nullptr && _SelectOnly(first))
			_NotifySelection();
	}
}


void
BColumnListView::Deselect(BRow* row)
{
	if (row == nullptr || !_Owns(row) || !row->fIsSelected)
		return;

	row->fIsSelected = false;
	_NotifySelection();
}


void
BColumnListView::AddToSelection(BRow* row)
{
	if (row == nullptr || _VisibleIndexOf(row) < 0 || row->fIsSelected)
		return;

	if (fSelectionMode == BListViewType::SingleSelection)
		(void)_SelectOnly(row);
	else
		row->fIsSelected = true;
	_NotifySelection();
}


void
BColumnListView::DeselectAll()
{
	if (_SelectOnly(nullptr))
		_NotifySelection();
}


BRow*
BColumnListView::CurrentSelection(BRow* lastSelected) const
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	std::size_t start = 0;
	if (lastSelected != nullptr) {
		const int32 index = _VisibleIndexOf(lastSelected);
		if (index < 0)
			return nullptr;
		start = static_cast<std::size_t>(index) + 1;
	}

	for (std::size_t i = start; i < rows.size(); i++) {
		if (rows[i].row->fIsSelected)
			return rows[i].row;
	}
	return nullptr;
}


void
BColumnListView::SelectionChanged()
{
	if (fSelectionMessage != nullptr)
		(void)Invoke(fSelectionMessage.get());
}


void
BColumnListView::SetSelectionMessage(std::unique_ptr<BMessage> message)
{
	fSelectionMessage = std::move(message);
}


BMessage*
BColumnListView::SelectionMessage() const
{
	return fSelectionMessage.get();
}


uint32
BColumnListView::SelectionCommand() const
{
	return fSelectionMessage != nullptr ? fSelectionMessage->what : 0;
}


// #pragma mark - sorting


void
BColumnListView::SetSortingEnabled(bool enabled)
{
	if (fSortingEnabled == enabled)
		return;

	fSortingEnabled = enabled;
	if (enabled)
		_SortAll();
	else
		_RowsChanged();
}


bool
BColumnListView::SortingEnabled() const
{
	return fSortingEnabled;
}


void
BColumnListView::SetSortColumn(BColumn* column, bool add, bool ascending)
{
	if (column == nullptr || column->fList != this)
		return;

	if (!add)
		fSortColumns.clear();
	column->fSortAscending = ascending;
	if (std::find(fSortColumns.begin(), fSortColumns.end(), column) == fSortColumns.end())
		fSortColumns.push_back(column);

	_SortAll();
}


void
BColumnListView::ClearSortColumns()
{
	fSortColumns.clear();
	_RowsChanged();
}


int32
BColumnListView::CountSortColumns() const
{
	return static_cast<int32>(fSortColumns.size());
}


BColumn*
BColumnListView::SortColumnAt(int32 index) const
{
	if (index < 0 || index >= CountSortColumns())
		return nullptr;
	return fSortColumns[static_cast<std::size_t>(index)];
}


bool
BColumnListView::IsSortAscending(const BColumn* column) const
{
	return column != nullptr && column->fSortAscending;
}


// #pragma mark - status view


void
BColumnListView::AddStatusView(std::unique_ptr<BView> view)
{
	if (view == nullptr)
		return;

	(void)RemoveStatusView();
	view->SetResizingMode(kFollowLeftTop);
	fStatusView = AddChild(std::move(view));
	_LayoutChildren();
}


std::unique_ptr<BView>
BColumnListView::RemoveStatusView()
{
	if (fStatusView == nullptr)
		return nullptr;

	std::unique_ptr<BView> view = RemoveChild(fStatusView);
	fStatusView = nullptr;
	_LayoutChildren();
	return view;
}


// #pragma mark - columns


void
BColumnListView::AddColumn(std::unique_ptr<BColumn> column, int32 logicalFieldIndex)
{
	if (column == nullptr || logicalFieldIndex < 0 || column->fList != nullptr)
		return;

	column->fFieldID = logicalFieldIndex;
	column->fList = this;
	fColumns.push_back(std::move(column));
	_RowsChanged();
}


void
BColumnListView::MoveColumn(BColumn* column, int32 index)
{
	const auto found = std::find_if(fColumns.begin(), fColumns.end(),
		[column](const std::unique_ptr<BColumn>& each) { return each.get() == column; });
	if (found == fColumns.end())
		return;

	const auto from = static_cast<std::size_t>(std::distance(fColumns.begin(), found));
	const auto to = static_cast<std::size_t>(
		std::clamp(index, 0, static_cast<int32>(fColumns.size()) - 1));
	std::unique_ptr<BColumn> moved = std::move(fColumns[from]);
	fColumns.erase(fColumns.begin() + static_cast<std::ptrdiff_t>(from));
	fColumns.insert(fColumns.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));
	_RowsChanged();
}


std::unique_ptr<BColumn>
BColumnListView::RemoveColumn(BColumn* column)
{
	const auto found = std::find_if(fColumns.begin(), fColumns.end(),
		[column](const std::unique_ptr<BColumn>& each) { return each.get() == column; });
	if (found == fColumns.end())
		return nullptr;

	std::unique_ptr<BColumn> removed = std::move(*found);
	fColumns.erase(found);
	std::erase(fSortColumns, column);
	fTitleView->ForgetColumn(column);
	fRowView->ForgetColumn(column);
	removed->fList = nullptr;
	_RowsChanged();
	return removed;
}


int32
BColumnListView::CountColumns() const
{
	return static_cast<int32>(fColumns.size());
}


BColumn*
BColumnListView::ColumnAt(int32 index) const
{
	if (index < 0 || index >= CountColumns())
		return nullptr;
	return fColumns[static_cast<std::size_t>(index)].get();
}


BColumn*
BColumnListView::ColumnAt(gfx::Point point) const
{
	float left = fLatchWidth;
	for (const std::unique_ptr<BColumn>& column : fColumns) {
		if (!column->fVisible)
			continue;
		if (point.x >= left && point.x < left + column->fWidth)
			return column.get();
		left += column->fWidth;
	}
	return nullptr;
}


void
BColumnListView::SetColumnVisible(BColumn* column, bool visible)
{
	if (column == nullptr)
		return;
	if (column->fList != this) {
		column->fVisible = visible;
		return;
	}
	if (column->fVisible == visible)
		return;

	column->fVisible = visible;
	_RowsChanged();
}


void
BColumnListView::SetColumnVisible(int32 index, bool visible)
{
	SetColumnVisible(ColumnAt(index), visible);
}


bool
BColumnListView::IsColumnVisible(int32 index) const
{
	const BColumn* column = ColumnAt(index);
	return column != nullptr && column->fVisible;
}


void
BColumnListView::SetColumnFlags(BColumnFlags flags)
{
	fColumnFlags = flags;
}


BColumnFlags
BColumnListView::ColumnFlags() const
{
	return fColumnFlags;
}


void
BColumnListView::ResizeColumnToPreferred(int32 index)
{
	BColumn* column = ColumnAt(index);
	if (column == nullptr)
		return;

	float width = column->fShowHeading ? column->GetPreferredWidth(nullptr, fRowView) : 0.0f;
	const bool indented = column == _FirstVisibleColumn();

	// all rows, also hidden ones: expanding must not cut fields off
	struct Pending {
		BRow*				row;
		int32				level;
	};
	std::vector<Pending> pending;
	for (std::unique_ptr<BRow>& row : fRows)
		pending.push_back({row.get(), 0});
	while (!pending.empty()) {
		const Pending current = pending.back();
		pending.pop_back();
		BField* field = current.row->GetField(column->fFieldID);
		if (field != nullptr && column->AcceptsField(field)) {
			float fieldWidth = column->GetPreferredWidth(field, fRowView);
			if (indented)
				fieldWidth += static_cast<float>(current.level) * fLatchWidth;
			width = std::max(width, fieldWidth);
		}
		for (std::unique_ptr<BRow>& child : current.row->fChildren)
			pending.push_back({child.get(), current.level + 1});
	}

	column->SetWidth(width);
}


void
BColumnListView::ResizeAllColumnsToPreferred()
{
	for (int32 i = 0; i < CountColumns(); i++)
		ResizeColumnToPreferred(i);
}


// #pragma mark - rows


BRow*
BColumnListView::RowAt(int32 index, BRow* parent) const
{
	if (parent != nullptr && !_Owns(parent))
		return nullptr;

	const std::vector<std::unique_ptr<BRow>>& rows = *_Siblings(parent);
	if (index < 0 || index >= static_cast<int32>(rows.size()))
		return nullptr;
	return rows[static_cast<std::size_t>(index)].get();
}


BRow*
BColumnListView::RowAt(gfx::Point point) const
{
	const VisibleRow* row = _VisibleRowAt(point.y);
	return row != nullptr ? row->row : nullptr;
}


bool
BColumnListView::GetRowRect(const BRow* row, gfx::Rect* rect) const
{
	const int32 index = _VisibleIndexOf(row);
	if (index < 0)
		return false;

	if (rect != nullptr) {
		const VisibleRow& shown = _VisibleRows()[static_cast<std::size_t>(index)];
		*rect = {0, shown.top, std::max(_ContentWidth(), fRowView->Bounds().x1),
			shown.top + row->fHeight};
	}
	return true;
}


bool
BColumnListView::FindParent(const BRow* row, BRow** parent, bool* isVisible) const
{
	if (row == nullptr || !_Owns(row) || row->fParent == nullptr)
		return false;

	if (parent != nullptr)
		*parent = row->fParent;
	if (isVisible != nullptr)
		*isVisible = _VisibleIndexOf(row->fParent) >= 0;
	return true;
}


int32
BColumnListView::IndexOf(const BRow* row) const
{
	if (row == nullptr || !_Owns(row))
		return -1;

	const std::vector<std::unique_ptr<BRow>>& siblings = *_Siblings(row->fParent);
	for (std::size_t i = 0; i < siblings.size(); i++) {
		if (siblings[i].get() == row)
			return static_cast<int32>(i);
	}
	return -1;
}


int32
BColumnListView::CountRows(BRow* parent) const
{
	if (parent != nullptr && !_Owns(parent))
		return 0;
	return static_cast<int32>(_Siblings(parent)->size());
}


bool
BColumnListView::AddRow(std::unique_ptr<BRow> row, BRow* parent)
{
	return AddRow(std::move(row), CountRows(parent), parent);
}


bool
BColumnListView::AddRow(std::unique_ptr<BRow> row, int32 index, BRow* parent)
{
	if (row == nullptr || row->fList != nullptr || (parent != nullptr && !_Owns(parent)))
		return false;

	BRow* added = row.get();
	_Attach(added, parent);
	if (_Sorting())
		_SortRows(added->fChildren);

	std::vector<std::unique_ptr<BRow>>& siblings = *_Siblings(parent);
	const int32 at = _InsertIndex(siblings, added, index);
	siblings.insert(siblings.begin() + at, std::move(row));
	_RowsChanged();
	return true;
}


bool
BColumnListView::AddRows(std::vector<std::unique_ptr<BRow>> rows, int32 index, BRow* parent)
{
	bool added = true;
	for (std::unique_ptr<BRow>& row : rows) {
		if (AddRow(std::move(row), index, parent)) {
			if (index >= 0)
				index++;
		} else
			added = false;
	}
	return added;
}


std::unique_ptr<BRow>
BColumnListView::RemoveRow(BRow* row)
{
	bool selectionChanged = false;
	std::unique_ptr<BRow> removed = _RemoveRow(row, &selectionChanged);
	if (removed != nullptr)
		_RowsChanged();
	if (selectionChanged)
		_NotifySelection();
	return removed;
}


std::vector<std::unique_ptr<BRow>>
BColumnListView::RemoveRows(const std::vector<BRow*>& rows)
{
	std::vector<std::unique_ptr<BRow>> removed;
	bool selectionChanged = false;
	for (BRow* row : rows) {
		std::unique_ptr<BRow> taken = _RemoveRow(row, &selectionChanged);
		if (taken != nullptr)
			removed.push_back(std::move(taken));
	}

	if (!removed.empty())
		_RowsChanged();
	if (selectionChanged)
		_NotifySelection();
	return removed;
}


void
BColumnListView::UpdateRow(BRow* row)
{
	if (row == nullptr || !_Owns(row))
		return;

	if (_Sorting()) {
		std::vector<std::unique_ptr<BRow>>& siblings = *_Siblings(row->fParent);
		const auto found = std::find_if(siblings.begin(), siblings.end(),
			[row](const std::unique_ptr<BRow>& each) { return each.get() == row; });
		std::unique_ptr<BRow> owned = std::move(*found);
		siblings.erase(found);
		const int32 at = _InsertIndex(siblings, row, 0);
		siblings.insert(siblings.begin() + at, std::move(owned));
	}
	_RowsChanged();
}


bool
BColumnListView::SwapRows(int32 index1, int32 index2, BRow* parent1, BRow* parent2)
{
	if ((parent1 != nullptr && !_Owns(parent1)) || (parent2 != nullptr && !_Owns(parent2)))
		return false;

	std::vector<std::unique_ptr<BRow>>& rows1 = *_Siblings(parent1);
	std::vector<std::unique_ptr<BRow>>& rows2 = *_Siblings(parent2);
	if (index1 < 0 || index1 >= static_cast<int32>(rows1.size()) || index2 < 0
		|| index2 >= static_cast<int32>(rows2.size())) {
		return false;
	}

	std::unique_ptr<BRow>& first = rows1[static_cast<std::size_t>(index1)];
	std::unique_ptr<BRow>& second = rows2[static_cast<std::size_t>(index2)];
	if (first == second)
		return true;
	// a row cannot move below itself
	if (IsInSubtree(parent2, first.get(), &BRow::fParent)
		|| IsInSubtree(parent1, second.get(), &BRow::fParent)) {
		return false;
	}

	std::swap(first, second);
	first->fParent = parent1;
	second->fParent = parent2;

	fVisibleRowsValid = false;
	const bool selectionChanged = _DeselectHidden();
	_RowsChanged();
	if (selectionChanged)
		_NotifySelection();
	return true;
}


void
BColumnListView::Clear()
{
	const bool hadSelection = CurrentSelection() != nullptr;
	fRowView->ForgetRow(nullptr);
	fFocusRow = nullptr;
	fAnchorRow = nullptr;
	fRows.clear();
	_RowsChanged();
	if (hadSelection)
		_NotifySelection();
}


void
BColumnListView::InvalidateRow(BRow* /*row*/)
{
	// the row view is recorded as a whole
	fRowView->Invalidate();
}


void
BColumnListView::ScrollTo(const BRow* row)
{
	gfx::Rect rect;
	if (!GetRowRect(row, &rect))
		return;

	const gfx::Rect bounds = fRowView->Bounds();
	if (rect.y0 < bounds.y0)
		fRowView->ScrollTo({bounds.x0, rect.y0});
	else if (rect.y1 > bounds.y1)
		fRowView->ScrollTo({bounds.x0, std::min(rect.y0, rect.y1 - bounds.Height())});
}


// #pragma mark - appearance


void
BColumnListView::SetColor(BColumnListViewColor which, gfx::Color color)
{
	fColors[static_cast<std::size_t>(which)] = color;
	Invalidate();
	fTitleView->Invalidate();
	fRowView->Invalidate();
}


void
BColumnListView::ResetColors()
{
	fColors.fill(std::nullopt);
	Invalidate();
	fTitleView->Invalidate();
	fRowView->Invalidate();
}


gfx::Color
BColumnListView::Color(BColumnListViewColor which) const
{
	return _Color(which, Palette::Default());
}


gfx::Point
BColumnListView::SuggestTextPosition(const BRow* row, const BColumn* column) const
{
	if (column == nullptr)
		column = _FirstVisibleColumn();

	const gfx::Rect rect = GetFieldRect(row, column);
	if (rect.IsEmpty())
		return {0, 0};
	return {rect.x0 + kTextMargin, ColumnTextBaseline(rect, TextSystem::Default())};
}


gfx::Rect
BColumnListView::GetFieldRect(const BRow* row, const BColumn* column) const
{
	const int32 index = _VisibleIndexOf(row);
	if (index < 0 || column == nullptr || column->fList != this || !column->fVisible)
		return {};
	return _FieldRect(_VisibleRows()[static_cast<std::size_t>(index)], column);
}


void
BColumnListView::SetLatchWidth(float width)
{
	fLatchWidth = std::max(width, 0.0f);
	_RowsChanged();
}


float
BColumnListView::LatchWidth() const
{
	return fLatchWidth;
}


void
BColumnListView::DrawLatch(BView* view, DrawContext& context, gfx::Rect frame, BLatchType type,
	BRow* /*row*/)
{
	if (type == BLatchType::None)
		return;

	// a square as large as the font, centered
	const float size = std::min({context.text.FontSize(FontRole::Plain), frame.Width(),
		frame.Height()});
	const float left = std::floor(frame.x0 + (frame.Width() - size) / 2);
	const float top = std::floor(frame.y0 + (frame.Height() - size) / 2);
	gfx::Rect latch = {left, top, left + size, top + size};

	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);
	const bool dark = base.r + base.g + base.b <= 1.5f;
	float tint = dark ? kLighten2Tint : kDarken4Tint;
	if (type == BLatchType::Pressed)
		tint = dark ? kLightenMaxTint : kDarkenMaxTint;

	view->Look().DrawArrowShape(context, latch, base,
		type == BLatchType::Closed ? ArrowDirection::Right : ArrowDirection::Down, {}, tint);
}


void
BColumnListView::SaveState(BMessage* state) const
{
	if (state == nullptr)
		return;

	for (const std::unique_ptr<BColumn>& column : fColumns) {
		(void)state->AddInt32("columnID", column->fFieldID);
		(void)state->AddFloat("columnWidth", column->fWidth);
		(void)state->AddBool("columnVisible", column->fVisible);
	}
	for (const BColumn* column : fSortColumns) {
		(void)state->AddInt32("sortID", column->fFieldID);
		(void)state->AddBool("sortAscending", column->fSortAscending);
	}
	(void)state->AddBool("sortingEnabled", fSortingEnabled);
}


void
BColumnListView::LoadState(const BMessage& state)
{
	auto find = [this](int32 id, std::size_t from) -> std::size_t {
		for (std::size_t i = from; i < fColumns.size(); i++) {
			if (fColumns[i]->fFieldID == id)
				return i;
		}
		return fColumns.size();
	};

	// the named columns first, in their order
	std::size_t placed = 0;
	for (int32 i = 0; auto id = state.FindInt32("columnID", i); i++) {
		const std::size_t found = find(*id, placed);
		if (found == fColumns.size())
			continue;

		std::unique_ptr<BColumn> column = std::move(fColumns[found]);
		fColumns.erase(fColumns.begin() + static_cast<std::ptrdiff_t>(found));
		if (auto width = state.FindFloat("columnWidth", i))
			column->fWidth = std::clamp(*width, column->fMinWidth, column->fMaxWidth);
		if (auto visible = state.FindBool("columnVisible", i))
			column->fVisible = *visible;
		fColumns.insert(fColumns.begin() + static_cast<std::ptrdiff_t>(placed),
			std::move(column));
		placed++;
	}

	if (state.FindInt32("sortID", 0).has_value()) {
		fSortColumns.clear();
		for (int32 i = 0; auto id = state.FindInt32("sortID", i); i++) {
			const std::size_t found = find(*id, 0);
			if (found == fColumns.size())
				continue;
			BColumn* column = fColumns[found].get();
			if (std::find(fSortColumns.begin(), fSortColumns.end(), column)
					!= fSortColumns.end()) {
				continue;
			}
			column->fSortAscending = state.FindBool("sortAscending", i).value_or(true);
			fSortColumns.push_back(column);
		}
	}
	if (auto enabled = state.FindBool("sortingEnabled"))
		fSortingEnabled = *enabled;

	_SortAll();
}


BView*
BColumnListView::ScrollView() const
{
	return fRowView;
}


void
BColumnListView::Refresh()
{
	if (_Sorting())
		_SortAll();
	else
		_RowsChanged();
}


// #pragma mark - BView


void
BColumnListView::MakeFocus(bool focus)
{
	if (IsFocus() == focus)
		return;

	BView::MakeFocus(focus);
	Invalidate();
	fRowView->Invalidate();
}


void
BColumnListView::Draw(DrawContext& context)
{
	if (fBorderStyle == BBorderStyle::None)
		return;

	ControlStateFlags flags;
	if (IsFocus() && Window() != nullptr && Window()->IsActive())
		flags |= ControlState::Focused;

	gfx::Rect rect = Bounds();
	const gfx::Rect vertical = fVerticalScrollBar->Frame();
	const gfx::Rect horizontal = fHorizontalScrollBar != nullptr
		? fHorizontalScrollBar->Frame() : gfx::Rect{0, 0, 0, 0};
	Look().DrawScrollViewFrame(context, rect, vertical, horizontal,
		context.palette.Get(BColorWhich::PanelBackground), fBorderStyle, flags);
}


void
BColumnListView::AttachedToWindow()
{
	BView::AttachedToWindow();
	if (!Messenger().IsValid())
		SetTarget(BMessenger(Window()));
	_LayoutChildren();
}


void
BColumnListView::WindowActivated(bool active)
{
	BView::WindowActivated(active);
	Invalidate();
	fRowView->Invalidate();
}


void
BColumnListView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	_LayoutChildren();
}


void
BColumnListView::KeyDown(std::string_view bytes)
{
	if (bytes.empty())
		return BView::KeyDown(bytes);

	const BModifierFlags modifiers = _Modifiers();
	const std::vector<VisibleRow>& rows = _VisibleRows();
	const auto last = static_cast<int32>(rows.size()) - 1;
	const int32 focus = _VisibleIndexOf(fFocusRow);

	switch (bytes[0]) {
		case keys::kUpArrow:
			_MoveFocus(focus < 0 ? 0 : focus - 1, modifiers);
			break;

		case keys::kDownArrow:
			_MoveFocus(focus < 0 ? 0 : focus + 1, modifiers);
			break;

		case keys::kPageUp:
		case keys::kPageDown:
		{
			if (rows.empty())
				break;
			const float page = fRowView->Bounds().Height();
			const float from = focus < 0 ? 0 : rows[static_cast<std::size_t>(focus)].top;
			const float y = bytes[0] == keys::kPageUp ? from - page : from + page;
			const VisibleRow* target = _VisibleRowAt(y);
			int32 index = y < 0 ? 0 : last;
			if (target != nullptr)
				index = _VisibleIndexOf(target->row);
			_MoveFocus(index, modifiers);
			break;
		}

		case keys::kHome:
			_MoveFocus(0, modifiers);
			break;

		case keys::kEnd:
			_MoveFocus(last, modifiers);
			break;

		case keys::kLeftArrow:
			if (fFocusRow == nullptr)
				break;
			if (fFocusRow->HasLatch() && fFocusRow->fIsExpanded)
				ExpandOrCollapse(fFocusRow, false);
			else if (fFocusRow->fParent != nullptr)
				_MoveFocus(_VisibleIndexOf(fFocusRow->fParent), modifiers);
			break;

		case keys::kRightArrow:
			if (fFocusRow == nullptr || !fFocusRow->HasLatch())
				break;
			if (!fFocusRow->fIsExpanded)
				ExpandOrCollapse(fFocusRow, true);
			else
				_MoveFocus(focus + 1, modifiers);
			break;

		case keys::kSpace:
		{
			if (fFocusRow == nullptr)
				break;
			fAnchorRow = fFocusRow;
			if (fSelectionMode == BListViewType::MultipleSelection) {
				fFocusRow->fIsSelected = !fFocusRow->fIsSelected;
				_NotifySelection();
			} else if (_SelectOnly(fFocusRow))
				_NotifySelection();
			break;
		}

		case keys::kEnter:
			if (CurrentSelection() != nullptr)
				ItemInvoked();
			break;

		default:
			BView::KeyDown(bytes);
			break;
	}
}


gfx::Size
BColumnListView::MinSize()
{
	const float border = BorderSize(fBorderStyle);
	const TextSystem& text = TextSystem::Default();
	const float scrollBar = Look().ScrollBarWidth(text, BOrientation::Vertical);
	const float horizontal = fHorizontalScrollBar != nullptr
		? Look().ScrollBarWidth(text, BOrientation::Horizontal) : 0.0f;
	const float status = fStatusView != nullptr ? fStatusView->Frame().Width() : 0.0f;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		{2 * border + fLatchWidth + scrollBar + status + 2 * kTextMargin,
			2 * border + _TitleHeight() + DefaultRowHeight() + horizontal});
}


gfx::Size
BColumnListView::PreferredSize()
{
	const float border = BorderSize(fBorderStyle);
	const TextSystem& text = TextSystem::Default();
	const float scrollBar = Look().ScrollBarWidth(text, BOrientation::Vertical);
	const float horizontal = fHorizontalScrollBar != nullptr
		? Look().ScrollBarWidth(text, BOrientation::Horizontal) : 0.0f;
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		{2 * border + _ContentWidth() + scrollBar,
			2 * border + _TitleHeight() + kPreferredRows * DefaultRowHeight() + horizontal});
}


gfx::Size
BColumnListView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), {kSizeUnlimited, kSizeUnlimited});
}


void
BColumnListView::DoLayout()
{
	_LayoutChildren();
}


// #pragma mark - accessibility


void
BColumnListView::GetAccessibleInfo(AccessibleInfo* info) const
{
	BView::GetAccessibleInfo(info);
	if (info == nullptr)
		return;

	const bool tree = std::any_of(fRows.begin(), fRows.end(),
		[](const std::unique_ptr<BRow>& row) { return !row->fChildren.empty(); });
	info->role = tree ? AccessibleRole::TreeTable : AccessibleRole::Table;
	info->rowCount = static_cast<std::int32_t>(_VisibleRows().size()) + 1;
	info->columnCount = _CountVisibleColumns();
}


void
BColumnListView::GetAccessibleItems(std::vector<AccessibleItem>* items) const
{
	if (items == nullptr)
		return;

	std::vector<BColumn*> columns;
	for (const std::unique_ptr<BColumn>& column : fColumns) {
		if (column->fVisible)
			columns.push_back(column.get());
	}
	AccessibleInfo listInfo;
	GetAccessibleInfo(&listInfo);
	const bool tree = listInfo.role == AccessibleRole::TreeTable;

	// the titles: a row of column headers
	AccessibleItem titles;
	titles.info.role = AccessibleRole::Row;
	titles.info.rowIndex = 0;
	titles.frame = fTitleView->Frame();
	items->push_back(std::move(titles));
	const gfx::Rect titleBounds = fTitleView->Bounds();
	for (std::size_t j = 0; j < columns.size(); j++) {
		AccessibleItem header;
		header.info.role = AccessibleRole::ColumnHeader;
		columns[j]->GetColumnName(&header.info.name);
		header.info.rowIndex = 0;
		header.info.columnIndex = static_cast<std::int32_t>(j);
		if (fSortingEnabled)
			header.info.actions |= AccessibleAction::Click;
		const float left = _ColumnLeft(columns[j]);
		header.frame = fTitleView->ConvertToParent(gfx::Rect{left, titleBounds.y0,
			left + columns[j]->fWidth, titleBounds.y1});
		header.parent = 0;
		items->push_back(std::move(header));
	}

	// the rows, with a cell for each column
	const std::vector<VisibleRow>& rows = _VisibleRows();
	const float width = std::max(_ContentWidth(), fRowView->Bounds().x1);
	for (std::size_t k = 0; k < rows.size(); k++) {
		const VisibleRow& shown = rows[k];
		BRow* row = shown.row;
		AccessibleItem entry;
		entry.info.role = AccessibleRole::Row;
		entry.info.rowIndex = static_cast<std::int32_t>(k) + 1;
		if (tree)
			entry.info.level = shown.level + 1;
		if (row->fIsSelected)
			entry.info.states |= AccessibleState::Selected;
		if (row == fFocusRow)
			entry.info.states |= AccessibleState::Focused;
		entry.info.actions = AccessibleAction::Click | AccessibleAction::Focus;
		if (row->HasLatch()) {
			entry.info.states |= row->fIsExpanded ? AccessibleState::Expanded
				: AccessibleState::Collapsed;
			entry.info.actions |= row->fIsExpanded ? AccessibleAction::Collapse
				: AccessibleAction::Expand;
		}
		entry.frame = fRowView->ConvertToParent(gfx::Rect{0, shown.top, width,
			shown.top + row->fHeight});
		const auto rowItem = static_cast<std::int32_t>(items->size());
		items->push_back(std::move(entry));

		for (std::size_t j = 0; j < columns.size(); j++) {
			AccessibleItem cell;
			cell.info.role = AccessibleRole::Cell;
			BField* field = row->GetField(columns[j]->fFieldID);
			if (field != nullptr && columns[j]->AcceptsField(field))
				columns[j]->GetFieldText(field, &cell.info.name);
			cell.info.rowIndex = static_cast<std::int32_t>(k) + 1;
			cell.info.columnIndex = static_cast<std::int32_t>(j);
			cell.frame = fRowView->ConvertToParent(_FieldRect(shown, columns[j]));
			cell.parent = rowItem;
			items->push_back(std::move(cell));
		}
	}
}


std::expected<void, Error>
BColumnListView::PerformAccessibleItemAction(int32 index, AccessibleAction action,
	const BMessage& /*data*/)
{
	std::vector<BColumn*> columns;
	for (const std::unique_ptr<BColumn>& column : fColumns) {
		if (column->fVisible)
			columns.push_back(column.get());
	}
	const auto perRow = static_cast<int32>(columns.size()) + 1;
	if (index < 0)
		return Failure(ErrorCode::NotFound, "no such item");

	// the titles' row and its column headers
	if (index < perRow) {
		if (index == 0 || action != AccessibleAction::Click || !fSortingEnabled)
			return Failure(ErrorCode::NotSupported, "the item does not take this action");
		_TitleClicked(columns[static_cast<std::size_t>(index - 1)], {});
		return {};
	}

	// a row or one of its cells
	const std::vector<VisibleRow>& rows = _VisibleRows();
	const auto shown = static_cast<std::size_t>((index - perRow) / perRow);
	if (shown >= rows.size())
		return Failure(ErrorCode::NotFound, "no such item");
	BRow* row = rows[shown].row;
	switch (action) {
		case AccessibleAction::Click:
			_ClickRow(row, {});
			return {};
		case AccessibleAction::Focus:
			MakeFocus(true);
			SetFocusRow(row);
			return {};
		case AccessibleAction::Expand:
		case AccessibleAction::Collapse:
			if (!row->HasLatch())
				return Failure(ErrorCode::NotSupported, "the row has no subrows");
			ExpandOrCollapse(row, action == AccessibleAction::Expand);
			return {};
		default:
			return Failure(ErrorCode::NotSupported, "the item does not take this action");
	}
}


// #pragma mark - private


void
BColumnListView::_Init(BBorderStyle border, bool showHorizontalScrollBar)
{
	fBorderStyle = border;
	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));

	fRowView = static_cast<ColumnRowView*>(AddChild(std::make_unique<ColumnRowView>(this)));
	fTitleView = static_cast<ColumnTitleView*>(
		AddChild(std::make_unique<ColumnTitleView>(this)));

	fVerticalScrollBar = static_cast<BScrollBar*>(AddChild(std::make_unique<BScrollBar>(
		gfx::Rect{0, 0, 14, 14}, "vertical", fRowView, 0.0f, 0.0f, BOrientation::Vertical)));
	fVerticalScrollBar->SetResizingMode(kFollowLeftTop);
	if (showHorizontalScrollBar) {
		fHorizontalScrollBar = static_cast<BScrollBar*>(
			AddChild(std::make_unique<ColumnScrollBar>(fRowView, fTitleView)));
		fHorizontalScrollBar->SetResizingMode(kFollowLeftTop);
	}

	_LayoutChildren();
}


void
BColumnListView::_LayoutChildren()
{
	const gfx::Rect bounds = Bounds();
	const float border = BorderSize(fBorderStyle);
	const TextSystem& text = TextSystem::Default();
	const float scrollBar = Look().ScrollBarWidth(text, BOrientation::Vertical);
	const float horizontal = fHorizontalScrollBar != nullptr
		? Look().ScrollBarWidth(text, BOrientation::Horizontal) : 0.0f;
	const float titleHeight = _TitleHeight();
	const float left = border;
	const float top = border;
	const float right = bounds.Width() - border;
	const float bottom = bounds.Height() - border;
	const float rowsHeight = std::max(bottom - horizontal - top - titleHeight, 0.0f);

	fTitleView->MoveTo({left, top});
	fTitleView->ResizeTo(std::max(right - left, 0.0f), titleHeight);
	fRowView->MoveTo({left, top + titleHeight});
	fRowView->ResizeTo(std::max(right - scrollBar - left, 0.0f), rowsHeight);
	fVerticalScrollBar->MoveTo({right - scrollBar, top + titleHeight});
	fVerticalScrollBar->ResizeTo(scrollBar, rowsHeight);

	const float status = fStatusView != nullptr ? fStatusView->Frame().Width() : 0.0f;
	if (fStatusView != nullptr) {
		fStatusView->MoveTo({left, bottom - horizontal});
		fStatusView->ResizeTo(status, horizontal);
	}
	if (fHorizontalScrollBar != nullptr) {
		fHorizontalScrollBar->MoveTo({left + status, bottom - horizontal});
		fHorizontalScrollBar->ResizeTo(std::max(right - scrollBar - left - status, 0.0f),
			horizontal);
	}

	_FixScrollBars();
	Invalidate();
}


void
BColumnListView::_RowsChanged()
{
	fVisibleRowsValid = false;
	_FixScrollBars();
	fTitleView->Invalidate();
	fRowView->Invalidate();
}


void
BColumnListView::_FixScrollBars()
{
	const gfx::Rect bounds = fRowView->Bounds();

	const float height = _ContentHeight();
	if (height <= bounds.Height())
		fVerticalScrollBar->SetRange(0, 0);
	else {
		fVerticalScrollBar->SetRange(0, height - bounds.Height());
		fVerticalScrollBar->SetProportion(bounds.Height() / height);
	}
	fVerticalScrollBar->SetSteps(std::ceil(DefaultRowHeight()), bounds.Height());

	if (fHorizontalScrollBar == nullptr)
		return;

	const float width = _ContentWidth();
	if (width <= bounds.Width())
		fHorizontalScrollBar->SetRange(0, 0);
	else {
		fHorizontalScrollBar->SetRange(0, width - bounds.Width());
		fHorizontalScrollBar->SetProportion(bounds.Width() / width);
	}
	fHorizontalScrollBar->SetSteps(kTextMargin, bounds.Width());
}


const std::vector<BColumnListView::VisibleRow>&
BColumnListView::_VisibleRows() const
{
	if (fVisibleRowsValid)
		return fVisibleRows;

	fVisibleRows.clear();
	float top = 0;

	// depth first, without recursion: deep trees must not exhaust the stack
	struct Level {
		const std::vector<std::unique_ptr<BRow>>* rows;
		std::size_t next;
	};
	std::vector<Level> stack = {{&fRows, 0}};
	while (!stack.empty()) {
		Level& level = stack.back();
		if (level.next >= level.rows->size()) {
			stack.pop_back();
			continue;
		}

		BRow* row = (*level.rows)[level.next++].get();
		fVisibleRows.push_back({row, top, static_cast<int32>(stack.size()) - 1});
		top += row->fHeight;
		if (row->fIsExpanded && !row->fChildren.empty())
			stack.push_back({&row->fChildren, 0});
	}

	fVisibleRowsValid = true;
	return fVisibleRows;
}


int32
BColumnListView::_VisibleIndexOf(const BRow* row) const
{
	if (row == nullptr || row->fList != this)
		return -1;

	const std::vector<VisibleRow>& rows = _VisibleRows();
	for (std::size_t i = 0; i < rows.size(); i++) {
		if (rows[i].row == row)
			return static_cast<int32>(i);
	}
	return -1;
}


const BColumnListView::VisibleRow*
BColumnListView::_VisibleRowAt(float y) const
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	const auto after = std::upper_bound(rows.begin(), rows.end(), y,
		[](float value, const VisibleRow& row) { return value < row.top; });
	if (after == rows.begin())
		return nullptr;

	const VisibleRow& row = *std::prev(after);
	if (y >= row.top + row.row->fHeight)
		return nullptr;
	return &row;
}


float
BColumnListView::_ContentWidth() const
{
	float width = fLatchWidth;
	for (const std::unique_ptr<BColumn>& column : fColumns) {
		if (column->fVisible)
			width += column->fWidth;
	}
	return width;
}


float
BColumnListView::_ContentHeight() const
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	if (rows.empty())
		return 0;
	return rows.back().top + rows.back().row->fHeight;
}


float
BColumnListView::_ColumnLeft(const BColumn* column) const
{
	float left = fLatchWidth;
	for (const std::unique_ptr<BColumn>& each : fColumns) {
		if (!each->fVisible)
			continue;
		if (each.get() == column)
			return left;
		left += each->fWidth;
	}
	return -1;
}


BColumn*
BColumnListView::_FirstVisibleColumn() const
{
	for (const std::unique_ptr<BColumn>& column : fColumns) {
		if (column->fVisible)
			return column.get();
	}
	return nullptr;
}


gfx::Rect
BColumnListView::_LatchRect(const VisibleRow& row) const
{
	const float left = static_cast<float>(row.level) * fLatchWidth;
	return {left, row.top, left + fLatchWidth, row.top + row.row->fHeight};
}


gfx::Rect
BColumnListView::_FieldRect(const VisibleRow& row, const BColumn* column) const
{
	const float left = _ColumnLeft(column);
	if (left < 0)
		return {};

	gfx::Rect rect = {left, row.top, left + column->fWidth, row.top + row.row->fHeight};
	if (column == _FirstVisibleColumn())
		rect.x0 = std::min(rect.x1, rect.x0 + static_cast<float>(row.level) * fLatchWidth);
	return rect;
}


float
BColumnListView::_TitleHeight() const
{
	const FontHeight height = TextSystem::Default().Height(FontRole::Plain);
	return std::ceil(height.ascent) + std::ceil(height.descent) + std::ceil(height.leading)
		+ kTitleExtra;
}


gfx::Color
BColumnListView::_Color(BColumnListViewColor which, const Palette& palette) const
{
	const std::optional<gfx::Color>& set = fColors[static_cast<std::size_t>(which)];
	if (set.has_value())
		return *set;

	switch (which) {
		case BColumnListViewColor::Background:
			return palette.Get(BColorWhich::ListBackground);
		case BColumnListViewColor::Text:
			return palette.Get(BColorWhich::ListItemText);
		case BColumnListViewColor::RowDivider:
			return TintColor(palette.Get(BColorWhich::ListBackground), kRowDividerTint);
		case BColumnListViewColor::Selection:
			return palette.Get(BColorWhich::ListSelectedBackground);
		case BColumnListViewColor::SelectionText:
			return palette.Get(BColorWhich::ListSelectedItemText);
		case BColumnListViewColor::NonFocusSelection:
			return TintColor(palette.Get(BColorWhich::ListSelectedBackground), kLighten1Tint);
		case BColumnListViewColor::EditBackground:
			return palette.Get(BColorWhich::DocumentBackground);
		case BColumnListViewColor::EditText:
			return palette.Get(BColorWhich::DocumentText);
		case BColumnListViewColor::HeaderBackground:
			return palette.Get(BColorWhich::PanelBackground);
		case BColumnListViewColor::HeaderText:
			return palette.Get(BColorWhich::PanelText);
		case BColumnListViewColor::SeparatorLine:
			return TintColor(palette.Get(BColorWhich::PanelBackground), kDarken2Tint);
		case BColumnListViewColor::SeparatorBorder:
			return TintColor(palette.Get(BColorWhich::PanelBackground), kLighten2Tint);
	}
	return palette.Get(BColorWhich::ListBackground);
}


std::vector<std::unique_ptr<BRow>>*
BColumnListView::_Siblings(BRow* parent)
{
	return parent != nullptr ? &parent->fChildren : &fRows;
}


const std::vector<std::unique_ptr<BRow>>*
BColumnListView::_Siblings(const BRow* parent) const
{
	return parent != nullptr ? &parent->fChildren : &fRows;
}


bool
BColumnListView::_Owns(const BRow* row) const
{
	return row != nullptr && row->fList == this;
}


bool
BColumnListView::_Sorting() const
{
	return fSortingEnabled && !fSortColumns.empty();
}


bool
BColumnListView::_RowLess(const BRow* a, const BRow* b) const
{
	for (BColumn* column : fSortColumns) {
		BField* first = const_cast<BRow*>(a)->GetField(column->fFieldID);
		BField* second = const_cast<BRow*>(b)->GetField(column->fFieldID);
		if (first != nullptr && !column->AcceptsField(first))
			first = nullptr;
		if (second != nullptr && !column->AcceptsField(second))
			second = nullptr;

		// missing fields last, in both directions
		if (first == nullptr || second == nullptr) {
			if (first == second)
				continue;
			return second == nullptr;
		}

		int order = column->CompareFields(first, second);
		if (!column->fSortAscending)
			order = -order;
		if (order != 0)
			return order < 0;
	}
	return false;
}


void
BColumnListView::_SortRows(std::vector<std::unique_ptr<BRow>>& rows)
{
	if (!_Sorting())
		return;

	std::vector<std::vector<std::unique_ptr<BRow>>*> pending = {&rows};
	while (!pending.empty()) {
		std::vector<std::unique_ptr<BRow>>* list = pending.back();
		pending.pop_back();
		std::stable_sort(list->begin(), list->end(),
			[this](const std::unique_ptr<BRow>& a, const std::unique_ptr<BRow>& b) {
				return _RowLess(a.get(), b.get());
			});
		for (std::unique_ptr<BRow>& row : *list) {
			if (!row->fChildren.empty())
				pending.push_back(&row->fChildren);
		}
	}
}


void
BColumnListView::_SortAll()
{
	_SortRows(fRows);
	_RowsChanged();
}


int32
BColumnListView::_InsertIndex(const std::vector<std::unique_ptr<BRow>>& rows, const BRow* row,
	int32 index) const
{
	if (_Sorting()) {
		const auto after = std::upper_bound(rows.begin(), rows.end(), row,
			[this](const BRow* value, const std::unique_ptr<BRow>& each) {
				return _RowLess(value, each.get());
			});
		return static_cast<int32>(std::distance(rows.begin(), after));
	}

	const auto count = static_cast<int32>(rows.size());
	return index < 0 || index > count ? count : index;
}


void
BColumnListView::_Attach(BRow* row, BRow* parent)
{
	row->fParent = parent;
	std::vector<BRow*> pending = {row};
	while (!pending.empty()) {
		BRow* current = pending.back();
		pending.pop_back();
		current->fList = this;
		current->fIsSelected = false;
		for (std::unique_ptr<BRow>& child : current->fChildren) {
			child->fParent = current;
			pending.push_back(child.get());
		}
	}
}


std::unique_ptr<BRow>
BColumnListView::_RemoveRow(BRow* row, bool* selectionChanged)
{
	if (row == nullptr || !_Owns(row))
		return nullptr;

	const int32 visibleIndex = _VisibleIndexOf(row);
	if (_DeselectSubtree(row, true))
		*selectionChanged = true;
	const bool focusInside = fFocusRow != nullptr
		&& IsInSubtree(fFocusRow, row, &BRow::fParent);
	if (fAnchorRow != nullptr && IsInSubtree(fAnchorRow, row, &BRow::fParent))
		fAnchorRow = nullptr;

	std::vector<std::unique_ptr<BRow>>& siblings = *_Siblings(row->fParent);
	const auto found = std::find_if(siblings.begin(), siblings.end(),
		[row](const std::unique_ptr<BRow>& each) { return each.get() == row; });
	std::unique_ptr<BRow> removed = std::move(*found);
	siblings.erase(found);
	_ForgetSubtree(row);
	row->fParent = nullptr;
	fVisibleRowsValid = false;

	if (focusInside) {
		fFocusRow = nullptr;
		_FixFocusAfterHiding(visibleIndex);
	}
	return removed;
}


bool
BColumnListView::_DeselectHidden()
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	std::unordered_set<const BRow*> visible;
	for (const VisibleRow& row : rows)
		visible.insert(row.row);

	bool changed = false;
	std::vector<BRow*> pending;
	for (std::unique_ptr<BRow>& row : fRows)
		pending.push_back(row.get());
	while (!pending.empty()) {
		BRow* row = pending.back();
		pending.pop_back();
		if (row->fIsSelected && !visible.contains(row)) {
			row->fIsSelected = false;
			changed = true;
		}
		for (std::unique_ptr<BRow>& child : row->fChildren)
			pending.push_back(child.get());
	}

	if (fFocusRow != nullptr && !visible.contains(fFocusRow))
		fFocusRow = nullptr;
	if (fAnchorRow != nullptr && !visible.contains(fAnchorRow))
		fAnchorRow = nullptr;
	return changed;
}


bool
BColumnListView::_DeselectSubtree(BRow* row, bool includeSelf)
{
	bool changed = false;
	std::vector<BRow*> pending;
	if (includeSelf)
		pending.push_back(row);
	else {
		for (std::unique_ptr<BRow>& child : row->fChildren)
			pending.push_back(child.get());
	}

	while (!pending.empty()) {
		BRow* current = pending.back();
		pending.pop_back();
		if (current->fIsSelected) {
			current->fIsSelected = false;
			changed = true;
		}
		for (std::unique_ptr<BRow>& child : current->fChildren)
			pending.push_back(child.get());
	}
	return changed;
}


void
BColumnListView::_ForgetSubtree(BRow* row)
{
	std::vector<BRow*> pending = {row};
	while (!pending.empty()) {
		BRow* current = pending.back();
		pending.pop_back();
		fRowView->ForgetRow(current);
		current->fList = nullptr;
		for (std::unique_ptr<BRow>& child : current->fChildren)
			pending.push_back(child.get());
	}
}


void
BColumnListView::_FixFocusAfterHiding(int32 visibleIndex)
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	if (rows.empty() || visibleIndex < 0) {
		fFocusRow = nullptr;
		return;
	}

	const auto index = std::min(static_cast<std::size_t>(visibleIndex), rows.size() - 1);
	fFocusRow = rows[index].row;
}


bool
BColumnListView::_SelectOnly(BRow* row)
{
	bool changed = false;
	for (const VisibleRow& shown : _VisibleRows()) {
		const bool selected = shown.row == row;
		if (shown.row->fIsSelected != selected) {
			shown.row->fIsSelected = selected;
			changed = true;
		}
	}
	return changed;
}


bool
BColumnListView::_SelectRange(BRow* from, BRow* to, bool extend)
{
	const int32 first = _VisibleIndexOf(from);
	const int32 second = _VisibleIndexOf(to);
	if (first < 0 || second < 0)
		return false;

	const int32 low = std::min(first, second);
	const int32 high = std::max(first, second);
	const std::vector<VisibleRow>& rows = _VisibleRows();
	bool changed = false;
	for (std::size_t i = 0; i < rows.size(); i++) {
		const auto index = static_cast<int32>(i);
		const bool inside = index >= low && index <= high;
		if (!inside && extend)
			continue;
		if (rows[i].row->fIsSelected != inside) {
			rows[i].row->fIsSelected = inside;
			changed = true;
		}
	}
	return changed;
}


void
BColumnListView::_NotifySelection()
{
	fRowView->Invalidate();
	SelectionChanged();
}


void
BColumnListView::_ClickRow(BRow* row, BModifierFlags modifiers)
{
	const bool multiple = fSelectionMode == BListViewType::MultipleSelection;
	bool changed = false;
	if (multiple && modifiers.Has(BModifier::Shift) && _VisibleIndexOf(fAnchorRow) >= 0)
		changed = _SelectRange(fAnchorRow, row, modifiers.Has(BModifier::Command));
	else if (multiple && modifiers.Has(BModifier::Command)) {
		row->fIsSelected = !row->fIsSelected;
		changed = true;
		fAnchorRow = row;
	} else {
		changed = _SelectOnly(row);
		fAnchorRow = row;
	}

	fFocusRow = row;
	fRowView->Invalidate();
	if (changed)
		_NotifySelection();
}


void
BColumnListView::_MoveFocus(int32 visibleIndex, BModifierFlags modifiers)
{
	const std::vector<VisibleRow>& rows = _VisibleRows();
	if (rows.empty())
		return;

	const auto index = static_cast<std::size_t>(
		std::clamp(visibleIndex, 0, static_cast<int32>(rows.size()) - 1));
	BRow* row = rows[index].row;

	bool changed = false;
	if (modifiers.Has(BModifier::Control)) {
		// only the focus moves (the window keeps keys with Command)
	} else if (fSelectionMode == BListViewType::MultipleSelection
		&& modifiers.Has(BModifier::Shift)) {
		if (_VisibleIndexOf(fAnchorRow) < 0)
			fAnchorRow = fFocusRow != nullptr ? fFocusRow : row;
		changed = _SelectRange(fAnchorRow, row, false);
	} else {
		changed = _SelectOnly(row);
		fAnchorRow = row;
	}

	fFocusRow = row;
	ScrollTo(row);
	fRowView->Invalidate();
	if (changed)
		_NotifySelection();
}


void
BColumnListView::_TitleClicked(BColumn* column, BModifierFlags modifiers)
{
	if (!fSortingEnabled || column == nullptr)
		return;

	const bool sorted = std::find(fSortColumns.begin(), fSortColumns.end(), column)
		!= fSortColumns.end();
	if (modifiers.Has(BModifier::Shift))
		SetSortColumn(column, true, sorted ? !column->fSortAscending : true);
	else if (fSortColumns.size() == 1 && sorted)
		SetSortColumn(column, true, !column->fSortAscending);
	else
		SetSortColumn(column, false, true);
}


int32
BColumnListView::_IndexOfColumn(const BColumn* column) const
{
	for (std::size_t i = 0; i < fColumns.size(); i++) {
		if (fColumns[i].get() == column)
			return static_cast<int32>(i);
	}
	return -1;
}


int32
BColumnListView::_CountVisibleColumns() const
{
	return static_cast<int32>(std::count_if(fColumns.begin(), fColumns.end(),
		[](const std::unique_ptr<BColumn>& column) { return column->fVisible; }));
}


BModifierFlags
BColumnListView::_Modifiers() const
{
	// Haiku's modifiers(): those of the event being handled
	const BMessage* current = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	return BModifierFlags::FromRaw(static_cast<uint32>(
		current != nullptr ? current->GetInt32("modifiers", 0) : 0));
}


} // namespace ngui
