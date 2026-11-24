/*
 * Copyright 2007-2020, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/TextLayout.h>
#include <ngui/View.h>
#include <ngui/gfx/Region.h>


namespace ngui {


class BMessageRunner;


/*!	A view that shows and edits text (Haiku's BTextView, module W5): lines
	wrapped at its width (or not), a selection, a blinking caret, editing
	with the keyboard (arrows, Home/End, Page Up/Down, Backspace, Delete,
	with Shift to select and Command or Option to go by words or lines),
	selecting with the mouse (double click: words, triple click: lines),
	scrolling with its scroll bars. Offsets are byte offsets into the UTF-8
	text; the caret moves by graphemes.

	Differences to Haiku: one font (the plain one) and color for all of the
	text; the text is laid out by TextLayout, again after each change (Haiku:
	only the changed lines), and is kept in a std::string (Haiku: a gap
	buffer). The selection and the caret are drawn by Draw() (Haiku
	inverted them on the screen); the caret blinks on a timer of its own
	(Haiku: Pulse()). CountLines() counts the empty line after a final line
	break. Not yet: the clipboard (Cut(), Copy(), Paste()), Undo(), input
	methods, drag and drop of text, the context menu, styles and run
	arrays, HideTyping(), MakeResizable(), auto scrolling while selecting
	and the I-beam cursor.
*/
class BTextView : public BView {
public:
								BTextView(gfx::Rect frame, std::string_view name,
									gfx::Rect textRect,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::PulseNeeded);
									/*!< A text view at \a frame whose text goes into
										\a textRect (its own coordinates). */
	explicit					BTextView(std::string_view name,
									BViewFlags flags = BViewFlag::WillDraw
										| BViewFlag::PulseNeeded);
									/*!< A text view for a layout. */
								~BTextView() override;
									/*!< Destroys the view and its text. */

			void				AttachedToWindow() override;
			void				DetachedFromWindow() override;
			void				Draw(DrawContext& context) override;
									/*!< The text, the selection (inverted, while active)
										and the caret. */
			void				MouseDown(gfx::Point where) override;
									/*!< Places the caret, selects words (double click) or
										lines (triple click), extends with Shift. */
			void				MouseUp(gfx::Point where) override;
			void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage) override;
									/*!< Extends the selection while the button is down. */
			void				WindowActivated(bool active) override;
			void				KeyDown(std::string_view bytes) override;
									/*!< Edits the text and moves the caret. */
			void				FrameResized(float newWidth, float newHeight) override;
			void				MakeFocus(bool focus = true) override;
			void				MessageReceived(BMessage* message) override;
									/*!< kSelectAll and the navigation shortcuts it
										installs while active. */

			void				SetText(std::string_view text);
									/*!< Replaces the text; the caret goes to 0. */
			void				Insert(std::string_view text);
									/*!< Inserts \a text at the selection's start. */
			void				Insert(int32 offset, std::string_view text);
									/*!< Inserts \a text at \a offset. */
			void				Delete();
									/*!< Deletes the selection. */
			void				Delete(int32 startOffset, int32 endOffset);
									/*!< Deletes the text from \a startOffset to
										\a endOffset. */

			std::string_view	Text() const;
									/*!< The text. */
			int32				TextLength() const;
									/*!< Its length in bytes. */
			std::string			GetText(int32 offset, int32 length) const;
									/*!< \a length bytes from \a offset. */
			uint8				ByteAt(int32 offset) const;
									/*!< The byte at \a offset, 0 outside the text. */

			int32				CountLines() const;
									/*!< The number of lines. */
			int32				CurrentLine() const;
									/*!< The line of the selection's start. */
			void				GoToLine(int32 lineNumber);
									/*!< Puts the caret at the start of a line. */

			void				Clear();
									/*!< Deletes the selection (Haiku: as B_CLEAR, for
										undo). */
	virtual	void				Select(int32 startOffset, int32 endOffset);
									/*!< Selects the text between the offsets; equal
										offsets place the caret. */
			void				SelectAll();
									/*!< Selects all of the text. */
			void				GetSelection(int32* start, int32* end) const;
									/*!< The selection; 0, 0 if not selectable. */

			void				AdoptSystemColors();
									/*!< The document colors of the palette (a tint darker
										when not editable). */

			int32				LineAt(int32 offset) const;
									/*!< The line of \a offset. */
			int32				LineAt(gfx::Point point) const;
									/*!< The line at \a point. */
			gfx::Point			PointAt(int32 offset, float* height = nullptr) const;
									/*!< The top of the caret at \a offset, and the line's
										height. */
			int32				OffsetAt(gfx::Point point) const;
									/*!< The offset nearest to \a point. */
			int32				OffsetAt(int32 line) const;
									/*!< The first offset of \a line. */
	virtual	void				FindWord(int32 offset, int32* fromOffset,
									int32* toOffset);
									/*!< The word around \a offset, as a double click
										selects it. */

			float				LineWidth(int32 lineNumber = 0) const;
									/*!< The width of a line without its line break. */
			float				LineHeight(int32 lineNumber = 0) const;
									/*!< The height of a line. */
			float				TextHeight(int32 startLine, int32 endLine) const;
									/*!< The height of lines \a startLine to \a endLine. */
			void				GetTextRegion(int32 startOffset, int32 endOffset,
									gfx::Region* outRegion) const;
									/*!< The area the text between the offsets takes, as
										the selection is drawn. */

	virtual	void				ScrollToOffset(int32 offset);
									/*!< Scrolls so that \a offset is visible. */
			void				ScrollToSelection();
									/*!< Scrolls to the selection's start. */
			void				Highlight(int32 startOffset, int32 endOffset);
									/*!< Redraws the text between the offsets (Haiku
										inverted it). */

			void				SetTextRect(gfx::Rect rect);
									/*!< Where the text goes; the insets follow. */
			gfx::Rect			TextRect() const;
									/*!< Where the text is. */
			void				SetInsets(float left, float top, float right,
									float bottom);
									/*!< The space between the bounds and the text. */
			void				GetInsets(float* left, float* top, float* right,
									float* bottom) const;
									/*!< The insets; pointers may be nullptr. */

			void				SetTabWidth(float width);
									/*!< The distance of tab stops (28 by default). */
			float				TabWidth() const;
									/*!< The distance of tab stops. */
			void				MakeSelectable(bool selectable = true);
									/*!< Whether the text can be selected. */
			bool				IsSelectable() const;
									/*!< Whether the text can be selected. */
			void				MakeEditable(bool editable = true);
									/*!< Whether the text can be changed. */
			bool				IsEditable() const;
									/*!< Whether the text can be changed. */
			void				SetWordWrap(bool wrap);
									/*!< Whether lines wrap at the text rectangle. */
			bool				DoesWordWrap() const;
									/*!< Whether lines wrap. */
			void				SetMaxBytes(int32 max);
									/*!< The most bytes the text may have; longer text is
										cut. */
			int32				MaxBytes() const;
									/*!< The most bytes the text may have. */
			void				DisallowChar(uint32 character);
									/*!< \a character cannot be typed. */
			void				AllowChar(uint32 character);
									/*!< \a character can be typed again. */
			void				SetAlignment(BHorizontalAlignment alignment);
									/*!< Left, Center or Right. */
			BHorizontalAlignment Alignment() const;
									/*!< The lines' alignment. */
			void				SetAutoindent(bool state);
									/*!< A new line starts with the white space of the
										one before. */
			bool				DoesAutoindent() const;
									/*!< Whether new lines are indented. */

			void				ResizeToPreferred() override;
			void				GetPreferredSize(float* width, float* height) override;
			gfx::Size			MinSize() override;
									/*!< Three lines' height wide, one line high, with the
										insets; without wrapping as wide as the text. */
			gfx::Size			MaxSize() override;
									/*!< Unlimited. */
			gfx::Size			PreferredSize() override;
			bool				HasHeightForWidth() override;
									/*!< True if not editable. */
			void				GetHeightForWidth(float width, float* min, float* max,
									float* preferred) override;

protected:
	virtual	void				InsertText(std::string_view text, int32 offset);
									/*!< Hook: puts \a text into the text at \a offset;
										the selection follows. */
	virtual	void				DeleteText(int32 fromOffset, int32 toOffset);
									/*!< Hook: takes the bytes out of the text; the
										selection follows. */

			void				LayoutInvalidated(bool descendants) override;
			void				DoLayout() override;

private:
	struct LayoutData {
		float				leftInset = 0;
		float				topInset = 0;
		float				rightInset = 0;
		float				bottomInset = 0;
		bool				overridden = false;
		gfx::Size			min = {0, 0};
		gfx::Size			preferred = {0, 0};
		bool				valid = false;
	};

	// What MouseDown() starts and MouseMoved() follows (Haiku's
	// TextTrackState). No default member initializers: Clang does not
	// count a nested class with them as constructible inside BTextView.
	struct TrackState {
		int32				clickOffset;
		bool				shiftDown;
		int32				anchor;
		int32				selStart;
		int32				selEnd;
	};

			void				_InitObject(gfx::Rect textRect);
			void				_ValidateLayoutData();
			void				_ResetTextRect();
			void				_Relayout();
			void				_Refresh(std::optional<int32> scrollTo = std::nullopt);
			void				_HandleBackspace(int32 modifiers = -1);
			void				_HandleArrowKey(uint8 arrowKey, int32 modifiers = -1);
			void				_HandleDelete(int32 modifiers = -1);
			void				_HandlePageKey(uint8 pageKey, int32 modifiers = -1);
			void				_HandleAlphaKey(std::string_view bytes);
			void				_DoInsertText(std::string_view text, int32 offset);
			void				_ShowCaret();
			void				_HideCaret();
			void				_InvalidateCaret();
			void				_StartBlinking();
			void				_StopMouseTracking();
			bool				_PerformMouseUp(gfx::Point where);
			bool				_PerformMouseMoved(gfx::Point where);
			void				_UpdateScrollbars();
			void				_ScrollBy(float horizontal, float vertical);
			void				_ScrollTo(float x, float y);
			void				_Activate();
			void				_Deactivate();
			int32				_CurrentModifiers() const;
			uint32				_CharClassification(int32 offset) const;
			int32				_NextInitialByte(int32 offset) const;
			int32				_PreviousInitialByte(int32 offset) const;
			int32				_PreviousLineStart(int32 offset);
			int32				_NextLineEnd(int32 offset);
			int32				_PreviousWordBoundary(int32 offset);
			int32				_NextWordBoundary(int32 offset);
			int32				_PreviousWordStart(int32 offset);
			int32				_NextWordEnd(int32 offset);
			bool				_IsOnEmptyLastLine(int32 offset) const;
			void				_UpdateInsets(const gfx::Rect& rect);
			float				_TextWidth() const;
			float				_TextHeight() const;
			float				_UneditableTint() const;

			std::string			fText;
			TextLayout			fLayout;
			gfx::Rect			fTextRect = {0, 0, 0, 0};
			int32				fSelStart = 0;
			int32				fSelEnd = 0;
			bool				fCaretVisible = false;
			int32				fCaretOffset = 0;
			int32				fClickCount = 0;
			std::optional<std::int64_t> fClickTime;
			bool				fActive = false;
			float				fTabWidth = 28;
			bool				fSelectable = true;
			bool				fEditable = true;
			bool				fWrap = true;
			int32				fMaxBytes = INT32_MAX;
			std::vector<uint32>	fDisallowedChars;
			BHorizontalAlignment fAlignment = BHorizontalAlignment::Left;
			bool				fAutoindent = false;
			std::optional<TrackState> fTrackingMouse;
			LayoutData			fLayoutData;
			int32				fLastClickOffset = -1;
			std::unique_ptr<BMessageRunner> fCaretRunner;
			std::vector<std::pair<uint32, BModifierFlags>> fInstalledShortcuts;
};


} // namespace ngui
