/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/TextView.cpp (commit b1d497bb69),
 * the single-style core: text, selection, caret, keyboard, mouse,
 * scrolling, layout, the clipboard and undo (with
 * textview_support/UndoBuffer.cpp); without archiving, scripting, styles,
 * input methods, drag and drop and the offscreen bitmap.
 * NewGUI (see TextView.h):
 * - TextLayout breaks the lines and answers PointAt() and OffsetAt();
 *   _Refresh() lays out all of the text again.
 * - The caret moves by graphemes (_NextInitialByte() and
 *   _PreviousInitialByte() step over them, Haiku: over code points). In
 *   text with right-to-left runs (module X3) the left and right arrows
 *   move visually (TextLayout::CaretLeft() and CaretRight()), a selection
 *   grows from its anchor, and GetTextRegion() gives the pieces of the
 *   range on the screen (TextLayout::SelectionRects()).
 * - Highlight(), the caret and the selection invalidate; Draw() fills the
 *   selection with the inverted colors and draws its text inverted
 *   (Haiku: B_OP_INVERT), while the view is active.
 * - The caret blinks on a BMessageRunner (Haiku: Pulse() at 500 ms).
 * - A click in the selection is a click: without drag and drop there is
 *   nothing to drag (Haiku: _InitiateDrag()); no auto scrolling while
 *   selecting, no I-beam cursor, no context menu.
 * - ScrollToOffset() does not realign a text that does not wrap.
 * - Sizes are pixel counts; Haiku's inclusive rectangles become
 *   half-open ones with the same edges.
 * - Typing undo erases and restores graphemes (Haiku: code points), as
 *   Backspace and Delete remove them.
 * - Input methods: kInputMethodEvent as Haiku's B_INPUT_METHOD_EVENT, but
 *   without an input method messenger (the platform composes; no location
 *   replies, the text input area goes to the platform window instead);
 *   kInputMethodStopped takes an active composition's text out (its final
 *   text comes as key events), and while composing the view leaves the
 *   input method's control keys (Backspace, arrows, Enter ...) alone.
 *
 * Authors (Haiku):
 *		Hiroshi Lockheimer (BTextView is based on his STEngine)
 *		John Scipione, jscipione@gmail.com
 *		Ingo Weinhold, ingo_weinhold@gmx.de
 *		Stephan Aßmus, superstippi@gmx.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		Oliver Tappe, zooey@hirschkaefer.de
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Marc Flerackers, mflerackers@androme.be
 */


#include <ngui/TextView.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <span>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Clipboard.h>
#include <ngui/Clock.h>
#include <ngui/ControlLook.h>
#include <ngui/Cursor.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/MessageRunner.h>
#include <ngui/Messenger.h>
#include <ngui/Palette.h>
#include <ngui/ScrollBar.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>

#include "TextStyleBuffer.h"
#include "compat/InterfaceDefs.h"


namespace ngui {


using namespace std::chrono_literals;


namespace {


constexpr float kHorizontalScrollBarStep = 10.0f;
constexpr float kVerticalScrollBarStep = 12.0f;

constexpr std::uint32_t kMsgNavigateArrow = FourCharCode("_NvA");
constexpr std::uint32_t kMsgNavigatePage = FourCharCode("_NvP");
constexpr std::uint32_t kMsgRemoveWord = FourCharCode("_RmW");
// NewGUI: the caret's timer (Haiku: Pulse())
constexpr std::uint32_t kMsgCaretBlink = FourCharCode("_TVB");

constexpr Clock::Time kCaretBlinkRate = 500ms;
// Haiku's default click speed (get_click_speed())
constexpr Clock::Time kClickSpeed = 500ms;

enum {
	CHAR_CLASS_DEFAULT,
	CHAR_CLASS_WHITESPACE,
	CHAR_CLASS_GRAPHICAL,
	CHAR_CLASS_QUOTE,
	CHAR_CLASS_PUNCTUATION,
	CHAR_CLASS_PARENS_OPEN,
	CHAR_CLASS_PARENS_CLOSE,
	CHAR_CLASS_END_OF_TEXT
};


// The shortcuts _Activate() installs, in pairs that are installed only if
// the window has neither (Haiku's fInstalled... flags).
struct NavigationShortcut {
	uint8				key;
	BModifierFlags		modifiers;
	std::uint32_t		what;
};

const NavigationShortcut kNavigationShortcuts[][2] = {
	{{keys::kLeftArrow, BModifier::Command, kMsgNavigateArrow},
		{keys::kRightArrow, BModifier::Command, kMsgNavigateArrow}},
	{{keys::kLeftArrow, BModifier::Command | BModifier::Shift, kMsgNavigateArrow},
		{keys::kRightArrow, BModifier::Command | BModifier::Shift, kMsgNavigateArrow}},
	{{keys::kDelete, BModifier::Command, kMsgRemoveWord},
		{keys::kBackspace, BModifier::Command, kMsgRemoveWord}},
	{{keys::kLeftArrow, BModifier::Option, kMsgNavigateArrow},
		{keys::kRightArrow, BModifier::Option, kMsgNavigateArrow}},
	{{keys::kLeftArrow, BModifier::Option | BModifier::Shift, kMsgNavigateArrow},
		{keys::kRightArrow, BModifier::Option | BModifier::Shift, kMsgNavigateArrow}},
	{{keys::kDelete, BModifier::Option, kMsgRemoveWord},
		{keys::kBackspace, BModifier::Option, kMsgRemoveWord}},
	{{keys::kUpArrow, BModifier::Option, kMsgNavigateArrow},
		{keys::kDownArrow, BModifier::Option, kMsgNavigateArrow}},
	{{keys::kUpArrow, BModifier::Option | BModifier::Shift, kMsgNavigateArrow},
		{keys::kDownArrow, BModifier::Option | BModifier::Shift, kMsgNavigateArrow}},
	{{keys::kHome, BModifier::Command, kMsgNavigatePage},
		{keys::kEnd, BModifier::Command, kMsgNavigatePage}},
	{{keys::kHome, BModifier::Command | BModifier::Shift, kMsgNavigatePage},
		{keys::kEnd, BModifier::Command | BModifier::Shift, kMsgNavigatePage}},
};


gfx::Color
Inverted(const gfx::Color& color)
{
	return {1 - color.r, 1 - color.g, 1 - color.b, color.a};
}


// Haiku's colors of the text an input method composes.
constexpr gfx::Color kBlueInputColor = gfx::Color::FromRgba8(152, 203, 255);
constexpr gfx::Color kRedInputColor = gfx::Color::FromRgba8(255, 152, 152);


// The clipboard's text as bytes.
std::span<const std::byte>
Bytes(std::string_view text)
{
	return std::as_bytes(std::span(text.data(), text.size()));
}


// The type of the runs on the clipboard.
constexpr std::string_view kTextRunArrayType = "application/x-vnd.Be-text_run_array";


} // namespace


// #pragma mark - UndoBuffer


// Ported from Haiku: src/kits/interface/textview_support/UndoBuffer.cpp
// (commit b1d497bb69), with std::string; the DropUndoBuffer for drops from
// other applications only (NewGUI drags no text out of a view yet). The
// runs are kept as the style buffer has them (colors that follow the text
// color included).
class BTextView::UndoBuffer {
public:
	UndoBuffer(BTextView* textView, BUndoState state)
		:
		fTextView(textView),
		fState(state)
	{
		fTextView->GetSelection(&fStart, &fEnd);
		fTextData = fTextView->GetText(fStart, fEnd - fStart);
		if (fTextView->IsStylable())
			fRunArray = fTextView->_StyleRuns(fStart, fEnd);
	}

	virtual ~UndoBuffer() = default;

	void Undo(BClipboard* clipboard)
	{
		fRedo ? RedoSelf(clipboard) : UndoSelf(clipboard);

		fRedo = !fRedo;
	}

	BUndoState State(bool* isRedo) const
	{
		if (isRedo != nullptr)
			*isRedo = fRedo;

		return fState;
	}

protected:
	virtual void UndoSelf(BClipboard* /*clipboard*/)
	{
		fTextView->Select(fStart, fStart);
		fTextView->_InsertStyled(fStart, fTextData, fRunArray);
		fTextView->Select(fStart, fStart);
	}

	virtual void RedoSelf(BClipboard* /*clipboard*/)
	{
	}

	BTextView*			fTextView;
	int32				fStart = 0;
	int32				fEnd = 0;
	std::string			fTextData;
	std::vector<TextStyleRunEntry> fRunArray;
	bool				fRedo = false;

private:
	BUndoState			fState;
};


class BTextView::CutUndoBuffer : public BTextView::UndoBuffer {
public:
	explicit CutUndoBuffer(BTextView* textView)
		:
		UndoBuffer(textView, BUndoState::Cut)
	{
	}

protected:
	void RedoSelf(BClipboard* clipboard) override
	{
		fTextView->Select(fStart, fStart);
		fTextView->Delete(fStart, fEnd);
		if (clipboard->Lock()) {
			(void)clipboard->Clear();
			if (BMessage* clip = clipboard->Data()) {
				(void)clip->AddData("text/plain", BTypeCode::Mime, Bytes(fTextData));
				if (!fRunArray.empty()) {
					(void)clip->AddData(kTextRunArrayType, BTypeCode::Mime,
						FlattenRunArray(fTextView->_ResolvedRuns(fRunArray)));
				}
				(void)clipboard->Commit();
			}
			clipboard->Unlock();
		}
	}
};


class BTextView::PasteUndoBuffer : public BTextView::UndoBuffer {
public:
	PasteUndoBuffer(BTextView* textView, std::string_view text,
		std::optional<BTextRunArray> runArray)
		:
		UndoBuffer(textView, BUndoState::Paste),
		fPasteText(text),
		fPasteRunArray(std::move(runArray))
	{
	}

protected:
	void UndoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fStart, fStart);
		fTextView->Delete(fStart, fStart + static_cast<int32>(fPasteText.size()));
		fTextView->_InsertStyled(fStart, fTextData, fRunArray);
		fTextView->Select(fStart, fEnd);
	}

	void RedoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fStart, fStart);
		fTextView->Delete(fStart, fEnd);
		fTextView->Insert(fPasteText, fPasteRunArray.has_value() ? &*fPasteRunArray : nullptr);
		const int32 end = fStart + static_cast<int32>(fPasteText.size());
		fTextView->Select(end, end);
	}

private:
	std::string			fPasteText;
	std::optional<BTextRunArray> fPasteRunArray;
};


class BTextView::DropUndoBuffer : public BTextView::UndoBuffer {
public:
	DropUndoBuffer(BTextView* textView, std::string_view text, int32 location)
		:
		UndoBuffer(textView, BUndoState::Drop),
		fDropText(text),
		fDropLocation(location)
	{
	}

protected:
	void UndoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fDropLocation, fDropLocation);
		fTextView->Delete(fDropLocation,
			fDropLocation + static_cast<int32>(fDropText.size()));
		fTextView->Select(fStart, fEnd);
	}

	void RedoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fDropLocation, fDropLocation);
		fTextView->Insert(fDropText);
		fTextView->Select(fDropLocation, fDropLocation + static_cast<int32>(fDropText.size()));
	}

private:
	std::string			fDropText;
	int32				fDropLocation;
};


class BTextView::ClearUndoBuffer : public BTextView::UndoBuffer {
public:
	explicit ClearUndoBuffer(BTextView* textView)
		:
		UndoBuffer(textView, BUndoState::Clear)
	{
	}

protected:
	void RedoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fStart, fStart);
		fTextView->Delete(fStart, fEnd);
	}
};


class BTextView::TypingUndoBuffer : public BTextView::UndoBuffer {
public:
	explicit TypingUndoBuffer(BTextView* textView)
		:
		UndoBuffer(textView, BUndoState::Typing),
		fTypedStart(fStart),
		fTypedEnd(fEnd)
	{
	}

	void InputCharacter(int32 length)
	{
		int32 start;
		int32 end;
		fTextView->GetSelection(&start, &end);

		if (start != fTypedEnd || end != fTypedEnd)
			_Reset();

		fTypedEnd += length;
	}

	void BackwardErase()
	{
		int32 start;
		int32 end;
		fTextView->GetSelection(&start, &end);

		// NewGUI: the grapheme Backspace removes
		const int32 charLen = start - fTextView->_PreviousInitialByte(start);

		if (start != fTypedEnd || end != fTypedEnd) {
			_Reset();
			// if we've got a selection, we're already done
			if (start != end)
				return;
		}

		fTypedStart = start - charLen;
		fTextData.insert(0, fTextView->GetText(fTypedStart, charLen));
		fTypedEnd -= charLen;
	}

	void ForwardErase()
	{
		// TODO: Cleanup
		int32 start;
		int32 end;
		fTextView->GetSelection(&start, &end);

		// NewGUI: the grapheme Delete removes
		const int32 charLen = fTextView->_NextInitialByte(start) - start;

		if (start != fTypedEnd || end != fTypedEnd || fUndone > 0) {
			_Reset();
			// if we've got a selection, we're already done
			if (fStart == fEnd)
				fTextData = fTextView->GetText(start, charLen);
		} else
			fTextData += fTextView->GetText(start, charLen);
	}

protected:
	void UndoSelf(BClipboard* /*clipboard*/) override
	{
		fTypedText = fTextView->GetText(fTypedStart, fTypedEnd - fTypedStart);

		fTextView->Select(fTypedStart, fTypedStart);
		fTextView->Delete(fTypedStart, fTypedEnd);
		fTextView->Insert(fTextData);
		fTextView->Select(fStart, fEnd);
		fUndone++;
	}

	void RedoSelf(BClipboard* /*clipboard*/) override
	{
		fTextView->Select(fTypedStart, fTypedStart);
		fTextView->Delete(fTypedStart, fTypedStart + static_cast<int32>(fTextData.size()));
		fTextView->Insert(fTypedText);
		fUndone--;
	}

private:
	void _Reset()
	{
		fTextView->GetSelection(&fStart, &fEnd);
		fTextData = fTextView->GetText(fStart, fEnd - fStart);
		fTypedStart = fStart;
		fTypedEnd = fStart;

		fTypedText.clear();

		fRedo = false;
		fUndone = 0;
	}

	std::string			fTypedText;
	int32				fTypedStart;
	int32				fTypedEnd;
	int32				fUndone = 0;
};


// #pragma mark - BTextView


BTextView::BTextView(gfx::Rect frame, std::string_view name, gfx::Rect textRect,
	BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitObject(textRect);
}


BTextView::BTextView(std::string_view name, BViewFlags flags)
	:
	BView(name, flags)
{
	_InitObject(Bounds());
}


BTextView::~BTextView()
{
}


void
BTextView::AttachedToWindow()
{
	BView::AttachedToWindow();

	fCaretVisible = false;
	fClickCount = 0;
	fClickTime.reset();
	fActive = false;

	_Refresh();
}


void
BTextView::DetachedFromWindow()
{
	// NewGUI: the shortcuts and the timer belong to the window
	if (fActive)
		_Deactivate();
	_StopMouseTracking();
	BView::DetachedFromWindow();
}


void
BTextView::Draw(DrawContext& context)
{
	const gfx::Color text = fTextColor.value_or(context.palette.Get(BColorWhich::DocumentText));
	const gfx::Color background = ViewColor().value_or(
		context.palette.Get(BColorWhich::DocumentBackground));
	const gfx::Point origin{fTextRect.x0, fTextRect.y0};

	fLayout.Draw(context.canvas, origin, text);

	if (fInline != nullptr && fInline->active) {
		// the composed text on blue, its selected part on red (Haiku's
		// _DrawLine())
		const auto paint = [&](int32 from, int32 to, const gfx::Color& color) {
			gfx::Region region;
			GetTextRegion(from, to, &region);
			for (int32 i = 0; i < region.CountRects(); i++) {
				const gfx::Rect rect = region.RectAt(i).ToRect();
				context.canvas.Save();
				context.canvas.ClipRect(rect);
				context.canvas.FillRect(rect, gfx::Paint{.color = color});
				fLayout.Draw(context.canvas, origin, text);
				context.canvas.Restore();
			}
		};
		paint(fInline->offset, fInline->offset + fInline->length, kBlueInputColor);
		if (fInline->selectionLength > 0) {
			const int32 selection = fInline->offset + fInline->selectionOffset;
			paint(selection, selection + fInline->selectionLength, kRedInputColor);
		}
	}

	if (!fActive)
		return;

	if (fSelStart != fSelEnd) {
		if (!fSelectable)
			return;

		// Haiku's Highlight(): B_OP_INVERT over the selection
		gfx::Region region;
		GetTextRegion(fSelStart, fSelEnd, &region);
		for (int32 i = 0; i < region.CountRects(); i++) {
			const gfx::Rect rect = region.RectAt(i).ToRect();
			context.canvas.Save();
			context.canvas.ClipRect(rect);
			context.canvas.FillRect(rect, gfx::Paint{.color = Inverted(background)});
			fLayout.Draw(context.canvas, origin, text, 0, -1, Inverted);
			context.canvas.Restore();
		}
	} else if (fCaretVisible && (fEditable || fSelectable)) {
		// Haiku's _DrawCaret(): a line of the line's height, inverted
		float lineHeight;
		gfx::Point caret = PointAt(fSelStart, &lineHeight);
		caret.x = std::min(caret.x, fTextRect.x1 - 1);
		context.canvas.FillRect({caret.x, caret.y, caret.x + 1, caret.y + lineHeight},
			gfx::Paint{.color = Inverted(background)});
	}
}


void
BTextView::MouseDown(gfx::Point where)
{
	if (!fEditable && !fSelectable)
		return;

	_CancelInputMethod();

	if (!IsFocus())
		MakeFocus();

	_HideCaret();

	_StopMouseTracking();

	int32 modifiers = 0;
	int32 buttons = 0;
	if (Window() != nullptr && Window()->CurrentMessage() != nullptr) {
		modifiers = Window()->CurrentMessage()->GetInt32("modifiers", 0);
		buttons = Window()->CurrentMessage()->GetInt32("buttons", 0);
	}

	if (buttons == 2) {
		// NewGUI: no context menu yet (Haiku's _ShowContextMenu())
		return;
	}

	fTrackingMouse = TrackState{0, false, 0, 0, 0};
	fTrackingMouse->clickOffset = OffsetAt(where);
	fTrackingMouse->shiftDown
		= BModifierFlags::FromRaw(static_cast<uint32>(modifiers)).Has(BModifier::Shift);

	const Clock::Time clickTime = Clock::Default().Now();
	const bool multipleClick = fClickTime.has_value()
		&& clickTime - std::chrono::microseconds(*fClickTime) < kClickSpeed
		&& fLastClickOffset == fTrackingMouse->clickOffset;
	const auto clickMicroseconds
		= std::chrono::duration_cast<std::chrono::microseconds>(clickTime).count();

	SetMouseEventMask(BEventMask::Pointer);

	if (multipleClick) {
		if (fClickCount > 3) {
			fClickCount = 0;
			fClickTime.reset();
		} else {
			fClickCount++;
			fClickTime = clickMicroseconds;
		}
	} else if (!fTrackingMouse->shiftDown) {
		fLastClickOffset = fCaretOffset = fTrackingMouse->clickOffset;
		fClickCount = 1;
		fClickTime = clickMicroseconds;

		// Deselect any previously selected text
		Select(fTrackingMouse->clickOffset, fTrackingMouse->clickOffset);
	}

	if (!fSelectable) {
		_StopMouseTracking();
		return;
	}

	int32 offset = fSelStart;
	if (fTrackingMouse->clickOffset > fSelStart)
		offset = fSelEnd;

	fTrackingMouse->anchor = offset;

	MouseMoved(where, BMouseTransit::Inside, nullptr);
}


void
BTextView::MouseUp(gfx::Point where)
{
	BView::MouseUp(where);
	_PerformMouseUp(where);
}


void
BTextView::MouseMoved(gfx::Point where, BMouseTransit transit, const BMessage* dragMessage)
{
	// Haiku's _TrackMouse(): the I-beam where text can be selected.
	// NewGUI: also over the selection, which cannot be dragged yet (Haiku
	// showed the arrow there).
	if (transit == BMouseTransit::Exited)
		SetViewCursor(BCursor(BCursorID::SystemDefault));
	else if (fSelectable || fEditable)
		SetViewCursor(BCursor(BCursorID::IBeam));
	else
		SetViewCursor(BCursor(BCursorID::SystemDefault));

	// Check if it's a "click'n'move"
	if (_PerformMouseMoved(where))
		return;

	BView::MouseMoved(where, transit, dragMessage);
}


void
BTextView::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (active && IsFocus()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}
}


void
BTextView::KeyDown(std::string_view bytes)
{
	if (bytes.empty())
		return;

	const auto keyPressed = static_cast<uint8>(bytes[0]);

	if (!fEditable) {
		// only arrow and page keys are allowed
		// (no need to hide the cursor)
		switch (keyPressed) {
			case keys::kLeftArrow:
			case keys::kRightArrow:
			case keys::kUpArrow:
			case keys::kDownArrow:
				_HandleArrowKey(keyPressed);
				break;

			case keys::kHome:
			case keys::kEnd:
			case keys::kPageUp:
			case keys::kPageDown:
				_HandlePageKey(keyPressed);
				break;

			default:
				BView::KeyDown(bytes);
				break;
		}

		return;
	}

	if (fInline != nullptr && fInline->active) {
		// NewGUI: the input method's own keys; typed text ends the
		// composition
		if (keyPressed < 0x20 || keyPressed == 0x7f)
			return;
		_RemoveInlineInput();
		fInline.reset();
	}

	// hide the caret
	_HideCaret();

	switch (keyPressed) {
		case keys::kBackspace:
			_HandleBackspace();
			break;

		case keys::kLeftArrow:
		case keys::kRightArrow:
		case keys::kUpArrow:
		case keys::kDownArrow:
			_HandleArrowKey(keyPressed);
			break;

		case keys::kDelete:
			_HandleDelete();
			break;

		case keys::kHome:
		case keys::kEnd:
		case keys::kPageUp:
		case keys::kPageDown:
			_HandlePageKey(keyPressed);
			break;

		case keys::kEscape:
		case keys::kInsert:
		case keys::kFunctionKey:
			// ignore, pass it up to superclass
			BView::KeyDown(bytes);
			break;

		default:
			// bail out if the character is not allowed
			if (std::find(fDisallowedChars.begin(), fDisallowedChars.end(),
					static_cast<uint32>(keyPressed)) != fDisallowedChars.end()) {
				// NewGUI: no beep()
				return;
			}

			_HandleAlphaKey(bytes);
			break;
	}

	// draw the caret
	if (fSelStart == fSelEnd)
		_ShowCaret();
}


void
BTextView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	// NewGUI: a text that does not wrap is aligned in _Relayout() as well
	_ResetTextRect();
}


void
BTextView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);

	if (focus && Window() != nullptr && Window()->IsActive()) {
		if (!fActive)
			_Activate();
	} else {
		if (fActive)
			_Deactivate();
	}
}


void
BTextView::MessageReceived(BMessage* message)
{
	// was this message dropped?
	if (message->WasDropped()) {
		if (!_MessageDropped(message, ConvertFromScreen(message->DropPoint())))
			BView::MessageReceived(message);
		return;
	}

	switch (message->what) {
		case kCut:
			// NewGUI: no beep()
			if (!IsTypingHidden())
				Cut(&BClipboard::System());
			break;

		case kCopy:
			if (!IsTypingHidden())
				Copy(&BClipboard::System());
			break;

		case kPaste:
			Paste(&BClipboard::System());
			break;

		case kUndo:
			Undo(&BClipboard::System());
			break;

		case kSelectAll:
			SelectAll();
			break;

		case kInputMethodEvent:
		{
			if (!fEditable)
				break;
			switch (message->GetInt32("be:opcode", -1)) {
				case kInputMethodStarted:
					if (fInline == nullptr)
						fInline.reset(new InlineInput{0, 0, 0, 0, false, {}});
					break;

				case kInputMethodStopped:
					// NewGUI: the composition's text goes, its final text
					// comes as key events
					_RemoveInlineInput();
					fInline.reset();
					break;

				case kInputMethodChanged:
					if (fInline == nullptr)
						fInline.reset(new InlineInput{0, 0, 0, 0, false, {}});
					_HandleInputMethodChanged(message);
					break;

				case kInputMethodLocationRequest:
					_UpdateTextInputArea();
					break;

				default:
					break;
			}
			break;
		}

		case kMsgNavigateArrow:
			_HandleArrowKey(static_cast<uint8>(message->GetInt32("key", 0)),
				message->GetInt32("modifiers", 0));
			break;

		case kMsgNavigatePage:
			_HandlePageKey(static_cast<uint8>(message->GetInt32("key", 0)),
				message->GetInt32("modifiers", 0));
			break;

		case kMsgRemoveWord:
		{
			const int32 key = message->GetInt32("key", 0);
			const int32 modifiers = message->GetInt32("modifiers", 0);
			if (key == keys::kDelete)
				_HandleDelete(modifiers);
			else if (key == keys::kBackspace)
				_HandleBackspace(modifiers);
			break;
		}

		case kMsgCaretBlink:
			// Haiku's Pulse()
			if (fActive && (fEditable || fSelectable) && fSelStart == fSelEnd) {
				fCaretVisible = !fCaretVisible;
				_InvalidateCaret();
			}
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


// #pragma mark - text


void
BTextView::SetText(std::string_view text, const BTextRunArray* runs)
{
	_CancelInputMethod();

	if (fActive) {
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_HideCaret();
	}

	if (!fText.empty())
		DeleteText(0, TextLength());

	if (!text.empty())
		InsertText(text, 0, runs);

	fCaretOffset = fSelStart = fSelEnd = 0;
	_Refresh();

	// draw the caret
	_ShowCaret();
}


void
BTextView::Insert(std::string_view text, const BTextRunArray* runs)
{
	if (!text.empty())
		_DoInsertText(text, fSelStart, runs);
}


void
BTextView::Insert(int32 offset, std::string_view text, const BTextRunArray* runs)
{
	offset = std::clamp(offset, 0, TextLength());
	if (!text.empty())
		_DoInsertText(text, offset, runs);
}


void
BTextView::Delete()
{
	Delete(fSelStart, fSelEnd);
}


void
BTextView::Delete(int32 startOffset, int32 endOffset)
{
	// anchor the offsets
	startOffset = std::clamp(startOffset, 0, TextLength());
	endOffset = std::clamp(endOffset, 0, TextLength());

	// bail out if there is nothing to do
	if (startOffset == endOffset)
		return;

	// hide the caret/selection
	if (fActive) {
		if (fSelStart != fSelEnd) {
			if (fSelectable)
				Highlight(fSelStart, fSelEnd);
		} else
			_HideCaret();
	}

	// remove data from buffer
	DeleteText(startOffset, endOffset);

	// check if the caret needs to be moved
	if (fCaretOffset >= endOffset)
		fCaretOffset -= (endOffset - startOffset);
	else if (fCaretOffset >= startOffset && fCaretOffset < endOffset)
		fCaretOffset = startOffset;

	fSelEnd = fSelStart = fCaretOffset;

	// recalc line breaks and draw what's left
	_Refresh(fCaretOffset);

	// draw the caret
	_ShowCaret();
}


std::string_view
BTextView::Text() const
{
	return fText;
}


int32
BTextView::TextLength() const
{
	return static_cast<int32>(fText.size());
}


std::string
BTextView::GetText(int32 offset, int32 length) const
{
	if (offset < 0 || offset >= TextLength() || length < 1)
		return {};
	return fText.substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
}


uint8
BTextView::ByteAt(int32 offset) const
{
	if (offset < 0 || offset >= TextLength())
		return '\0';

	return static_cast<uint8>(fText[static_cast<std::size_t>(offset)]);
}


int32
BTextView::CountLines() const
{
	return fLayout.CountLines();
}


int32
BTextView::CurrentLine() const
{
	return LineAt(fSelStart);
}


void
BTextView::GoToLine(int32 index)
{
	_HideCaret();
	fSelStart = fSelEnd = fCaretOffset = OffsetAt(index);
	_ShowCaret();
}


void
BTextView::Cut(BClipboard* clipboard)
{
	if (!fEditable)
		return;

	if (fUndo)
		fUndo = std::make_unique<CutUndoBuffer>(this);

	Copy(clipboard);
	Delete();
}


void
BTextView::Copy(BClipboard* clipboard)
{
	if (clipboard->Lock()) {
		(void)clipboard->Clear();

		if (BMessage* clip = clipboard->Data()) {
			const std::string text = GetText(fSelStart, fSelEnd - fSelStart);
			(void)clip->AddData("text/plain", BTypeCode::Mime, Bytes(text));

			// NewGUI: the runs flattened (Haiku: the text_run_array as it
			// is in memory)
			if (fStylable) {
				(void)clip->AddData(kTextRunArrayType, BTypeCode::Mime,
					FlattenRunArray(RunArray(fSelStart, fSelEnd)));
			}
			(void)clipboard->Commit();
		}
		clipboard->Unlock();
	}
}


bool
BTextView::AcceptsDrop(const BMessage* message)
{
	return fEditable && message != nullptr
		&& message->HasData("text/plain", BTypeCode::Mime);
}


bool
BTextView::_MessageDropped(BMessage* message, gfx::Point where)
{
	// are we sure we like this message?
	if (!AcceptsDrop(message))
		return false;

	int32 dropOffset = OffsetAt(where);
	if (dropOffset > TextLength())
		dropOffset = TextLength();

	const auto data = message->FindData("text/plain", BTypeCode::Mime);
	if (!data.has_value())
		return true;
	const std::string text = _FilterDisallowedChars(std::string_view(
		reinterpret_cast<const char*>(data->data()), data->size()));
	// NewGUI: no beep() for nothing left
	if (text.empty())
		return true;

	if (fUndo)
		fUndo = std::make_unique<DropUndoBuffer>(this, text, dropOffset);

	Insert(dropOffset, text);
	if (IsFocus())
		Select(dropOffset, dropOffset + static_cast<int32>(text.size()));
	return true;
}


void
BTextView::Paste(BClipboard* clipboard)
{
	if (!fEditable || !clipboard->Lock())
		return;

	if (BMessage* clip = clipboard->Data()) {
		if (auto data = clip->FindData("text/plain", BTypeCode::Mime)) {
			const std::string_view pasted(reinterpret_cast<const char*>(data->data()),
				data->size());
			std::optional<BTextRunArray> runArray;
			if (fStylable) {
				if (auto runs = clip->FindData(kTextRunArrayType, BTypeCode::Mime)) {
					if (auto unflattened = UnflattenRunArray(*runs))
						runArray = std::move(*unflattened);
				}
			}

			const std::string text = _FilterDisallowedChars(pasted,
				runArray.has_value() ? &*runArray : nullptr);
			if (text.empty()) {
				// NewGUI: no beep()
				clipboard->Unlock();
				return;
			}

			if (fUndo)
				fUndo = std::make_unique<PasteUndoBuffer>(this, text, runArray);

			if (fSelStart != fSelEnd)
				Delete();

			Insert(text, runArray.has_value() ? &*runArray : nullptr);
			ScrollToOffset(fSelEnd);
		}
	}

	clipboard->Unlock();
}


void
BTextView::Clear()
{
	if (fUndo)
		fUndo = std::make_unique<ClearUndoBuffer>(this);

	Delete();
}


bool
BTextView::AcceptsPaste(BClipboard* clipboard)
{
	bool result = false;

	if (fEditable && clipboard != nullptr && clipboard->Lock()) {
		BMessage* data = clipboard->Data();
		result = data != nullptr && data->HasData("text/plain", BTypeCode::Mime);
		clipboard->Unlock();
	}

	return result;
}


void
BTextView::Undo(BClipboard* clipboard)
{
	if (fUndo)
		fUndo->Undo(clipboard);
}


BUndoState
BTextView::UndoState(bool* isRedo) const
{
	return fUndo == nullptr ? BUndoState::Unavailable : fUndo->State(isRedo);
}


void
BTextView::SetDoesUndo(bool undo)
{
	if (undo && fUndo == nullptr)
		fUndo = std::make_unique<UndoBuffer>(this, BUndoState::Unavailable);
	else if (!undo && fUndo != nullptr)
		fUndo.reset();
}


bool
BTextView::DoesUndo() const
{
	return fUndo != nullptr;
}


void
BTextView::HideTyping(bool enabled)
{
	if (enabled == fHideTyping)
		return;

	if (enabled) {
		Delete(0, TextLength());
		// NewGUI: nor can undo bring it back
		if (fUndo != nullptr)
			fUndo = std::make_unique<UndoBuffer>(this, BUndoState::Unavailable);
	}
	fHideTyping = enabled;
	_Refresh();
}


bool
BTextView::IsTypingHidden() const
{
	return fHideTyping;
}


void
BTextView::Select(int32 startOffset, int32 endOffset)
{
	if (!fSelectable)
		return;

	// pin offsets at reasonable values
	startOffset = std::clamp(startOffset, 0, TextLength());
	endOffset = std::clamp(endOffset, 0, TextLength());

	// a negative selection?
	if (startOffset > endOffset)
		return;

	// is the new selection any different from the current selection?
	if (startOffset == fSelStart && endOffset == fSelEnd)
		return;

	fStyles->InvalidateNullStyle();

	_HideCaret();

	if (startOffset == endOffset) {
		if (fSelStart != fSelEnd) {
			// unhilite the selection
			if (fActive)
				Highlight(fSelStart, fSelEnd);
		}
		fSelStart = fSelEnd = fCaretOffset = startOffset;
		_ShowCaret();
	} else {
		if (fActive) {
			// NewGUI: the old and the new selection are redrawn (Haiku
			// inverted what changed)
			if (fSelStart != fSelEnd)
				Highlight(fSelStart, fSelEnd);
			Highlight(startOffset, endOffset);
		}
		fSelStart = startOffset;
		fSelEnd = endOffset;
	}
}


void
BTextView::SelectAll()
{
	fCaretOffset = TextLength();
	Select(0, fCaretOffset);
}


void
BTextView::GetSelection(int32* start, int32* end) const
{
	int32 selectionStart = 0;
	int32 selectionEnd = 0;

	if (fSelectable) {
		selectionStart = fSelStart;
		selectionEnd = fSelEnd;
	}

	if (start != nullptr)
		*start = selectionStart;

	if (end != nullptr)
		*end = selectionEnd;
}


void
BTextView::AdoptSystemColors()
{
	const gfx::Color background = Palette::Default().Get(BColorWhich::DocumentBackground);
	if (IsEditable())
		SetViewColor(background);
	else
		SetViewColor(TintColor(background, _UneditableTint()));
}


void
BTextView::SetTextColor(std::optional<gfx::Color> color)
{
	if (fTextColor == color)
		return;

	fTextColor = color;
	Invalidate();
}


std::optional<gfx::Color>
BTextView::TextColor() const
{
	return fTextColor;
}


void
BTextView::SetFontAndColor(const BFont* font, BFontModeFlags mode, const gfx::Color* color)
{
	SetFontAndColor(fSelStart, fSelEnd, font, mode, color);
}


void
BTextView::SetFontAndColor(int32 startOffset, int32 endOffset, const BFont* font,
	BFontModeFlags mode, const gfx::Color* color)
{
	_HideCaret();

	const int32 textLength = TextLength();

	if (!fStylable) {
		// When the text view is not stylable, we always set the whole text's
		// style and ignore the offsets
		startOffset = 0;
		endOffset = textLength;
	} else {
		// pin offsets at reasonable values
		startOffset = std::clamp(startOffset, 0, textLength);
		endOffset = std::clamp(endOffset, 0, textLength);
	}

	// apply the style to the style buffer
	fStyles->InvalidateNullStyle();
	const std::optional<gfx::Color> optionalColor = color != nullptr
		? std::optional<gfx::Color>(*color) : std::nullopt;
	_ApplyStyleRange(startOffset, endOffset, mode, font,
		color != nullptr ? &optionalColor : nullptr);

	// NewGUI: laid out and drawn anew in any case (Haiku: only redrawn if
	// neither family, style nor size changed)
	InvalidateLayout();
	_Refresh();

	_ShowCaret();
}


void
BTextView::GetFontAndColor(int32 offset, BFont* font, gfx::Color* color) const
{
	const TextStyle style = fStyles->GetStyle(offset);
	if (font != nullptr)
		*font = style.font;
	if (color != nullptr)
		*color = _ResolvedColor(style.color);
}


void
BTextView::GetFontAndColor(BFont* font, BFontModeFlags* mode, gfx::Color* color,
	bool* sameColor) const
{
	std::optional<gfx::Color> styleColor;
	fStyles->ContinuousGetStyle(font, mode, &styleColor, sameColor, fSelStart, fSelEnd);
	if (color != nullptr)
		*color = _ResolvedColor(styleColor);
}


void
BTextView::SetRunArray(int32 startOffset, int32 endOffset, const BTextRunArray& runs)
{
	_CancelInputMethod();

	BTextRunArray oneRun;
	const BTextRunArray* used = &runs;

	if (!fStylable) {
		// when the text view is not stylable, we always set the whole text's
		// style with the first run and ignore the offsets
		if (runs.empty())
			return;

		startOffset = 0;
		endOffset = TextLength();
		oneRun.push_back(runs.front());
		oneRun.front().offset = 0;
		used = &oneRun;
	} else {
		// pin offsets at reasonable values
		startOffset = std::clamp(startOffset, 0, TextLength());
		endOffset = std::clamp(endOffset, 0, TextLength());
	}

	_SetRunArray(startOffset, endOffset, *used);

	_Refresh();
}


BTextRunArray
BTextView::RunArray(int32 startOffset, int32 endOffset) const
{
	// pin offsets at reasonable values
	startOffset = std::clamp(startOffset, 0, TextLength());
	endOffset = std::clamp(endOffset, 0, TextLength());

	return _ResolvedRuns(_StyleRuns(startOffset, endOffset));
}


void
BTextView::SetStylable(bool stylable)
{
	fStylable = stylable;
}


bool
BTextView::IsStylable() const
{
	return fStylable;
}


namespace {


// Haiku's flattened_text_run and flattened_text_run_array, big endian.
constexpr std::uint32_t kFlattenedTextRunArrayMagic = 0x416c6921;	// 'Ali!'
constexpr std::uint32_t kFlattenedTextRunArrayVersion = 0;
constexpr std::size_t kFlattenedHeaderSize = 12;
constexpr std::size_t kFontNameLength = 64;		// B_FONT_FAMILY_LENGTH + 1
constexpr std::size_t kFlattenedRunSize = 4 + 2 * kFontNameLength + 4 + 4 + 2 + 4 + 2;


void
PutUint32(std::vector<std::byte>& out, std::uint32_t value)
{
	for (int shift = 24; shift >= 0; shift -= 8)
		out.push_back(static_cast<std::byte>((value >> shift) & 0xff));
}


void
PutUint16(std::vector<std::byte>& out, std::uint16_t value)
{
	out.push_back(static_cast<std::byte>(value >> 8));
	out.push_back(static_cast<std::byte>(value & 0xff));
}


void
PutFloat(std::vector<std::byte>& out, float value)
{
	PutUint32(out, std::bit_cast<std::uint32_t>(value));
}


void
PutName(std::vector<std::byte>& out, const std::string& name)
{
	// a C string in a fixed field, cut to fit with its terminating 0
	for (std::size_t i = 0; i < kFontNameLength; i++) {
		out.push_back(i + 1 < kFontNameLength && i < name.size()
			? static_cast<std::byte>(name[i]) : std::byte{0});
	}
}


std::uint32_t
GetUint32(std::span<const std::byte> data, std::size_t at)
{
	std::uint32_t value = 0;
	for (std::size_t i = 0; i < 4; i++)
		value = (value << 8) | static_cast<std::uint32_t>(data[at + i]);
	return value;
}


std::uint16_t
GetUint16(std::span<const std::byte> data, std::size_t at)
{
	return static_cast<std::uint16_t>((static_cast<unsigned>(data[at]) << 8)
		| static_cast<unsigned>(data[at + 1]));
}


std::string
GetName(std::span<const std::byte> data, std::size_t at)
{
	std::string name;
	for (std::size_t i = 0; i < kFontNameLength && data[at + i] != std::byte{0}; i++)
		name.push_back(static_cast<char>(data[at + i]));
	return name;
}


} // namespace


std::vector<std::byte>
BTextView::FlattenRunArray(const BTextRunArray& runs)
{
	std::vector<std::byte> out;
	out.reserve(kFlattenedHeaderSize + runs.size() * kFlattenedRunSize);
	PutUint32(out, kFlattenedTextRunArrayMagic);
	PutUint32(out, kFlattenedTextRunArrayVersion);
	PutUint32(out, static_cast<std::uint32_t>(runs.size()));

	for (const BTextRun& run : runs) {
		PutUint32(out, static_cast<std::uint32_t>(run.offset));
		std::string family;
		std::string style;
		run.font.GetFamilyAndStyle(&family, &style);
		PutName(out, family);
		PutName(out, style);
		PutFloat(out, run.font.Size());
		// NewGUI: no shear; Haiku's upright 90 degrees
		PutFloat(out, 90.0f);
		PutUint16(out, static_cast<std::uint16_t>(run.font.Face().Raw()));
		const compat::rgb_color color = compat::ToRgbColor(run.color);
		out.push_back(static_cast<std::byte>(color.red));
		out.push_back(static_cast<std::byte>(color.green));
		out.push_back(static_cast<std::byte>(color.blue));
		out.push_back(std::byte{255});
		PutUint16(out, 0);
	}

	return out;
}


std::expected<BTextRunArray, Error>
BTextView::UnflattenRunArray(std::span<const std::byte> data)
{
	if (data.size() < kFlattenedHeaderSize
		|| GetUint32(data, 0) != kFlattenedTextRunArrayMagic
		|| GetUint32(data, 4) != kFlattenedTextRunArrayVersion) {
		return Failure(ErrorCode::BadData, "not a flattened text_run_array");
	}

	const std::uint32_t count = GetUint32(data, 8);
	if (count > (data.size() - kFlattenedHeaderSize) / kFlattenedRunSize)
		return Failure(ErrorCode::BadData, "the flattened text_run_array is cut off");

	BTextRunArray runArray;
	for (std::size_t i = 0; i < count; i++) {
		const std::size_t at = kFlattenedHeaderSize + i * kFlattenedRunSize;
		BTextRun run;
		run.offset = static_cast<int32>(GetUint32(data, at));

		// Set family and style independently from each other, so that
		// even if the family doesn't exist, we try to preserve the style
		(void)run.font.SetFamilyAndStyle(GetName(data, at + 4), "");
		(void)run.font.SetFamilyAndStyle("", GetName(data, at + 4 + kFontNameLength));

		std::size_t field = at + 4 + 2 * kFontNameLength;
		run.font.SetSize(std::bit_cast<float>(GetUint32(data, field)));
		field += 8;	// size and shear (NewGUI: no shear)

		const auto face = BFontFaceFlags::FromRaw(GetUint16(data, field));
		if (face != BFontFaceFlags(BFontFace::Regular)) {
			// Be's version doesn't seem to set this correctly
			run.font.SetFace(face);
		}
		field += 2;

		run.color = gfx::Color::FromRgba8(static_cast<std::uint8_t>(data[field]),
			static_cast<std::uint8_t>(data[field + 1]), static_cast<std::uint8_t>(data[field + 2]),
			static_cast<std::uint8_t>(data[field + 3]));
		runArray.push_back(run);
	}

	return runArray;
}


// #pragma mark - geometry


int32
BTextView::LineAt(int32 offset) const
{
	return fLayout.LineAtOffset(static_cast<std::size_t>(std::clamp(offset, 0, TextLength())));
}


int32
BTextView::LineAt(gfx::Point point) const
{
	return fLayout.LineAtY(point.y - fTextRect.y0);
}


gfx::Point
BTextView::PointAt(int32 offset, float* height) const
{
	const gfx::Point point = fLayout.PointAt(
		static_cast<std::size_t>(std::clamp(offset, 0, TextLength())), height);
	return {std::round(point.x + fTextRect.x0), std::round(point.y + fTextRect.y0)};
}


int32
BTextView::OffsetAt(gfx::Point point) const
{
	return static_cast<int32>(fLayout.OffsetAt({point.x - fTextRect.x0,
		point.y - fTextRect.y0}));
}


int32
BTextView::OffsetAt(int32 line) const
{
	if (line < 0)
		return 0;

	if (line >= CountLines())
		return TextLength();

	return static_cast<int32>(fLayout.LineAt(line).start);
}


void
BTextView::FindWord(int32 offset, int32* fromOffset, int32* toOffset)
{
	if (offset < 0) {
		if (fromOffset != nullptr)
			*fromOffset = 0;
		if (toOffset != nullptr)
			*toOffset = 0;
		return;
	}

	if (offset > TextLength()) {
		if (fromOffset != nullptr)
			*fromOffset = TextLength();
		if (toOffset != nullptr)
			*toOffset = TextLength();
		return;
	}

	if (fromOffset != nullptr)
		*fromOffset = _PreviousWordBoundary(offset);

	if (toOffset != nullptr)
		*toOffset = _NextWordBoundary(offset);
}


float
BTextView::LineWidth(int32 lineNumber) const
{
	if (lineNumber < 0 || lineNumber >= CountLines())
		return 0;

	return fLayout.LineAt(lineNumber).width;
}


float
BTextView::LineHeight(int32 lineNumber) const
{
	float lineHeight = TextHeight(lineNumber, lineNumber);
	if (lineHeight == 0.0f) {
		// We probably don't have text content yet. Take the null style's
		// height (Haiku's _NullStyleHeight()).
		FontHeight fontHeight{};
		fStyles->NullStyle().font.GetHeight(&fontHeight);
		lineHeight = std::ceil(fontHeight.ascent + fontHeight.descent + 1);
	}
	return lineHeight;
}


float
BTextView::TextHeight(int32 startLine, int32 endLine) const
{
	const int32 numLines = CountLines();
	startLine = std::clamp(startLine, 0, numLines - 1);
	endLine = std::clamp(endLine, 0, numLines - 1);

	// NewGUI: the empty line after a final line break is a line of its own
	const float bottom = endLine + 1 < numLines ? fLayout.LineAt(endLine + 1).top
		: fLayout.Height();
	return std::ceil(bottom - fLayout.LineAt(startLine).top);
}


void
BTextView::GetTextRegion(int32 startOffset, int32 endOffset, gfx::Region* outRegion) const
{
	if (outRegion == nullptr)
		return;

	outRegion->MakeEmpty();

	startOffset = std::clamp(startOffset, 0, TextLength());
	endOffset = std::clamp(endOffset, 0, TextLength());

	// return an empty region if the range is invalid
	if (startOffset >= endOffset)
		return;

	float startLineHeight = 0.0;
	float endLineHeight = 0.0;
	const gfx::Point startPt = PointAt(startOffset, &startLineHeight);
	const gfx::Point endPt = PointAt(endOffset, &endLineHeight);

	startLineHeight = std::ceil(startLineHeight);
	endLineHeight = std::ceil(endLineHeight);

	if (fLayout.HasRightToLeft()) {
		// NewGUI: right-to-left runs; the range in pieces as it is on the
		// screen (TextLayout::SelectionRects())
		for (const gfx::Rect& rect : fLayout.SelectionRects(
				static_cast<std::size_t>(startOffset), static_cast<std::size_t>(endOffset))) {
			const gfx::Rect placed{rect.x0 + fTextRect.x0, rect.y0 + fTextRect.y0,
				rect.x1 + fTextRect.x0, rect.y1 + fTextRect.y0};
			if (placed.x1 > placed.x0 && placed.y1 > placed.y0)
				outRegion->Include(gfx::IntRect::RoundedFrom(placed));
		}
		return;
	}

	const gfx::Rect bounds = Bounds();
	const float left = std::min(fTextRect.x0, bounds.x0 + fLayoutData.leftInset);
	const float right = std::max(fTextRect.x1, bounds.x1 - fLayoutData.rightInset);
	const auto include = [outRegion](float x0, float y0, float x1, float y1) {
		if (x1 > x0 && y1 > y0)
			outRegion->Include(gfx::IntRect::RoundedFrom({x0, y0, x1, y1}));
	};

	if (startPt.y == endPt.y) {
		// this is a one-line region
		include(startPt.x, startPt.y, endPt.x, endPt.y + endLineHeight);
	} else {
		// more than one line in the specified offset range
		include(startPt.x, startPt.y, right, startPt.y + startLineHeight);

		// if the startPt line and the endPt line are not adjacent
		// fill the gap
		if (startPt.y + startLineHeight < endPt.y)
			include(left, startPt.y + startLineHeight, right, endPt.y);

		include(left, endPt.y, endPt.x, endPt.y + endLineHeight);
	}
}


void
BTextView::ScrollToOffset(int32 offset)
{
	const gfx::Rect bounds = Bounds();
	float lineHeight = 0.0;
	const gfx::Point point = PointAt(offset, &lineHeight);
	gfx::Point scrollBy{0, 0};

	// horizontal (Haiku's inclusive right edge, one less)
	const float boundsRight = bounds.x1 - 1;
	if (point.x < bounds.x0)
		scrollBy.x = point.x - boundsRight;
	else if (point.x > boundsRight)
		scrollBy.x = point.x - bounds.x0;

	// prevent from scrolling out of view
	if (scrollBy.x != 0.0f) {
		const float rightMax = fTextRect.x1 - 1 + fLayoutData.rightInset;
		if (boundsRight + scrollBy.x > rightMax)
			scrollBy.x = rightMax - boundsRight;
		const float leftMin = fTextRect.x0 - fLayoutData.leftInset;
		if (bounds.x0 + scrollBy.x < leftMin)
			scrollBy.x = leftMin - bounds.x0;
	}

	// vertical
	const float boundsBottom = bounds.y1 - 1;
	if (CountLines() > 1) {
		// scroll in Y only if multiple lines!
		if (point.y < bounds.y0 - fLayoutData.topInset)
			scrollBy.y = point.y - bounds.y0 - fLayoutData.topInset;
		else if (point.y + lineHeight > boundsBottom + fLayoutData.bottomInset) {
			scrollBy.y = point.y + lineHeight - boundsBottom + fLayoutData.bottomInset;
		}
	}

	if (scrollBy.x != 0 || scrollBy.y != 0)
		ScrollBy(scrollBy.x, scrollBy.y);
}


void
BTextView::ScrollToSelection()
{
	ScrollToOffset(fSelStart);
}


void
BTextView::Highlight(int32 startOffset, int32 endOffset)
{
	// get real
	startOffset = std::clamp(startOffset, 0, TextLength());
	endOffset = std::clamp(endOffset, 0, TextLength());

	if (startOffset >= endOffset)
		return;

	// NewGUI: views are redrawn whole
	Invalidate();
}


void
BTextView::SetTextRect(gfx::Rect rect)
{
	if (rect == fTextRect)
		return;

	if (!fWrap) {
		rect.x1 = Bounds().x1;
		rect.y1 = Bounds().y1;
	}

	_UpdateInsets(rect);

	fTextRect = rect;

	_ResetTextRect();
}


gfx::Rect
BTextView::TextRect() const
{
	return fTextRect;
}


void
BTextView::SetInsets(float left, float top, float right, float bottom)
{
	if (fLayoutData.leftInset == left && fLayoutData.topInset == top
		&& fLayoutData.rightInset == right && fLayoutData.bottomInset == bottom) {
		return;
	}

	fLayoutData.leftInset = left;
	fLayoutData.topInset = top;
	fLayoutData.rightInset = right;
	fLayoutData.bottomInset = bottom;

	fLayoutData.overridden = true;

	InvalidateLayout();
	// NewGUI: the text moves at once (Haiku waited for the layout)
	_ResetTextRect();
}


void
BTextView::GetInsets(float* left, float* top, float* right, float* bottom) const
{
	if (left != nullptr)
		*left = fLayoutData.leftInset;
	if (top != nullptr)
		*top = fLayoutData.topInset;
	if (right != nullptr)
		*right = fLayoutData.rightInset;
	if (bottom != nullptr)
		*bottom = fLayoutData.bottomInset;
}


// #pragma mark - settings


void
BTextView::SetTabWidth(float width)
{
	if (width == fTabWidth)
		return;

	fTabWidth = width;

	_Refresh();
}


float
BTextView::TabWidth() const
{
	return fTabWidth;
}


void
BTextView::MakeSelectable(bool selectable)
{
	if (selectable == fSelectable)
		return;

	fSelectable = selectable;

	if (fActive && Window() != nullptr) {
		if (fSelStart != fSelEnd)
			Highlight(fSelStart, fSelEnd);
		else if (fSelectable)
			_ShowCaret();
		else
			_HideCaret();
	}
}


bool
BTextView::IsSelectable() const
{
	return fSelectable;
}


void
BTextView::MakeEditable(bool editable)
{
	if (editable == fEditable)
		return;

	// NewGUI: Haiku's HasSystemColors(), the color of the other state
	const gfx::Color background = Palette::Default().Get(BColorWhich::DocumentBackground);
	const bool systemColors = ViewColor() == (fEditable ? background
		: TintColor(background, _UneditableTint()));

	fEditable = editable;

	if (systemColors)
		AdoptSystemColors();

	// TextControls change the color of the text when
	// they are made editable, so we need to invalidate
	// the NULL style here
	if (fEditable)
		fStyles->InvalidateNullStyle();

	if (Window() != nullptr && fActive) {
		if (!fEditable && !fSelectable)
			_HideCaret();
	}
	Invalidate();
}


bool
BTextView::IsEditable() const
{
	return fEditable;
}


void
BTextView::SetWordWrap(bool wrap)
{
	if (wrap == fWrap)
		return;

	fWrap = wrap;
	_ResetTextRect();

	// NewGUI: without wrapping the text view is as wide as its text, so its
	// size for the layout changes (Haiku kept the old one until the layout
	// was invalidated otherwise)
	InvalidateLayout();

	if (fEditable || fSelectable)
		ScrollToOffset(fCaretOffset);
}


bool
BTextView::DoesWordWrap() const
{
	return fWrap;
}


void
BTextView::SetMaxBytes(int32 max)
{
	const int32 textLength = TextLength();
	fMaxBytes = max;

	if (fMaxBytes < textLength) {
		int32 offset = fMaxBytes;
		// Delete the text after fMaxBytes, but
		// respect multibyte characters boundaries.
		const int32 previousInitial = _PreviousInitialByte(offset);
		if (_NextInitialByte(previousInitial) != offset)
			offset = previousInitial;

		Delete(offset, textLength);
	}
}


int32
BTextView::MaxBytes() const
{
	return fMaxBytes;
}


void
BTextView::DisallowChar(uint32 character)
{
	if (std::find(fDisallowedChars.begin(), fDisallowedChars.end(), character)
			== fDisallowedChars.end()) {
		fDisallowedChars.push_back(character);
	}
}


void
BTextView::AllowChar(uint32 character)
{
	std::erase(fDisallowedChars, character);
}


void
BTextView::SetAlignment(BHorizontalAlignment alignment)
{
	// Do a reality check
	if (fAlignment != alignment
		&& (alignment == BHorizontalAlignment::Left
			|| alignment == BHorizontalAlignment::Right
			|| alignment == BHorizontalAlignment::Center)) {
		fAlignment = alignment;

		// After setting new alignment, update the view/window
		_ResetTextRect();
	}
}


BHorizontalAlignment
BTextView::Alignment() const
{
	return fAlignment;
}


void
BTextView::SetAutoindent(bool state)
{
	fAutoindent = state;
}


bool
BTextView::DoesAutoindent() const
{
	return fAutoindent;
}


// #pragma mark - layout


void
BTextView::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


void
BTextView::GetPreferredSize(float* width, float* height)
{
	_ValidateLayoutData();

	if (width != nullptr) {
		float preferredWidth = Bounds().Width();
		if (preferredWidth < fLayoutData.min.w || Flags().Has(BViewFlag::SupportsLayout))
			preferredWidth = fLayoutData.min.w;
		*width = preferredWidth;
	}

	if (height != nullptr) {
		float preferredHeight = Bounds().Height();
		if (preferredHeight < fLayoutData.min.h || Flags().Has(BViewFlag::SupportsLayout))
			preferredHeight = fLayoutData.min.h;
		*height = preferredHeight;
	}
}


gfx::Size
BTextView::MinSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData.min);
}


gfx::Size
BTextView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), {kSizeUnlimited, kSizeUnlimited});
}


gfx::Size
BTextView::PreferredSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), fLayoutData.preferred);
}


bool
BTextView::HasHeightForWidth()
{
	if (IsEditable())
		return BView::HasHeightForWidth();

	// When not editable, we assume that all text is supposed to be visible.
	return true;
}


void
BTextView::GetHeightForWidth(float width, float* min, float* max, float* preferred)
{
	if (IsEditable()) {
		BView::GetHeightForWidth(width, min, max, preferred);
		return;
	}

	// NewGUI: a layout of its own at that width (Haiku recalculated its
	// lines and restored the text rectangle)
	float textWidth = width;
	if (fLayoutData.overridden)
		textWidth -= fLayoutData.leftInset + fLayoutData.rightInset;
	const TextLayout layout(TextSystem::Default(), fText, _LayoutRuns(),
		{.width = textWidth, .wrap = fWrap, .tabWidth = fTabWidth, .alignment = fAlignment});
	float height = layout.Height();
	if (fLayoutData.overridden)
		height += fLayoutData.topInset + fLayoutData.bottomInset;

	if (min != nullptr)
		*min = height;
	if (max != nullptr)
		*max = kSizeUnlimited;
	if (preferred != nullptr)
		*preferred = height;
}


void
BTextView::LayoutInvalidated(bool /*descendants*/)
{
	fLayoutData.valid = false;
}


void
BTextView::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return;

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != nullptr) {
		BView::DoLayout();
		return;
	}

	_ValidateLayoutData();

	// reset the text rect
	_ResetTextRect();
}


// #pragma mark - hooks


void
BTextView::InsertText(std::string_view text, int32 offset, const BTextRunArray* runs)
{
	offset = std::clamp(offset, 0, TextLength());

	const auto length = static_cast<int32>(text.size());
	if (length > 0) {
		// add the text to the buffer
		fText.insert(static_cast<std::size_t>(offset), text);

		// update the style runs
		fStyles->BumpOffset(length, fStyles->OffsetToRun(offset - 1) + 1);

		// offset the caret/selection, if the text was inserted before it
		if (offset <= fSelEnd) {
			fSelStart += length;
			fCaretOffset = fSelEnd = fSelStart;
		}
	}

	if (fStylable && runs != nullptr)
		_SetRunArray(offset, offset + length, *runs);
	else {
		// apply null-style to inserted text
		_ApplyStyleRange(offset, offset + length);
	}
}


void
BTextView::DeleteText(int32 fromOffset, int32 toOffset)
{
	fromOffset = std::clamp(fromOffset, 0, TextLength());
	toOffset = std::clamp(toOffset, 0, TextLength());

	if (fromOffset >= toOffset)
		return;

	// set nullStyle to style at beginning of range
	fStyles->InvalidateNullStyle();
	fStyles->SyncNullStyle(fromOffset);

	// remove from the text buffer
	fText.erase(static_cast<std::size_t>(fromOffset),
		static_cast<std::size_t>(toOffset - fromOffset));

	// remove any style runs that have been obliterated
	fStyles->RemoveStyleRange(fromOffset, toOffset);

	// update the selection
	const int32 range = toOffset - fromOffset;
	if (fSelStart >= toOffset) {
		// selection is behind the range that was removed
		fSelStart -= range;
		fSelEnd -= range;
	} else if (fSelStart >= fromOffset && fSelEnd <= toOffset) {
		// the selection is within the range that was removed
		fSelStart = fSelEnd = fromOffset;
	} else if (fSelStart >= fromOffset && fSelEnd > toOffset) {
		// the selection starts within and ends after the range
		// the remaining part is the part that was after the range
		fSelStart = fromOffset;
		fSelEnd = fromOffset + fSelEnd - toOffset;
	} else if (fSelStart < fromOffset && fSelEnd < toOffset) {
		// the selection starts before, but ends within the range
		fSelEnd = fromOffset;
	} else if (fSelStart < fromOffset && fSelEnd >= toOffset) {
		// the selection starts before and ends after the range
		fSelEnd -= range;
	}
}


// #pragma mark - private


void
BTextView::_InitObject(gfx::Rect textRect)
{
	fTextRect = textRect;
	fStyles = std::make_unique<TextStyleBuffer>(BFont::Plain(), std::nullopt);
	_UpdateInsets(textRect);
	AdoptSystemColors();
	SetDoesUndo(true);
	_Relayout();
}


void
BTextView::_ValidateLayoutData()
{
	if (fLayoutData.valid)
		return;

	const float lineHeight = std::ceil(LineHeight(0));

	gfx::Size min{lineHeight * 3, lineHeight};
	min.w += fLayoutData.leftInset + fLayoutData.rightInset;
	min.h += fLayoutData.topInset + fLayoutData.bottomInset;

	fLayoutData.min = min;

	// compute our preferred size
	fLayoutData.preferred.h = _TextHeight();

	if (fWrap)
		fLayoutData.preferred.w = min.w + 5 * lineHeight;
	else {
		float maxWidth = fLayout.MaxWidth() + fLayoutData.leftInset + fLayoutData.rightInset;
		if (maxWidth < min.w)
			maxWidth = min.w;

		fLayoutData.preferred.w = maxWidth;
		fLayoutData.min = fLayoutData.preferred;
	}

	fLayoutData.valid = true;
	ResetLayoutInvalidation();
}


void
BTextView::_ResetTextRect()
{
	const gfx::Rect bounds = Bounds();
	fTextRect = {fLayoutData.leftInset, fLayoutData.topInset,
		bounds.Width() - fLayoutData.rightInset, bounds.Height() - fLayoutData.bottomInset};

	_Refresh();
}


void
BTextView::_Relayout()
{
	// Haiku's _RecalculateLineBreaks() for all of the text: the text rect
	// keeps its left, top and (when wrapping) right edges; its height is
	// the text's. Without wrapping it is as wide as the widest line, placed
	// by the alignment.
	const TextSystem& system = TextSystem::Default();
	const std::vector<TextStyleRun> runs = _LayoutRuns();
	const gfx::Rect bounds = Bounds();
	const float left = fLayoutData.leftInset;
	const float right = bounds.Width() - fLayoutData.rightInset;

	// update text rect (Haiku: after the lines, from the bounds and insets)
	fTextRect.x0 = left;
	fTextRect.x1 = right;
	fTextRect.y0 = fLayoutData.topInset;
	TextLayoutOptions options{.width = std::max(fTextRect.Width(), 0.0f), .wrap = fWrap,
		.tabWidth = fTabWidth, .alignment = fAlignment, .hideText = fHideTyping};

	if (!fWrap) {
		const TextLayout measured(system, fText, runs,
			{.width = 0, .wrap = false, .tabWidth = fTabWidth, .hideText = fHideTyping});
		const float minTextRectWidth = std::max(std::round(measured.MaxWidth()), 1.0f);
		switch (fAlignment) {
			default:
			case BHorizontalAlignment::Left:
				// move right edge
				fTextRect.x0 = left;
				fTextRect.x1 = left + minTextRectWidth;
				break;

			case BHorizontalAlignment::Right:
				// move left edge
				fTextRect.x1 = right;
				fTextRect.x0 = right - minTextRectWidth;
				break;

			case BHorizontalAlignment::Center:
			{
				// move both edges
				const float inset = (right - left - minTextRectWidth) / 2;
				fTextRect.x0 = left + inset;
				fTextRect.x1 = right - inset;
				break;
			}
		}
		options.width = fTextRect.Width();
	}

	fLayout = TextLayout(system, fText, runs, options);

	// always set text rect bottom (Haiku's _ValidateTextRect(): at least
	// one pixel)
	fTextRect.y1 = fTextRect.y0 + std::max(fLayout.Height(), 1.0f);
	if (fTextRect.x1 <= fTextRect.x0)
		fTextRect.x1 = fTextRect.x0 + 1;
}


void
BTextView::_Refresh(std::optional<int32> scrollTo)
{
	// recalculate line breaks and redraw everything (NewGUI: Haiku
	// recalculated and drew only the lines from fromOffset to toOffset)
	_Relayout();

	if (Window() == nullptr)
		return;

	Invalidate();
	_UpdateScrollbars();

	if (scrollTo.has_value())
		ScrollToOffset(*scrollTo);
}


int32
BTextView::_CurrentModifiers() const
{
	const BMessage* currentMessage = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	return currentMessage != nullptr ? currentMessage->GetInt32("modifiers", 0) : 0;
}


void
BTextView::_HandleBackspace(int32 modifiers)
{
	if (!fEditable)
		return;

	if (modifiers < 0)
		modifiers = _CurrentModifiers();

	const auto flags = BModifierFlags::FromRaw(static_cast<uint32>(modifiers));
	const bool controlKeyDown = flags.Has(BModifier::Control);
	const bool optionKeyDown = flags.Has(BModifier::Option);
	const bool commandKeyDown = flags.Has(BModifier::Command);

	if ((commandKeyDown || optionKeyDown) && !controlKeyDown) {
		fSelStart = _PreviousWordStart(fCaretOffset - 1);
		fSelEnd = fCaretOffset;
	}

	if (TypingUndoBuffer* undoBuffer = _TypingUndo())
		undoBuffer->BackwardErase();

	if (fSelStart == fSelEnd) {
		if (fSelStart != 0)
			fSelStart = _PreviousInitialByte(fSelStart);
	} else
		Highlight(fSelStart, fSelEnd);

	DeleteText(fSelStart, fSelEnd);
	fCaretOffset = fSelEnd = fSelStart;

	_Refresh(fCaretOffset);
}


// NewGUI: the selection between its anchor and the caret, after the caret
// moved visually from \a previousCaret (the anchor is the other end).
void
BTextView::_SelectFromAnchor(int32 previousCaret, int32* selStart, int32* selEnd) const
{
	const int32 anchor = previousCaret == fSelStart ? fSelEnd : fSelStart;
	*selStart = std::min(anchor, fCaretOffset);
	*selEnd = std::max(anchor, fCaretOffset);
}


void
BTextView::_HandleArrowKey(uint8 arrowKey, int32 modifiers)
{
	// return if there's nothing left to do
	if (TextLength() == 0)
		return;

	int32 selStart = fSelStart;
	int32 selEnd = fSelEnd;

	if (modifiers < 0)
		modifiers = _CurrentModifiers();

	const auto flags = BModifierFlags::FromRaw(static_cast<uint32>(modifiers));
	const bool shiftKeyDown = flags.Has(BModifier::Shift);
	const bool controlKeyDown = flags.Has(BModifier::Control);
	const bool optionKeyDown = flags.Has(BModifier::Option);
	const bool commandKeyDown = flags.Has(BModifier::Command);

	const int32 lastClickOffset = fCaretOffset;

	switch (arrowKey) {
		case keys::kLeftArrow:
			if (!fEditable && !fSelectable)
				_ScrollBy(-kHorizontalScrollBarStep, 0);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelStart;
			else if (fLayout.HasRightToLeft() && !commandKeyDown && !optionKeyDown) {
				// NewGUI: visually over the grapheme left of the caret
				fCaretOffset = static_cast<int32>(
					fLayout.CaretLeft(static_cast<std::size_t>(fCaretOffset)));
				if (shiftKeyDown)
					_SelectFromAnchor(lastClickOffset, &selStart, &selEnd);
			} else {
				if ((commandKeyDown || optionKeyDown) && !controlKeyDown)
					fCaretOffset = _PreviousWordStart(fCaretOffset - 1);
				else
					fCaretOffset = _PreviousInitialByte(fCaretOffset);

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the left
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the right
						selEnd = fCaretOffset;
					}
				}
			}
			break;

		case keys::kRightArrow:
			if (!fEditable && !fSelectable)
				_ScrollBy(kHorizontalScrollBarStep, 0);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelEnd;
			else if (fLayout.HasRightToLeft() && !commandKeyDown && !optionKeyDown) {
				// NewGUI: visually over the grapheme right of the caret
				fCaretOffset = static_cast<int32>(
					fLayout.CaretRight(static_cast<std::size_t>(fCaretOffset)));
				if (shiftKeyDown)
					_SelectFromAnchor(lastClickOffset, &selStart, &selEnd);
			} else {
				if ((commandKeyDown || optionKeyDown) && !controlKeyDown)
					fCaretOffset = _NextWordEnd(fCaretOffset);
				else
					fCaretOffset = _NextInitialByte(fCaretOffset);

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the right
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the left
						selStart = fCaretOffset;
					}
				}
			}
			break;

		case keys::kUpArrow:
		{
			if (!fEditable && !fSelectable)
				_ScrollBy(0, -kVerticalScrollBarStep);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelStart;
			else {
				if (optionKeyDown && !commandKeyDown && !controlKeyDown)
					fCaretOffset = _PreviousLineStart(fCaretOffset);
				else if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, 0);
					fCaretOffset = 0;
				} else {
					float height;
					gfx::Point point = PointAt(fCaretOffset, &height);
					// find the caret position on the previous
					// line by gradually moving the point up
					for (int i = 1; static_cast<float>(i) <= height; i++) {
						point.y--;
						const int32 offset = OffsetAt(point);
						if (offset < fCaretOffset || static_cast<float>(i) == height) {
							fCaretOffset = offset;
							break;
						}
					}
				}

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the top
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the bottom
						selEnd = fCaretOffset;
					}
				}
			}
			break;
		}

		case keys::kDownArrow:
		{
			if (!fEditable && !fSelectable)
				_ScrollBy(0, kVerticalScrollBarStep);
			else if (fSelStart != fSelEnd && !shiftKeyDown)
				fCaretOffset = fSelEnd;
			else {
				if (optionKeyDown && !commandKeyDown && !controlKeyDown)
					fCaretOffset = _NextLineEnd(fCaretOffset);
				else if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, fTextRect.y1 + fLayoutData.bottomInset);
					fCaretOffset = TextLength();
				} else {
					float height;
					gfx::Point point = PointAt(fCaretOffset, &height);
					point.y += height;
					fCaretOffset = OffsetAt(point);
				}

				if (shiftKeyDown && fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the bottom
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the top
						selStart = fCaretOffset;
					}
				}
			}
			break;
		}

		default:
			break;
	}

	fStyles->InvalidateNullStyle();

	if (fEditable || fSelectable) {
		if (shiftKeyDown)
			Select(selStart, selEnd);
		else
			Select(fCaretOffset, fCaretOffset);

		// scroll if needed
		ScrollToOffset(fCaretOffset);
	}
}


void
BTextView::_HandleDelete(int32 modifiers)
{
	if (!fEditable)
		return;

	if (modifiers < 0)
		modifiers = _CurrentModifiers();

	const auto flags = BModifierFlags::FromRaw(static_cast<uint32>(modifiers));
	const bool controlKeyDown = flags.Has(BModifier::Control);
	const bool optionKeyDown = flags.Has(BModifier::Option);
	const bool commandKeyDown = flags.Has(BModifier::Command);

	if ((commandKeyDown || optionKeyDown) && !controlKeyDown) {
		fSelStart = fCaretOffset;
		// NewGUI: the next grapheme (Haiku: + 1)
		fSelEnd = _NextInitialByte(_NextWordEnd(fCaretOffset));
	}

	if (TypingUndoBuffer* undoBuffer = _TypingUndo())
		undoBuffer->ForwardErase();

	if (fSelStart == fSelEnd) {
		if (fSelEnd != TextLength())
			fSelEnd = _NextInitialByte(fSelEnd);
	} else
		Highlight(fSelStart, fSelEnd);

	DeleteText(fSelStart, fSelEnd);
	fCaretOffset = fSelEnd = fSelStart;

	_Refresh(fCaretOffset);
}


void
BTextView::_HandlePageKey(uint8 pageKey, int32 modifiers)
{
	if (modifiers < 0)
		modifiers = _CurrentModifiers();

	const auto flags = BModifierFlags::FromRaw(static_cast<uint32>(modifiers));
	const bool shiftKeyDown = flags.Has(BModifier::Shift);
	const bool controlKeyDown = flags.Has(BModifier::Control);
	const bool optionKeyDown = flags.Has(BModifier::Option);
	const bool commandKeyDown = flags.Has(BModifier::Command);

	int32 selStart = fSelStart;
	int32 selEnd = fSelEnd;

	const int32 lastClickOffset = fCaretOffset;
	switch (pageKey) {
		case keys::kHome:
			if (!fEditable && !fSelectable) {
				fCaretOffset = 0;
				_ScrollTo(0, 0);
				break;
			} else {
				if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, 0);
					fCaretOffset = 0;
				} else {
					// move cursor to start of current line
					fCaretOffset = OffsetAt(LineAt(lastClickOffset));
				}

				if (!shiftKeyDown)
					selStart = selEnd = fCaretOffset;
				else if (fCaretOffset != lastClickOffset) {
					if (fCaretOffset < fSelStart) {
						// extend selection to the left
						selStart = fCaretOffset;
						if (lastClickOffset > fSelStart) {
							// caret has jumped across "anchor"
							selEnd = fSelStart;
						}
					} else {
						// shrink selection from the right
						selEnd = fCaretOffset;
					}
				}
			}
			break;

		case keys::kEnd:
			if (!fEditable && !fSelectable) {
				fCaretOffset = TextLength();
				_ScrollTo(0, fTextRect.y1 + fLayoutData.bottomInset);
				break;
			} else {
				if (commandKeyDown && !optionKeyDown && !controlKeyDown) {
					_ScrollTo(0, fTextRect.y1 + fLayoutData.bottomInset);
					fCaretOffset = TextLength();
				} else {
					// If we are on the last line, just go to the last
					// character in the buffer, otherwise get the starting
					// offset of the next line, and go to the previous
					// character
					const int32 currentLine = LineAt(lastClickOffset);
					if (currentLine + 1 < CountLines()
						&& !_IsOnEmptyLastLine(OffsetAt(currentLine + 1))) {
						fCaretOffset = _PreviousInitialByte(OffsetAt(currentLine + 1));
					} else {
						if (fCaretOffset != TextLength()) {
							// This check is needed to avoid moving the
							// cursor when the cursor is on the last line,
							// and that line is empty
							fCaretOffset = TextLength();
							if (!fHideTyping && ByteAt(fCaretOffset - 1) == '\n')
								fCaretOffset--;
						}
					}
				}

				if (!shiftKeyDown)
					selStart = selEnd = fCaretOffset;
				else if (fCaretOffset != lastClickOffset) {
					if (fCaretOffset > fSelEnd) {
						// extend selection to the right
						selEnd = fCaretOffset;
						if (lastClickOffset < fSelEnd) {
							// caret has jumped across "anchor"
							selStart = fSelEnd;
						}
					} else {
						// shrink selection from the left
						selStart = fCaretOffset;
					}
				}
			}
			break;

		case keys::kPageUp:
		{
			float lineHeight;
			const gfx::Point currentPos = PointAt(fCaretOffset, &lineHeight);
			gfx::Point nextPos{currentPos.x, currentPos.y + lineHeight - Bounds().Height()};
			fCaretOffset = OffsetAt(nextPos);
			nextPos = PointAt(fCaretOffset);
			_ScrollBy(0, nextPos.y - currentPos.y);

			if (!fEditable && !fSelectable)
				break;

			if (!shiftKeyDown)
				selStart = selEnd = fCaretOffset;
			else if (fCaretOffset != lastClickOffset) {
				if (fCaretOffset < fSelStart) {
					// extend selection to the top
					selStart = fCaretOffset;
					if (lastClickOffset > fSelStart) {
						// caret has jumped across "anchor"
						selEnd = fSelStart;
					}
				} else {
					// shrink selection from the bottom
					selEnd = fCaretOffset;
				}
			}

			break;
		}

		case keys::kPageDown:
		{
			const gfx::Point currentPos = PointAt(fCaretOffset);
			gfx::Point nextPos{currentPos.x, currentPos.y + Bounds().Height()};
			fCaretOffset = OffsetAt(nextPos);
			nextPos = PointAt(fCaretOffset);
			_ScrollBy(0, nextPos.y - currentPos.y);

			if (!fEditable && !fSelectable)
				break;

			if (!shiftKeyDown)
				selStart = selEnd = fCaretOffset;
			else if (fCaretOffset != lastClickOffset) {
				if (fCaretOffset > fSelEnd) {
					// extend selection to the bottom
					selEnd = fCaretOffset;
					if (lastClickOffset < fSelEnd) {
						// caret has jumped across "anchor"
						selStart = fSelEnd;
					}
				} else {
					// shrink selection from the top
					selStart = fCaretOffset;
				}
			}

			break;
		}

		default:
			break;
	}

	if (fEditable || fSelectable) {
		if (shiftKeyDown)
			Select(selStart, selEnd);
		else
			Select(fCaretOffset, fCaretOffset);

		ScrollToOffset(fCaretOffset);
	}
}


void
BTextView::_HandleAlphaKey(std::string_view bytes)
{
	if (!fEditable)
		return;

	if (TypingUndoBuffer* undoBuffer = _TypingUndo())
		undoBuffer->InputCharacter(static_cast<int32>(bytes.size()));

	if (fSelStart != fSelEnd) {
		Highlight(fSelStart, fSelEnd);
		DeleteText(fSelStart, fSelEnd);
	}

	// we're only going to handle the first character
	// NewGUI: not in hidden text, which is one line
	if (fAutoindent && !fHideTyping && bytes.size() == 1 && bytes[0] == keys::kEnter) {
		int32 start;
		int32 offset;
		start = offset = OffsetAt(LineAt(fSelStart));

		while (ByteAt(offset) != '\0'
			&& (ByteAt(offset) == keys::kTab || ByteAt(offset) == keys::kSpace)
			&& offset < fSelStart) {
			offset++;
		}

		_DoInsertText(bytes, fSelStart);
		if (start != offset) {
			const std::string indent = GetText(start, offset - start);
			_DoInsertText(indent, fSelStart);
		}
	} else
		_DoInsertText(bytes, fSelStart);

	fCaretOffset = fSelEnd;
	ScrollToOffset(fCaretOffset);
}


void
BTextView::_DoInsertText(std::string_view text, int32 offset, const BTextRunArray* runs)
{
	_CancelInputMethod();

	// Don't do any work if the text view is full
	if (TextLength() + static_cast<int32>(text.size()) > MaxBytes())
		return;

	if (fSelStart != fSelEnd)
		Select(fSelStart, fSelStart);

	offset = std::min(offset, TextLength());

	// copy data into buffer
	InsertText(text, offset, runs);

	// recalc line breaks and draw the text
	_Refresh();
}


void
BTextView::_ApplyStyleRange(int32 fromOffset, int32 toOffset, BFontModeFlags mode,
	const BFont* font, const std::optional<gfx::Color>* color, bool syncNullStyle)
{
	// NewGUI: no _NormalizeFont() (no encodings and spacing)
	if (!fStylable) {
		// always apply font and color to full range for non-stylable textviews
		fromOffset = 0;
		toOffset = TextLength();
	}

	if (syncNullStyle)
		fStyles->SyncNullStyle(fromOffset);

	fStyles->SetStyleRange(fromOffset, toOffset, TextLength(), mode, font, color);
}


void
BTextView::_SetRunArray(int32 startOffset, int32 endOffset, const BTextRunArray& runs)
{
	const auto numStyles = static_cast<int32>(runs.size());
	if (numStyles > 0) {
		for (int32 index = 0; index < numStyles; index++) {
			const BTextRun& theRun = runs[static_cast<std::size_t>(index)];
			const int32 fromOffset = theRun.offset + startOffset;
			int32 toOffset = endOffset;
			if (index + 1 < numStyles) {
				toOffset = runs[static_cast<std::size_t>(index + 1)].offset + startOffset;
				toOffset = (toOffset > endOffset) ? endOffset : toOffset;
			}

			// NewGUI: a run beyond the range styles nothing (Haiku styled from
			// its offset backwards, or changed the null style)
			if (fromOffset >= toOffset)
				continue;

			// NewGUI: the text color stays the text color, so that the text
			// follows it
			const std::optional<gfx::Color> color = theRun.color == _ResolvedColor(std::nullopt)
				? std::nullopt : std::optional<gfx::Color>(theRun.color);
			_ApplyStyleRange(fromOffset, toOffset, kFontAll, &theRun.font, &color, false);
		}
		fStyles->InvalidateNullStyle();
	}
}


std::vector<TextStyleRunEntry>
BTextView::_StyleRuns(int32 startOffset, int32 endOffset) const
{
	return fStyles->GetStyleRange(startOffset, endOffset - 1);
}


void
BTextView::_SetStyleRuns(int32 startOffset, int32 endOffset,
	const std::vector<TextStyleRunEntry>& runs)
{
	// _SetRunArray() with the runs as kept, colors that follow the text
	// color included
	const auto numStyles = static_cast<int32>(runs.size());
	for (int32 index = 0; index < numStyles; index++) {
		const TextStyleRunEntry& theRun = runs[static_cast<std::size_t>(index)];
		const int32 fromOffset = theRun.offset + startOffset;
		int32 toOffset = endOffset;
		if (index + 1 < numStyles) {
			toOffset = std::min(runs[static_cast<std::size_t>(index + 1)].offset + startOffset,
				endOffset);
		}
		if (fromOffset >= toOffset)
			continue;
		_ApplyStyleRange(fromOffset, toOffset, kFontAll, &theRun.style.font,
			&theRun.style.color, false);
	}
	if (numStyles > 0)
		fStyles->InvalidateNullStyle();
}


void
BTextView::_InsertStyled(int32 offset, std::string_view text,
	const std::vector<TextStyleRunEntry>& runs)
{
	// Haiku's undo buffers: Insert() with the run array they kept; styled
	// is only what was inserted, where it was
	const int32 at = std::clamp(offset, 0, TextLength());
	const int32 before = TextLength();
	const auto length = static_cast<int32>(text.size());
	Insert(at, text);
	if (fStylable && !runs.empty() && length > 0 && TextLength() - before == length) {
		_SetStyleRuns(at, at + length, runs);
		_Refresh();
	}
}


std::vector<TextStyleRun>
BTextView::_LayoutRuns() const
{
	std::vector<TextStyleRun> runs;
	if (fStyles->NumRuns() == 0) {
		const TextStyle& style = fStyles->NullStyle();
		runs.push_back({0, style.font.Spec(), style.color});
		return runs;
	}

	for (int32 i = 0; i < fStyles->NumRuns(); i++) {
		const TextStyleRunEntry& run = fStyles->RunAt(i);
		// the first run from the start
		const int32 start = i == 0 ? 0 : std::max(run.offset, 0);
		runs.push_back({static_cast<std::size_t>(start), run.style.font.Spec(), run.style.color});
	}
	return runs;
}


gfx::Color
BTextView::_ResolvedColor(const std::optional<gfx::Color>& color) const
{
	return color.value_or(fTextColor.value_or(
		Palette::Default().Get(BColorWhich::DocumentText)));
}


std::string
BTextView::_FilterDisallowedChars(std::string_view text, BTextRunArray* runArray) const
{
	// Haiku's _FilterDisallowedChars(), byte by byte; the runs move with
	// the text that is left
	std::string filtered;
	filtered.reserve(text.size());
	std::vector<int32> keptBefore(text.size() + 1, 0);
	for (std::size_t i = 0; i < text.size(); i++) {
		keptBefore[i] = static_cast<int32>(filtered.size());
		const char c = text[i];
		if (std::find(fDisallowedChars.begin(), fDisallowedChars.end(),
				static_cast<uint32>(static_cast<unsigned char>(c))) == fDisallowedChars.end()) {
			filtered.push_back(c);
		}
	}
	keptBefore[text.size()] = static_cast<int32>(filtered.size());

	if (runArray != nullptr) {
		for (BTextRun& run : *runArray) {
			const auto offset = static_cast<std::size_t>(
				std::clamp(run.offset, 0, static_cast<int32>(text.size())));
			run.offset = keptBefore[offset];
		}
	}
	return filtered;
}


BTextRunArray
BTextView::_ResolvedRuns(const std::vector<TextStyleRunEntry>& runs) const
{
	BTextRunArray runArray;
	for (const TextStyleRunEntry& run : runs)
		runArray.push_back({run.offset, run.style.font, _ResolvedColor(run.style.color)});
	return runArray;
}


BTextView::TypingUndoBuffer*
BTextView::_TypingUndo()
{
	if (fUndo == nullptr)
		return nullptr;

	auto* undoBuffer = dynamic_cast<TypingUndoBuffer*>(fUndo.get());
	if (undoBuffer == nullptr) {
		auto typing = std::make_unique<TypingUndoBuffer>(this);
		undoBuffer = typing.get();
		fUndo = std::move(typing);
	}
	return undoBuffer;
}


void
BTextView::_ShowCaret()
{
	if (fActive && !fCaretVisible && (fEditable || fSelectable) && fSelStart == fSelEnd) {
		fCaretVisible = true;
		_InvalidateCaret();
		_StartBlinking();
		_UpdateTextInputArea();
	}
}


void
BTextView::_HandleInputMethodChanged(BMessage* message)
{
	if (!message->HasString("be:string"))
		return;
	const std::string string(message->GetString("be:string", ""));

	_HideCaret();

	const bool confirmed = message->GetBool("be:confirmed", false);

	// Delete the previously inserted text (if any)
	if (fInline->active) {
		const int32 oldOffset = fInline->offset;
		DeleteText(oldOffset, oldOffset + fInline->length);
		if (confirmed)
			fInline->active = false;
		fCaretOffset = fSelStart = fSelEnd = oldOffset;
	}

	const auto stringLen = static_cast<int32>(string.size());

	fInline->offset = fSelStart;
	fInline->length = stringLen;
	fInline->clauses.clear();

	if (!confirmed && !fInline->active)
		fInline->active = true;

	// Get the clauses, and pass them to the InlineInput object
	for (int32 i = 0; auto start = message->FindInt32("be:clause_start", i); i++) {
		auto end = message->FindInt32("be:clause_end", i);
		if (!end.has_value())
			break;
		fInline->clauses.emplace_back(*start, *end);
	}

	if (confirmed) {
		_Refresh(fSelEnd);
		_ShowCaret();

		// now we need to feed ourselves the individual characters as if the
		// user would have pressed them now - this lets KeyDown() pick out all
		// the special characters like B_BACKSPACE, cursor keys and the like:
		std::size_t i = 0;
		while (i < string.size()) {
			std::size_t next = i + 1;
			while (next < string.size()
				&& (static_cast<unsigned char>(string[next]) & 0xc0) == 0x80) {
				next++;
			}
			KeyDown(std::string_view(string).substr(i, next - i));
			i = next;
		}

		_Refresh(fSelEnd);
	} else {
		// temporarily show transient state of inline input
		fInline->selectionOffset = message->GetInt32("be:selection", 0);
		const auto selectionEnd = message->FindInt32("be:selection", 1);
		fInline->selectionLength = selectionEnd.has_value()
			? *selectionEnd - fInline->selectionOffset : 0;

		InsertText(string, fSelStart, nullptr);

		_Refresh(fSelEnd);
		_ShowCaret();
	}
	_UpdateTextInputArea();
}


void
BTextView::_RemoveInlineInput()
{
	// NewGUI: the composed text goes (Haiku's input method sent the final
	// text confirmed instead)
	if (fInline == nullptr || !fInline->active)
		return;

	_HideCaret();
	const int32 offset = fInline->offset;
	DeleteText(offset, offset + fInline->length);
	fInline->active = false;
	fCaretOffset = fSelStart = fSelEnd = offset;
	_Refresh(fCaretOffset);
	_ShowCaret();
}


void
BTextView::_CancelInputMethod()
{
	if (fInline == nullptr)
		return;

	const bool active = fInline->active;
	fInline.reset();

	if (active && Window() != nullptr) {
		// the composed text stays; the input method ends (Haiku: sent it
		// B_INPUT_METHOD_STOPPED)
		_Refresh();
		Window()->_ClearComposition();
	}
}


void
BTextView::_UpdateTextInputArea()
{
	// NewGUI: where the platform's input method shows its candidates
	// (Haiku answered B_INPUT_METHOD_LOCATION_REQUEST)
	if (Window() == nullptr || !fActive)
		return;

	const int32 offset = fInline != nullptr && fInline->active ? fInline->offset : fSelStart;
	float height;
	const gfx::Point point = PointAt(offset, &height);
	const gfx::Point window = ConvertToWindow(point);
	Window()->_SetTextInputArea({window.x, window.y, window.x + 1, window.y + height}, 0);
}


void
BTextView::_HideCaret()
{
	if (fCaretVisible && fSelStart == fSelEnd) {
		fCaretVisible = false;
		_InvalidateCaret();
	}
}


void
BTextView::_InvalidateCaret()
{
	// NewGUI: views are redrawn whole
	Invalidate();
}


void
BTextView::_StartBlinking()
{
	// NewGUI: the caret changes every 500 ms from now on (Haiku: Pulse(),
	// 500 ms after fCaretTime)
	if (Window() == nullptr)
		return;
	fCaretRunner = std::make_unique<BMessageRunner>(BMessenger(this),
		BMessage(kMsgCaretBlink), kCaretBlinkRate);
}


void
BTextView::_StopMouseTracking()
{
	fTrackingMouse.reset();
}


bool
BTextView::_PerformMouseUp(gfx::Point /*where*/)
{
	if (!fTrackingMouse.has_value())
		return false;

	_StopMouseTracking();
	return true;
}


bool
BTextView::_PerformMouseMoved(gfx::Point where)
{
	if (!fTrackingMouse.has_value())
		return false;

	const int32 currentOffset = OffsetAt(where);

	TrackState& tracking = *fTrackingMouse;
	switch (fClickCount) {
		case 3:
			// triple click, extend selection linewise
			if (currentOffset <= tracking.anchor) {
				tracking.selStart = OffsetAt(LineAt(currentOffset));
				tracking.selEnd = tracking.shiftDown ? fSelEnd
					: OffsetAt(LineAt(tracking.anchor) + 1);
			} else {
				tracking.selStart = tracking.shiftDown ? fSelStart
					: OffsetAt(LineAt(tracking.anchor));
				tracking.selEnd = OffsetAt(LineAt(currentOffset) + 1);
			}
			break;

		case 2:
			// double click, extend selection wordwise
			if (currentOffset <= tracking.anchor) {
				tracking.selStart = _PreviousWordBoundary(currentOffset);
				tracking.selEnd = tracking.shiftDown ? fSelEnd
					: _NextWordBoundary(tracking.anchor);
			} else {
				tracking.selStart = tracking.shiftDown ? fSelStart
					: _PreviousWordBoundary(tracking.anchor);
				tracking.selEnd = _NextWordBoundary(currentOffset);
			}
			break;

		default:
			// new click, extend selection char by char
			if (currentOffset <= tracking.anchor) {
				tracking.selStart = currentOffset;
				tracking.selEnd = tracking.shiftDown ? fSelEnd : tracking.anchor;
			} else {
				tracking.selStart = tracking.shiftDown ? fSelStart : tracking.anchor;
				tracking.selEnd = currentOffset;
			}
			break;
	}

	// position caret to follow the direction of the selection
	if (tracking.selEnd != fSelEnd)
		fCaretOffset = tracking.selEnd;
	else if (tracking.selStart != fSelStart)
		fCaretOffset = tracking.selStart;

	Select(tracking.selStart, tracking.selEnd);
	return true;
}


void
BTextView::_UpdateScrollbars()
{
	const gfx::Rect bounds = Bounds();
	BScrollBar* horizontalScrollBar = ScrollBar(BOrientation::Horizontal);
	BScrollBar* verticalScrollBar = ScrollBar(BOrientation::Vertical);

	// do we have a horizontal scroll bar?
	if (horizontalScrollBar != nullptr) {
		const float viewWidth = bounds.Width();
		const float dataWidth = std::ceil(_TextWidth());

		const float maxRange = std::max(dataWidth - viewWidth, 0.0f);

		horizontalScrollBar->SetRange(0, maxRange);
		horizontalScrollBar->SetProportion(viewWidth / dataWidth);
		horizontalScrollBar->SetSteps(kHorizontalScrollBarStep, dataWidth / 10);
	}

	// how about a vertical scroll bar?
	if (verticalScrollBar != nullptr) {
		const float viewHeight = bounds.Height();
		const float dataHeight = std::ceil(_TextHeight());

		const float maxRange = std::max(dataHeight - viewHeight, 0.0f);

		verticalScrollBar->SetRange(0, maxRange);
		verticalScrollBar->SetProportion(viewHeight / dataHeight);
		verticalScrollBar->SetSteps(kVerticalScrollBarStep, viewHeight);
	}
}


void
BTextView::_ScrollBy(float horizontal, float vertical)
{
	const gfx::Rect bounds = Bounds();
	_ScrollTo(bounds.x0 + horizontal, bounds.y0 + vertical);
}


void
BTextView::_ScrollTo(float x, float y)
{
	const gfx::Rect bounds = Bounds();
	const float viewWidth = bounds.Width();
	const float viewHeight = bounds.Height();

	const float minWidth = fTextRect.x0 - fLayoutData.leftInset;
	const float maxWidth = fTextRect.x1 + fLayoutData.rightInset - viewWidth;
	const float minHeight = fTextRect.y0 - fLayoutData.topInset;
	const float maxHeight = fTextRect.y1 + fLayoutData.bottomInset - viewHeight;

	if (x > maxWidth)
		x = maxWidth;
	if (x < minWidth)
		x = minWidth;
	if (y > maxHeight)
		y = maxHeight;
	if (y < minHeight)
		y = minHeight;

	ScrollTo({x, y});
}


void
BTextView::_Activate()
{
	fActive = true;

	if (fSelStart != fSelEnd) {
		if (fSelectable)
			Highlight(fSelStart, fSelEnd);
	} else
		_ShowCaret();

	BWindow* window = Window();
	if (window == nullptr)
		return;

	// The window keeps Command keys for its shortcuts: those the text view
	// handles come to it as shortcuts, if the window has none of its own.
	for (const auto& pair : kNavigationShortcuts) {
		if (window->HasShortcut(pair[0].key, pair[0].modifiers)
			|| window->HasShortcut(pair[1].key, pair[1].modifiers)) {
			continue;
		}
		for (const NavigationShortcut& shortcut : pair) {
			auto message = std::make_unique<BMessage>(shortcut.what);
			(void)message->AddInt32("key", shortcut.key);
			(void)message->AddInt32("modifiers",
				static_cast<int32>(shortcut.modifiers.Raw()));
			window->AddShortcut(shortcut.key, shortcut.modifiers, std::move(message), this);
			fInstalledShortcuts.emplace_back(shortcut.key, shortcut.modifiers);
		}
	}
}


void
BTextView::_Deactivate()
{
	fActive = false;
	_CancelInputMethod();
	fCaretRunner.reset();

	if (fSelStart != fSelEnd) {
		if (fSelectable)
			Highlight(fSelStart, fSelEnd);
	} else
		_HideCaret();

	if (BWindow* window = Window()) {
		for (const auto& [key, modifiers] : fInstalledShortcuts)
			window->RemoveShortcut(key, modifiers);
	}
	fInstalledShortcuts.clear();
}


uint32
BTextView::_CharClassification(int32 offset) const
{
	// NewGUI: hidden text is one word, without white space or line breaks
	// (as its TextLayout)
	if (fHideTyping && ByteAt(offset) != '\0')
		return CHAR_CLASS_DEFAULT;

	switch (ByteAt(offset)) {
		case '\0':
			return CHAR_CLASS_END_OF_TEXT;

		case keys::kSpace:
		case keys::kTab:
		case keys::kEnter:
			return CHAR_CLASS_WHITESPACE;

		case '=':
		case '+':
		case '@':
		case '#':
		case '$':
		case '%':
		case '^':
		case '&':
		case '*':
		case '\\':
		case '|':
		case '<':
		case '>':
		case '/':
		case '~':
			return CHAR_CLASS_GRAPHICAL;

		case '\'':
		case '"':
			return CHAR_CLASS_QUOTE;

		case ',':
		case '.':
		case '?':
		case '!':
		case ';':
		case ':':
		case '-':
			return CHAR_CLASS_PUNCTUATION;

		case '(':
		case '[':
		case '{':
			return CHAR_CLASS_PARENS_OPEN;

		case ')':
		case ']':
		case '}':
			return CHAR_CLASS_PARENS_CLOSE;

		default:
			return CHAR_CLASS_DEFAULT;
	}
}


int32
BTextView::_NextInitialByte(int32 offset) const
{
	// NewGUI: the next grapheme
	if (offset >= TextLength())
		return offset;
	return static_cast<int32>(fLayout.CaretRight(static_cast<std::size_t>(std::max(offset, 0))));
}


int32
BTextView::_PreviousInitialByte(int32 offset) const
{
	// NewGUI: the previous grapheme
	if (offset <= 0)
		return 0;
	return static_cast<int32>(fLayout.CaretLeft(static_cast<std::size_t>(offset)));
}


int32
BTextView::_PreviousLineStart(int32 offset)
{
	if (offset <= 0)
		return 0;

	while (offset > 0) {
		offset = _PreviousInitialByte(offset);
		if (_CharClassification(offset) == CHAR_CLASS_WHITESPACE && ByteAt(offset) == '\n')
			return offset + 1;
	}

	return offset;
}


int32
BTextView::_NextLineEnd(int32 offset)
{
	const int32 textLen = TextLength();
	if (offset >= textLen)
		return textLen;

	while (offset < textLen) {
		if (_CharClassification(offset) == CHAR_CLASS_WHITESPACE && ByteAt(offset) == '\n')
			break;
		offset = _NextInitialByte(offset);
	}

	return offset;
}


int32
BTextView::_PreviousWordBoundary(int32 offset)
{
	const uint32 charType = _CharClassification(offset);
	int32 previous;
	while (offset > 0) {
		previous = _PreviousInitialByte(offset);
		if (_CharClassification(previous) != charType)
			break;
		offset = previous;
	}

	return offset;
}


int32
BTextView::_NextWordBoundary(int32 offset)
{
	const int32 textLen = TextLength();
	const uint32 charType = _CharClassification(offset);
	while (offset < textLen) {
		offset = _NextInitialByte(offset);
		if (_CharClassification(offset) != charType)
			break;
	}

	return offset;
}


int32
BTextView::_PreviousWordStart(int32 offset)
{
	if (offset <= 1)
		return 0;

	--offset;
		// need to look at previous char
	if (_CharClassification(offset) != CHAR_CLASS_DEFAULT) {
		// skip non-word characters
		while (offset > 0) {
			offset = _PreviousInitialByte(offset);
			if (_CharClassification(offset) == CHAR_CLASS_DEFAULT)
				break;
		}
	}
	while (offset > 0) {
		// skip to start of word
		const int32 previous = _PreviousInitialByte(offset);
		if (_CharClassification(previous) != CHAR_CLASS_DEFAULT)
			break;
		offset = previous;
	}

	return offset;
}


int32
BTextView::_NextWordEnd(int32 offset)
{
	const int32 textLen = TextLength();
	if (_CharClassification(offset) != CHAR_CLASS_DEFAULT) {
		// skip non-word characters
		while (offset < textLen) {
			offset = _NextInitialByte(offset);
			if (_CharClassification(offset) == CHAR_CLASS_DEFAULT)
				break;
		}
	}
	while (offset < textLen) {
		// skip to end of word
		offset = _NextInitialByte(offset);
		if (_CharClassification(offset) != CHAR_CLASS_DEFAULT)
			break;
	}

	return offset;
}


bool
BTextView::_IsOnEmptyLastLine(int32 offset) const
{
	return !fHideTyping && offset == TextLength() && offset > 0
		&& ByteAt(offset - 1) == '\n';
}


void
BTextView::_UpdateInsets(const gfx::Rect& rect)
{
	// do not update insets if SetInsets() was called
	if (fLayoutData.overridden)
		return;

	const gfx::Rect bounds = Bounds();

	// we disallow negative insets, as they would cause parts of the
	// text to be hidden
	fLayoutData.leftInset = rect.x0 >= bounds.x0 ? rect.x0 - bounds.x0 : 0;
	fLayoutData.topInset = rect.y0 >= bounds.y0 ? rect.y0 - bounds.y0 : 0;
	fLayoutData.rightInset = bounds.x1 >= rect.x1 ? bounds.x1 - rect.x1 : 0;
	fLayoutData.bottomInset = bounds.y1 >= rect.y1 ? bounds.y1 - rect.y1 : 0;

	// only add default insets if text rect is set to bounds
	if (rect == bounds && (fEditable || fSelectable)) {
		const float hPadding = Look().DefaultLabelSpacing(TextSystem::Default());
		const float hInset = std::floor(hPadding / 2.0f);
		const float vInset = 1;
		fLayoutData.leftInset += hInset;
		fLayoutData.topInset += vInset;
		fLayoutData.rightInset += hInset;
		fLayoutData.bottomInset += vInset;
	}
}


float
BTextView::_TextWidth() const
{
	return fTextRect.Width() + fLayoutData.leftInset + fLayoutData.rightInset;
}


float
BTextView::_TextHeight() const
{
	return fTextRect.Height() + fLayoutData.topInset + fLayoutData.bottomInset;
}


float
BTextView::_UneditableTint() const
{
	// Haiku's rgb_color::IsLight(): brightness above 127
	const gfx::Color background = Palette::Default().Get(BColorWhich::DocumentBackground);
	const float brightness = (background.r * 299 + background.g * 587
		+ background.b * 114) * 255 / 1000;
	return brightness > 127 ? kDarken1Tint : 0.853f;
}


} // namespace ngui
