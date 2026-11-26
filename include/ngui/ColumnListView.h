/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Clock.h>
#include <ngui/Flags.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Invoker.h>
#include <ngui/ListView.h>
#include <ngui/View.h>


namespace ngui {


class BColumn;
class BColumnListView;
class BScrollBar;
class ColumnRowView;
class ColumnTitleView;


/*!	How DrawLatch() draws the latch of a row (Haiku's LatchType). */
enum class BLatchType : std::uint8_t {
	None,		/*!< B_NO_LATCH: the row has no subrows. */
	Open,		/*!< B_OPEN_LATCH: the subrows are shown. */
	Pressed,	/*!< B_PRESSED_LATCH: the mouse is pressed on it. */
	Closed		/*!< B_CLOSED_LATCH: the subrows are hidden. */
};


/*!	What the user may do with the columns of a column list view (Haiku's
	column_flags). */
enum class BColumnFlag : std::uint32_t {
	Move = 1u << 0,		/*!< B_ALLOW_COLUMN_MOVE: drag a title to another place. */
	Resize = 1u << 1,	/*!< B_ALLOW_COLUMN_RESIZE: drag the right edge of a title;
							a double click on it fits the column to its fields. */
	PopUp = 1u << 2,	/*!< B_ALLOW_COLUMN_POPUP: a secondary click on the titles
							opens a menu that shows and hides columns. */
	Remove = 1u << 3	/*!< B_ALLOW_COLUMN_REMOVE: a title dragged off the titles
							hides its column. */
};
NGUI_FLAG_ENUM(BColumnFlag)

/*!	A set of BColumnFlag. */
using BColumnFlags = Flags<BColumnFlag>;


/*!	The colors of a column list view (Haiku's ColumnListViewColor). Each
	follows a palette color until SetColor() sets it. */
enum class BColumnListViewColor : std::uint8_t {
	Background,			/*!< B_COLOR_BACKGROUND: behind the rows (list background). */
	Text,				/*!< B_COLOR_TEXT: the fields (list item text). */
	RowDivider,			/*!< B_COLOR_ROW_DIVIDER: the line below each row. */
	Selection,			/*!< B_COLOR_SELECTION: behind selected rows while the list
							has the focus. */
	SelectionText,		/*!< B_COLOR_SELECTION_TEXT: the fields of selected rows. */
	NonFocusSelection,	/*!< B_COLOR_NON_FOCUS_SELECTION: behind selected rows
							otherwise. */
	EditBackground,		/*!< B_COLOR_EDIT_BACKGROUND: for columns that edit fields. */
	EditText,			/*!< B_COLOR_EDIT_TEXT: for columns that edit fields. */
	HeaderBackground,	/*!< B_COLOR_HEADER_BACKGROUND: the column titles. */
	HeaderText,			/*!< B_COLOR_HEADER_TEXT: the text of the titles. */
	SeparatorLine,		/*!< B_COLOR_SEPARATOR_LINE: the dark line between titles. */
	SeparatorBorder		/*!< B_COLOR_SEPARATOR_BORDER: the light line beside it. */
};

/*!	The number of BColumnListViewColor values (B_COLOR_TOTAL). */
inline constexpr std::size_t kColumnListViewColorCount = 12;


/*!	The data of one cell (Haiku's BField): a row holds one field for each
	logical field index, a column draws and compares the fields of its
	index. Derived classes hold the data; BStringField (ColumnTypes.h) a
	string. */
class BField {
public:
								BField();
									/*!< An empty field. */
	virtual						~BField();
};


/*!	A row of a column list view (Haiku's BRow): its fields, its height and
	its subrows. A row belongs to one list; added to it, it is drawn and
	can be selected while all its parents are expanded.

	Differences to Haiku: a row owns its fields (SetField() takes a
	std::unique_ptr) and its subrows; the default height follows the font
	(Haiku: 16 pixels); HasLatch() is true when the row has subrows.
*/
class BRow {
public:
								BRow();
									/*!< A row as high as a line of the plain font and 4
										pixels. */
	explicit					BRow(float height);
									/*!< A row of \a height pixels. */
	virtual						~BRow();
									/*!< Destroys its fields and subrows. */

	virtual	bool				HasLatch() const;
									/*!< Whether it shows a latch: it has subrows. */

			int32				CountFields() const;
									/*!< One more than the highest logical field index
										set. */
			BField*				GetField(int32 logicalFieldIndex);
									/*!< The field of \a logicalFieldIndex, or nullptr. */
			const BField*		GetField(int32 logicalFieldIndex) const;
									/*!< The field of \a logicalFieldIndex, or nullptr. */
			void				SetField(std::unique_ptr<BField> field,
									int32 logicalFieldIndex);
									/*!< Puts \a field at \a logicalFieldIndex (0 or more)
										and destroys the one there; redraws the row.
										BColumnListView::UpdateRow() sorts it anew. */

			float				Height() const;
									/*!< Its height in pixels. */
			bool				IsExpanded() const;
									/*!< Whether its subrows are shown. */
			bool				IsSelected() const;
									/*!< Whether it is selected. */
			void				Invalidate();
									/*!< Redraws it, if it is in a list. */

private:
	friend class BColumnListView;
	friend class ColumnRowView;

			std::vector<std::unique_ptr<BField>> fFields;
			std::vector<std::unique_ptr<BRow>> fChildren;
			BRow*				fParent = nullptr;
			BColumnListView*	fList = nullptr;
			float				fHeight;
			bool				fIsExpanded = false;
			bool				fIsSelected = false;
};


/*!	A column of a column list view (Haiku's BColumn): it draws the title
	and, for each row, the field of its logical field index; it compares
	fields for sorting. BTitledColumn and the columns of ColumnTypes.h
	derive from it.

	Differences to Haiku: the drawing hooks get the DrawContext and the
	color to draw text in; GetColumnName() fills a std::string. New:
	GetFieldText() for screen readers.
*/
class BColumn {
public:
								BColumn(float width, float minWidth, float maxWidth,
									BHorizontalAlignment alignment = BHorizontalAlignment::Left);
									/*!< A column of \a width pixels, kept between
										\a minWidth and \a maxWidth. */
								BColumn(const BColumn&) = delete;
			BColumn&			operator=(const BColumn&) = delete;
	virtual						~BColumn();

			float				Width() const;
									/*!< Its width in pixels. */
			void				SetWidth(float width);
									/*!< Sets the width, kept between MinWidth() and
										MaxWidth(); the list follows. */
			float				MinWidth() const;
									/*!< The smallest width. */
			float				MaxWidth() const;
									/*!< The largest width. */

	virtual	void				DrawTitle(BView* owner, DrawContext& context, gfx::Rect rect,
									const gfx::Color& textColor);
									/*!< Hook: draws the title in \a rect; draws nothing. */
	virtual	void				DrawField(BField* field, BView* owner, DrawContext& context,
									gfx::Rect rect, const gfx::Color& textColor);
									/*!< Hook: draws \a field in \a rect, clipped to it;
										draws nothing. */
	virtual	int					CompareFields(BField* field1, BField* field2);
									/*!< Hook: less than 0 if \a field1 comes first, more
										than 0 if \a field2 does; 0. */

	virtual	void				MouseMoved(BColumnListView* parent, BRow* row, BField* field,
									gfx::Rect fieldRect, gfx::Point point, int32 buttons,
									BMouseTransit transit);
									/*!< Hook for WantsEvents(): the mouse moved over
										\a field, or pressed on it. */
	virtual	void				MouseDown(BColumnListView* parent, BRow* row, BField* field,
									gfx::Rect fieldRect, gfx::Point point, int32 buttons);
									/*!< Hook for WantsEvents(): pressed on \a field. */
	virtual	void				MouseUp(BColumnListView* parent, BRow* row, BField* field);
									/*!< Hook for WantsEvents(): released after
										MouseDown(). */

	virtual	void				GetColumnName(std::string* into) const;
									/*!< Hook: its name, as in the menu of columns; empty
										(the column is not in the menu). */
	virtual	void				GetFieldText(BField* field, std::string* into) const;
									/*!< Hook: \a field as text, for assistive technology
										(the name of its cell); empty. */
	virtual	float				GetPreferredWidth(BField* field, BView* parent) const;
									/*!< Hook: how wide the column wants to be for
										\a field, or for its title if \a field is
										nullptr; its width. */

			bool				IsVisible() const;
									/*!< Whether it is shown. */
			void				SetVisible(bool visible);
									/*!< Shows or hides it. */
			bool				WantsEvents() const;
									/*!< Whether it gets the mouse hooks. */
			void				SetWantsEvents(bool wants);
									/*!< Whether it gets the mouse hooks (default no). */
			bool				ShowHeading() const;
									/*!< Whether its title is drawn. */
			void				SetShowHeading(bool show);
									/*!< Whether its title is drawn (default yes). */
			BHorizontalAlignment Alignment() const;
									/*!< How fields and title are aligned. */
			void				SetAlignment(BHorizontalAlignment alignment);
									/*!< How fields and title are aligned. */
			int32				LogicalFieldNum() const;
									/*!< The logical field index it shows, -1 before
										BColumnListView::AddColumn(). */

	virtual	bool				AcceptsField(const BField* field) const;
									/*!< Hook: whether it can draw and compare \a field;
										true. A field it does not accept is not drawn and
										sorts as missing. */

private:
	friend class BColumnListView;
	friend class ColumnRowView;
	friend class ColumnTitleView;

			float				fWidth;
			float				fMinWidth;
			float				fMaxWidth;
			int32				fFieldID = -1;
			BColumnListView*	fList = nullptr;
			BHorizontalAlignment fAlignment;
			bool				fVisible = true;
			bool				fSortAscending = true;
			bool				fWantsEvents = false;
			bool				fShowHeading = true;
};


/*!	A list of rows in columns, with titles above (Haiku's BColumnListView,
	an own implementation after its API): rows can have subrows, shown
	below them when expanded; a click on a title sorts by that column, again
	in the other direction, with Shift by one more column. Rows are
	selected with the mouse (Shift selects a range, Command toggles in a
	multiple selection list) and the keys: Up, Down, Page Up, Page Down,
	Home and End move the focus row and select it (Shift extends, Control
	only moves), Space selects it (toggles with several), Left collapses it
	or moves to its parent, Right expands it or moves to its first subrow.
	A double click or Enter invokes. The list has scroll bars and a place
	for a status view left of the horizontal one.

	As the column flags allow (SetColumnFlags(), all by default), the user
	drags a title to move its column, its right edge to resize it (a double
	click there fits it), a title off the titles to hide its column, and
	opens the menu of columns with a secondary click on the titles.

	Each change of the selection calls SelectionChanged(), which sends the
	selection message; invoking calls ItemInvoked(), which sends the
	invocation message, both with "when" and "source". Messages go to the
	window unless a target is set.

	Coordinates of rows and columns (RowAt(), ColumnAt(), GetRowRect(),
	GetFieldRect()) are those of ScrollView(), the view the rows are drawn
	in: the latch area (LatchWidth()) is at its left, then the visible
	columns follow. A subrow's first field is indented by the latch width
	per level; its latch is drawn in the indentation.

	Differences to Haiku:
	- The list owns its rows (AddRow() takes a std::unique_ptr,
	  RemoveRow() gives it back with its subrows, Clear() destroys them)
	  and columns (AddColumn(), RemoveColumn()); a status view the same.
	- Sorting happens at once in the window's thread (Haiku sorts large
	  lists in a thread of its own); rows with a field missing sort last.
	- Only visible rows are selected: collapsing a row deselects its
	  subrows; CurrentSelection() goes through the selection in the order
	  of the rows.
	- No drag and drop of rows (InitiateDrag() and MessageDropped() do not
	  exist), no fonts per part (rows and titles use the plain font), no
	  deprecated color methods; SetEditMode() does not exist.
	- SaveState() and LoadState() keep the columns by their logical field
	  index, in a message of their own layout.
	- The menu of columns opens with any secondary click on the titles;
	  the last visible column cannot be hidden.
*/
class BColumnListView : public BView, public BInvoker {
public:
								BColumnListView(gfx::Rect frame, std::string_view name,
									BResizingMode resizingMode, BViewFlags flags,
									BBorderStyle border = BBorderStyle::None,
									bool showHorizontalScrollBar = true);
									/*!< A list at \a frame. */
								BColumnListView(std::string_view name, BViewFlags flags,
									BBorderStyle border = BBorderStyle::None,
									bool showHorizontalScrollBar = true);
									/*!< A list for a layout. */
								~BColumnListView() override;
									/*!< Destroys the rows and columns. */

	// Interaction

	virtual	void				ExpandOrCollapse(BRow* row, bool expand);
									/*!< Shows or hides the subrows of \a row; collapsing
										deselects them and moves the focus row out of
										them. */
			std::expected<void, Error> Invoke(const BMessage* message = nullptr) override;
									/*!< Sends \a message (or the invocation message) with
										"when" and "source". */
	virtual	void				ItemInvoked();
									/*!< Hook: a row was invoked; calls Invoke(). */
	virtual	void				SetInvocationMessage(std::unique_ptr<BMessage> message);
									/*!< The message ItemInvoked() sends. */
			BMessage*			InvocationMessage() const;
									/*!< The invocation message, or nullptr. */
			uint32				InvocationCommand() const;
									/*!< Its what, or 0. */

			BRow*				FocusRow() const;
									/*!< The row the keys work on, or nullptr. */
			void				SetFocusRow(int32 index, bool select = false);
									/*!< Makes top level row \a index the focus row. */
			void				SetFocusRow(BRow* row, bool select = false);
									/*!< Makes \a row the focus row, a visible row of the
										list; selects it alone with \a select. */
			void				SetMouseTrackingEnabled(bool enabled);
									/*!< Whether columns that want events get MouseMoved()
										also without a pressed button. */

	// Selection

			BListViewType		SelectionMode() const;
									/*!< Single or multiple selection. */
			void				SetSelectionMode(BListViewType mode);
									/*!< Single or multiple selection; a single selection
										keeps the first selected row. */
			void				Deselect(BRow* row);
									/*!< Deselects \a row. */
			void				AddToSelection(BRow* row);
									/*!< Selects \a row, a visible row of the list; in a
										single selection list instead of the selected one. */
			void				DeselectAll();
									/*!< Deselects all rows. */
			BRow*				CurrentSelection(BRow* lastSelected = nullptr) const;
									/*!< The first selected row after \a lastSelected
										(nullptr: the first), in the order of the rows; or
										nullptr. */
	virtual	void				SelectionChanged();
									/*!< Hook: the selection changed; sends the selection
										message. */
	virtual	void				SetSelectionMessage(std::unique_ptr<BMessage> message);
									/*!< The message SelectionChanged() sends. */
			BMessage*			SelectionMessage() const;
									/*!< The selection message, or nullptr. */
			uint32				SelectionCommand() const;
									/*!< Its what, or 0. */

	// Sorting

			void				SetSortingEnabled(bool enabled);
									/*!< Whether the rows are sorted and the titles sort
										(default yes). */
			bool				SortingEnabled() const;
									/*!< Whether sorting is enabled. */
			void				SetSortColumn(BColumn* column, bool add, bool ascending);
									/*!< Sorts by \a column, \a ascending or descending;
										with \a add after the columns it sorts by already,
										otherwise alone. */
			void				ClearSortColumns();
									/*!< Sorts by no column; the rows keep their order. */
			int32				CountSortColumns() const;
									/*!< How many columns it sorts by. */
			BColumn*			SortColumnAt(int32 index) const;
									/*!< The \a index-th column it sorts by, or nullptr. */
			bool				IsSortAscending(const BColumn* column) const;
									/*!< Whether \a column sorts ascending. */

	// Status view

			void				AddStatusView(std::unique_ptr<BView> view);
									/*!< Puts \a view left of the horizontal scroll bar, as
										wide as its frame; replaces one there. */
			std::unique_ptr<BView> RemoveStatusView();
									/*!< Takes the status view out and gives it back. */

	// Columns

			void				AddColumn(std::unique_ptr<BColumn> column,
									int32 logicalFieldIndex);
									/*!< Appends \a column, showing the fields of
										\a logicalFieldIndex (0 or more). */
			void				MoveColumn(BColumn* column, int32 index);
									/*!< Moves \a column to place \a index. */
			std::unique_ptr<BColumn> RemoveColumn(BColumn* column);
									/*!< Takes \a column out and gives it back. */
			int32				CountColumns() const;
									/*!< The number of columns, hidden ones too. */
			BColumn*			ColumnAt(int32 index) const;
									/*!< The column at place \a index, or nullptr. */
			BColumn*			ColumnAt(gfx::Point point) const;
									/*!< The visible column at \a point, or nullptr. */
			void				SetColumnVisible(BColumn* column, bool visible);
									/*!< Shows or hides \a column. */
			void				SetColumnVisible(int32 index, bool visible);
									/*!< Shows or hides the column at place \a index. */
			bool				IsColumnVisible(int32 index) const;
									/*!< Whether the column at place \a index is shown. */
			void				SetColumnFlags(BColumnFlags flags);
									/*!< What the user may do with the columns (default:
										all). */
			BColumnFlags		ColumnFlags() const;
									/*!< What the user may do with the columns. */
			void				ResizeColumnToPreferred(int32 index);
									/*!< Fits the column at place \a index to its title
										and the fields of all rows (BColumn::
										GetPreferredWidth()), the first visible column with
										their indentation. */
			void				ResizeAllColumnsToPreferred();
									/*!< Fits every column. */

	// Rows

			BRow*				RowAt(int32 index, BRow* parent = nullptr) const;
									/*!< Subrow \a index of \a parent (nullptr: of the top
										level), or nullptr. */
			BRow*				RowAt(gfx::Point point) const;
									/*!< The visible row at \a point, or nullptr. */
			bool				GetRowRect(const BRow* row, gfx::Rect* rect) const;
									/*!< Where \a row is, if it is visible. */
			bool				FindParent(const BRow* row, BRow** parent,
									bool* isVisible) const;
									/*!< The parent of \a row and whether it is visible;
										false for a top level row or one not in the list. */
			int32				IndexOf(const BRow* row) const;
									/*!< The index of \a row among its parent's subrows,
										or -1. */
			int32				CountRows(BRow* parent = nullptr) const;
									/*!< The number of subrows of \a parent (nullptr: of
										the top level). */
			bool				AddRow(std::unique_ptr<BRow> row, BRow* parent = nullptr);
									/*!< Appends \a row, with its subrows, to \a parent
										(nullptr: the top level), sorted in when sorting. */
			bool				AddRow(std::unique_ptr<BRow> row, int32 index,
									BRow* parent = nullptr);
									/*!< Inserts \a row at \a index of \a parent; sorted in
										instead when sorting. */
			bool				AddRows(std::vector<std::unique_ptr<BRow>> rows, int32 index,
									BRow* parent = nullptr);
									/*!< Inserts \a rows from \a index on. */
			std::unique_ptr<BRow> RemoveRow(BRow* row);
									/*!< Takes \a row out with its subrows and gives it
										back, deselected. */
			std::vector<std::unique_ptr<BRow>> RemoveRows(const std::vector<BRow*>& rows);
									/*!< Takes \a rows out; those in a removed row come
										with it. */
			void				UpdateRow(BRow* row);
									/*!< \a row changed: sorts it in anew and redraws. */
			bool				SwapRows(int32 index1, int32 index2, BRow* parent1 = nullptr,
									BRow* parent2 = nullptr);
									/*!< Swaps subrow \a index1 of \a parent1 with subrow
										\a index2 of \a parent2; false if one contains the
										other. */
			void				Clear();
									/*!< Destroys all rows. */
			void				InvalidateRow(BRow* row);
									/*!< Redraws \a row. */
			using				BView::ScrollTo;
			void				ScrollTo(const BRow* row);
									/*!< Scrolls the rows so that \a row is visible. */

	// Appearance

			void				SetColor(BColumnListViewColor which, gfx::Color color);
									/*!< Sets a color instead of the palette's. */
			void				ResetColors();
									/*!< All colors follow the palette again. */
			gfx::Color			Color(BColumnListViewColor which) const;
									/*!< A color, from the default palette unless set. */
			gfx::Point			SuggestTextPosition(const BRow* row,
									const BColumn* column = nullptr) const;
									/*!< Where text starts in the field of \a row and
										\a column (nullptr: the first visible one): its
										left plus a margin, on the baseline of the plain
										font centered in the row. */
			gfx::Rect			GetFieldRect(const BRow* row, const BColumn* column) const;
									/*!< Where the field of \a row and \a column is drawn;
										empty if one is not visible. */
			void				SetLatchWidth(float width);
									/*!< The width of the latch area and of each level of
										indentation (default 15; 0: no latches). */
			float				LatchWidth() const;
									/*!< The width of the latch area. */
	virtual	void				DrawLatch(BView* view, DrawContext& context, gfx::Rect frame,
									BLatchType type, BRow* row);
									/*!< Hook: draws the latch of \a row in \a frame, an
										arrow pointing right (closed) or down (open). */
			void				SaveState(BMessage* state) const;
									/*!< Adds the order, widths and visibility of the
										columns ("columnID", "columnWidth",
										"columnVisible", in order) and the sorting
										("sortID", "sortAscending", "sortingEnabled") to
										\a state. */
			void				LoadState(const BMessage& state);
									/*!< Puts the columns named in \a state (by logical
										field index) first, in its order, with its widths
										and visibility, and sorts as it says. */
			BView*				ScrollView() const;
									/*!< The view the rows are drawn in. */
			void				Refresh();
									/*!< Sorts all rows anew and redraws. */

	// BView

			void				MakeFocus(bool focus = true) override;
									/*!< Highlights the border and the selection. */
			void				Draw(DrawContext& context) override;
									/*!< The border. */
			void				AttachedToWindow() override;
									/*!< The window is the target if none is set. */
			void				WindowActivated(bool active) override;
			void				FrameResized(float newWidth, float newHeight) override;
			void				KeyDown(std::string_view bytes) override;
									/*!< Moves the focus row, selects and invokes. */
			gfx::Size			MinSize() override;
									/*!< The titles, a row and the scroll bars. */
			gfx::Size			PreferredSize() override;
									/*!< The visible columns, the titles and eight rows. */
			gfx::Size			MaxSize() override;
									/*!< Unlimited. */
			void				DoLayout() override;
									/*!< Places titles, rows, scroll bars and status view. */

	// Accessibility

			void				GetAccessibleInfo(AccessibleInfo* info) const override;
									/*!< Role Table (TreeTable if a row has subrows), with
										the number of rows (the titles' row too) and of
										visible columns. */
			void				GetAccessibleItems(std::vector<AccessibleItem>* items)
									const override;
									/*!< A Row of ColumnHeaders (the column names), then a
										Row for each shown row (Selected, Expanded or
										Collapsed, Focused for the focus row; its level in
										a TreeTable) with a Cell for each visible column
										(BColumn::GetFieldText()). */
			std::expected<void, Error> PerformAccessibleItemAction(int32 index,
									AccessibleAction action, const BMessage& data) override;
									/*!< On a column header, Click sorts as a click on the
										title. On a row or its cells, Click selects the row
										alone, Focus makes it the focus row (and focuses
										the list), Expand and Collapse show and hide its
										subrows. */

private:
	friend class BRow;
	friend class BColumn;
	friend class ColumnRowView;
	friend class ColumnTitleView;

	// A row as shown: its top in the row view and its level.
	struct VisibleRow {
			BRow*				row;
			float				top;
			int32				level;
	};

			void				_Init(BBorderStyle border, bool showHorizontalScrollBar);
			void				_LayoutChildren();
			void				_RowsChanged();
			void				_FixScrollBars();
			const std::vector<VisibleRow>& _VisibleRows() const;
			int32				_VisibleIndexOf(const BRow* row) const;
			const VisibleRow*	_VisibleRowAt(float y) const;
			float				_ContentWidth() const;
			float				_ContentHeight() const;
			float				_ColumnLeft(const BColumn* column) const;
			BColumn*			_FirstVisibleColumn() const;
			gfx::Rect			_LatchRect(const VisibleRow& row) const;
			gfx::Rect			_FieldRect(const VisibleRow& row, const BColumn* column) const;
			float				_TitleHeight() const;
			gfx::Color			_Color(BColumnListViewColor which, const Palette& palette) const;

			std::vector<std::unique_ptr<BRow>>* _Siblings(BRow* parent);
			const std::vector<std::unique_ptr<BRow>>* _Siblings(const BRow* parent) const;
			bool				_Owns(const BRow* row) const;
			bool				_Sorting() const;
			bool				_RowLess(const BRow* a, const BRow* b) const;
			void				_SortRows(std::vector<std::unique_ptr<BRow>>& rows);
			void				_SortAll();
			int32				_InsertIndex(const std::vector<std::unique_ptr<BRow>>& rows,
									const BRow* row, int32 index) const;
			void				_Attach(BRow* row, BRow* parent);
			std::unique_ptr<BRow> _RemoveRow(BRow* row, bool* selectionChanged);
			bool				_DeselectHidden();
			bool				_DeselectSubtree(BRow* row, bool includeSelf);
			void				_ForgetSubtree(BRow* row);
			void				_FixFocusAfterHiding(int32 visibleIndex);

			bool				_SelectOnly(BRow* row);
			bool				_SelectRange(BRow* from, BRow* to, bool extend);
			void				_NotifySelection();
			void				_ClickRow(BRow* row, BModifierFlags modifiers);
			void				_MoveFocus(int32 visibleIndex, BModifierFlags modifiers);
			void				_TitleClicked(BColumn* column, BModifierFlags modifiers);
			int32				_IndexOfColumn(const BColumn* column) const;
			int32				_CountVisibleColumns() const;
			BModifierFlags		_Modifiers() const;

			std::vector<std::unique_ptr<BColumn>> fColumns;
			std::vector<BColumn*> fSortColumns;
			std::vector<std::unique_ptr<BRow>> fRows;
	mutable	std::vector<VisibleRow> fVisibleRows;
	mutable	bool				fVisibleRowsValid = false;
			std::array<std::optional<gfx::Color>, kColumnListViewColorCount> fColors;
			ColumnTitleView*	fTitleView = nullptr;
			ColumnRowView*		fRowView = nullptr;
			BScrollBar*			fHorizontalScrollBar = nullptr;
			BScrollBar*			fVerticalScrollBar = nullptr;
			BView*				fStatusView = nullptr;
			BRow*				fFocusRow = nullptr;
			BRow*				fAnchorRow = nullptr;
			std::unique_ptr<BMessage> fSelectionMessage;
			BListViewType		fSelectionMode = BListViewType::MultipleSelection;
			BBorderStyle		fBorderStyle = BBorderStyle::None;
			float				fLatchWidth = 15.0f;
			bool				fSortingEnabled = true;
			BColumnFlags		fColumnFlags = BColumnFlag::Move | BColumnFlag::Resize
									| BColumnFlag::PopUp | BColumnFlag::Remove;
			bool				fMouseTracking = false;
};


} // namespace ngui
