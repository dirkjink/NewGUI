/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <optional>

#include <ngui/Clock.h>
#include <ngui/ColumnListView.h>
#include <ngui/ScrollBar.h>
#include <ngui/TextSystem.h>


namespace ngui {


// The titles of a BColumnListView: scrolled horizontally with the rows. A
// click on a title sorts by its column; dragging its right edge resizes it
// (a double click there fits it), dragging the title moves the column or,
// off the titles, hides it; a secondary click opens the menu of columns.
class ColumnTitleView : public BView {
public:
								ColumnTitleView(BColumnListView* master);

			void				Draw(DrawContext& context) override;
			void				MouseDown(gfx::Point where) override;
			void				MouseUp(gfx::Point where) override;
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
			void				MessageReceived(BMessage* message) override;

			// A column is being removed: forget it.
			void				ForgetColumn(const BColumn* column);

	// The message of the items of the menu of columns, with "index".
	static constexpr std::uint32_t kToggleColumn = 0x5f43544c;	// '_CTL'

private:
			BColumn*			_EdgeAt(float x) const;
			void				_OpenMenu(gfx::Point where);
			void				_UpdateCursor(gfx::Point where);

			BColumnListView*	fMaster;
			BColumn*			fPressed = nullptr;
			bool				fPressedInside = false;
			bool				fMoving = false;
			float				fDragStart = 0;
			BColumn*			fResizing = nullptr;
			float				fResizeOffset = 0;
			BColumn*			fLastEdge = nullptr;
			std::optional<Clock::Time> fLastEdgeTime;
			bool				fResizeCursor = false;
};


// The rows of a BColumnListView, the target of its scroll bars.
class ColumnRowView : public BView {
public:
								ColumnRowView(BColumnListView* master);

			void				Draw(DrawContext& context) override;
			void				MouseDown(gfx::Point where) override;
			void				MouseUp(gfx::Point where) override;
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
			void				FrameResized(float newWidth, float newHeight) override;

			// A row is being removed: forget it (nullptr: all rows).
			void				ForgetRow(const BRow* row);
			// A column is being removed: forget it.
			void				ForgetColumn(const BColumn* column);

private:
			bool				_Highlighted() const;

			BColumnListView*	fMaster;
			BRow*				fLatchRow = nullptr;
			bool				fLatchPressed = false;
			BColumn*			fEventColumn = nullptr;
			BRow*				fEventRow = nullptr;
			BRow*				fLastClickRow = nullptr;
			std::optional<Clock::Time> fLastClickTime;
			gfx::Point			fLastClickPoint;
};


// The baseline of a line of the plain font centered vertically in rect.
float							ColumnTextBaseline(const gfx::Rect& rect, const TextSystem& text);


// The horizontal scroll bar: scrolls the titles along with the rows.
class ColumnScrollBar : public BScrollBar {
public:
								ColumnScrollBar(BView* target, BView* titles);

			void				ValueChanged(float newValue) override;

private:
			BView*				fTitles;
};


} // namespace ngui
