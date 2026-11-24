/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextView.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Clipboard.h>
#include <ngui/Clock.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kSetText = FourCharCode("STXT");


// A window with a text view of 200 × 100 at (10, 10), focused; its text
// begins at (13, 11) in the window (insets 3 and 1). It answers kAsk with
// the text, the selection and the number of lines, and sets the text for
// kSetText.
class TextWindow : public BWindow {
public:
	TextWindow()
		:
		BWindow({0, 0, 300, 200}, "text")
	{
		auto view = std::make_unique<BTextView>(gfx::Rect{10, 10, 210, 110}, "text",
			gfx::Rect{0, 0, 200, 100});
		fView = static_cast<BTextView*>(AddChild(std::move(view)));
		fView->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSetText) {
			fView->SetText(message->GetString("text", ""));
			return;
		}
		if (message->what == kUndo) {
			// as a menu item targeting the focus would
			fView->MessageReceived(message);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		int32 start = 0;
		int32 end = 0;
		fView->GetSelection(&start, &end);
		(void)reply.AddString("text", fView->Text());
		(void)reply.AddInt32("start", start);
		(void)reply.AddInt32("end", end);
		(void)reply.AddInt32("lines", fView->CountLines());
		(void)message->SendReply(reply);
	}

private:
	BTextView* fView = nullptr;
};


struct TextState {
	std::string text;
	int32 start = -1;
	int32 end = -1;
	int32 lines = 0;
};


// The fixed text system for the duration of a test, and a running
// TextWindow, activated.
class RunningText {
public:
	RunningText()
	{
		TextSystem::SetDefault(&fText);
		fWindow = *BLooper::Run(std::make_unique<TextWindow>());
		BMessage activated(kWindowActivated);
		(void)activated.AddBool("active", true);
		EXPECT_TRUE(fWindow.SendMessage(activated).has_value());
	}

	~RunningText()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		TextSystem::SetDefault(nullptr);
	}

	void Key(std::string_view bytes, BModifierFlags modifiers = {})
	{
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", bytes);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Type(std::string_view text)
	{
		for (char c : text)
			Key(std::string(1, c));
	}

	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons,
		BModifierFlags modifiers = {})
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", buttons);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Click(gfx::Point where, BModifierFlags modifiers = {})
	{
		Mouse(kMouseDown, where, 1, modifiers);
		Mouse(kMouseUp, where, 0, modifiers);
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(BMessage(what)).has_value());
	}

	void Compose(std::string_view text, bool confirmed = false)
	{
		BMessage message(kInputMethodEvent);
		if (text.empty() && !confirmed) {
			(void)message.AddInt32("be:opcode", kInputMethodStopped);
		} else {
			(void)message.AddInt32("be:opcode", kInputMethodChanged);
			(void)message.AddString("be:string", text);
			(void)message.AddBool("be:confirmed", confirmed);
			(void)message.AddInt32("be:selection", 0);
			(void)message.AddInt32("be:selection", static_cast<std::int32_t>(text.size()));
		}
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void SetText(std::string_view text)
	{
		BMessage message(kSetText);
		(void)message.AddString("text", text);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	TextState Ask()
	{
		BMessage reply;
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		EXPECT_TRUE(fWindow.SendMessage(BMessage(kAsk), &reply).has_value());
		return {std::string(reply.GetString("text", "?")), reply.GetInt32("start", -1),
			reply.GetInt32("end", -1), reply.GetInt32("lines", 0)};
	}

private:
	test::FixedTextSystem fText;
	BMessenger fWindow;
};


// The window point in the middle of the byte \a offset of line \a line
// (7 pixels per byte, 14 per line, text at (13, 11)).
gfx::Point
At(int32 offset, int32 line = 0)
{
	return {13 + 7.0f * static_cast<float>(offset) + 2, 11 + 14.0f * static_cast<float>(line) + 5};
}


TEST(TextViewTest, TheKeyboardEditsTheText)
{
	// What is tested: Typing "Hello world" into a focused text view of an
	// active window; Left five times; Shift+Right five times; typing
	// "there"; Command+Left; Backspace; End, Home, Shift+End; Command+A;
	// Delete; Enter between two lines and Up.
	// Success criteria: The text and the caret follow as in Haiku: Left
	// moves the caret before "world" (6); Shift+Right selects "world"
	// (6, 11), which the typed text replaces; Command+Left (a shortcut the
	// view installs, the window keeps Command keys) goes to the start of
	// "there"; Backspace removes the space before it; Home and End go to
	// the line's ends, Shift+End selects to the end; Command+A selects all,
	// Delete removes it; Enter makes two lines, Up goes to the first.
	// Why it matters: A text view is used with the keyboard first.
	//
	// Given
	RunningText text;

	// When / Then
	text.Type("Hello world");
	EXPECT_EQ(text.Ask().text, "Hello world");
	for (int i = 0; i < 5; i++)
		text.Key("\x1c");
	EXPECT_EQ(text.Ask().start, 6);
	for (int i = 0; i < 5; i++)
		text.Key("\x1d", BModifier::Shift);
	const TextState selected = text.Ask();
	EXPECT_EQ(selected.start, 6);
	EXPECT_EQ(selected.end, 11);
	text.Type("there");
	EXPECT_EQ(text.Ask().text, "Hello there");
	text.Key("\x1c", BModifier::Command);
	EXPECT_EQ(text.Ask().start, 6);
	text.Key("\x08");
	const TextState backspaced = text.Ask();
	EXPECT_EQ(backspaced.text, "Hellothere");
	EXPECT_EQ(backspaced.start, 5);
	text.Key("\x04");
	EXPECT_EQ(text.Ask().start, 10);
	text.Key("\x01");
	EXPECT_EQ(text.Ask().start, 0);
	text.Key("\x04", BModifier::Shift);
	const TextState toEnd = text.Ask();
	EXPECT_EQ(toEnd.start, 0);
	EXPECT_EQ(toEnd.end, 10);
	text.Key("\x1d");
	text.Key("a", BModifier::Command);
	const TextState all = text.Ask();
	EXPECT_EQ(all.start, 0);
	EXPECT_EQ(all.end, 10);
	text.Key("\x7f");
	EXPECT_EQ(text.Ask().text, "");
	text.Type("ab");
	text.Key("\n");
	text.Type("cd");
	const TextState twoLines = text.Ask();
	EXPECT_EQ(twoLines.text, "ab\ncd");
	EXPECT_EQ(twoLines.lines, 2);
	text.Key("\x1e");
	EXPECT_EQ(text.Ask().start, 2);
}


TEST(TextViewTest, TheMouseSelects)
{
	// What is tested: With a ManualClock, in "one two three\nfour": a click
	// in "two"; a second click there at once; a third; a click later; a
	// Shift+click into the second line; a press in "one" dragged to "three"
	// and released.
	// Success criteria: A click places the caret where it hits (between
	// the bytes nearest to the point); a double click selects the word
	// (4, 7); a triple click the line with its line break (0, 14); a click
	// after the click time is a single click again; Shift extends the
	// selection from the caret; dragging selects from the press to the
	// release.
	// Why it matters: Selecting with the mouse works as in Haiku.
	//
	// Given
	ManualClock clock;
	Clock::SetDefault(&clock);
	{
		RunningText text;
		text.SetText("one two three\nfour");

		// When / Then
		text.Click(At(5));
		const TextState clicked = text.Ask();
		EXPECT_EQ(clicked.start, 5);
		EXPECT_EQ(clicked.end, 5);
		text.Click(At(5));
		const TextState word = text.Ask();
		EXPECT_EQ(word.start, 4);
		EXPECT_EQ(word.end, 7);
		text.Click(At(5));
		const TextState line = text.Ask();
		EXPECT_EQ(line.start, 0);
		EXPECT_EQ(line.end, 14);
		clock.Advance(1s);
		text.Click(At(1));
		EXPECT_EQ(text.Ask().end, 1);
		clock.Advance(1s);
		text.Click(At(2, 1), BModifier::Shift);
		const TextState extended = text.Ask();
		EXPECT_EQ(extended.start, 1);
		EXPECT_EQ(extended.end, 16);
		clock.Advance(1s);
		text.Mouse(kMouseDown, At(1), 1);
		text.Mouse(kMouseMoved, At(10), 1);
		text.Mouse(kMouseUp, At(10), 0);
		const TextState dragged = text.Ask();
		EXPECT_EQ(dragged.start, 1);
		EXPECT_EQ(dragged.end, 10);
	}
	Clock::SetDefault(nullptr);
}


TEST(TextViewTest, LimitsWrappingAndGeometry)
{
	// What is tested: A text view (not in a window) of 100 × 50 with the
	// fixed text system: a text wider than a line; then without word wrap;
	// SetMaxBytes(5) on "abcdefgh"; DisallowChar('x'); right alignment;
	// Autoindent.
	// Success criteria: The text wraps within the text rect (100 minus
	// insets of 3 each side, 94 wide) and PointAt() and OffsetAt() agree;
	// without wrapping it is one line and the text rect as wide as the
	// text; SetMaxBytes() cuts the text to 5 bytes and inserting beyond
	// does nothing; right aligned, the line ends at the text rect's right
	// edge.
	// Why it matters: Text fields depend on these settings (a maximum
	// length, forbidden characters, alignment).
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextView view({0, 0, 100, 50}, "view", {0, 0, 100, 50});

	// When
	view.SetText("aaaa bbbb cccc dddd");
	const int32 wrappedLines = view.CountLines();
	const gfx::Rect wrappedRect = view.TextRect();
	const gfx::Point point = view.PointAt(10);
	const int32 hit = view.OffsetAt({point.x + 1, point.y + 1});
	view.SetWordWrap(false);
	const int32 unwrappedLines = view.CountLines();
	const float unwrappedWidth = view.TextRect().Width();
	view.SetText("abcdefgh");
	view.SetMaxBytes(5);
	const std::string cut(view.Text());
	view.Insert("z");
	const std::string full(view.Text());
	view.SetWordWrap(true);
	view.SetAlignment(BHorizontalAlignment::Right);
	const gfx::Point end = view.PointAt(5);

	// Then
	EXPECT_EQ(wrappedRect, (gfx::Rect{3, 1, 97, wrappedRect.y1}));
	EXPECT_GT(wrappedLines, 1);
	EXPECT_EQ(hit, 10);
	EXPECT_EQ(unwrappedLines, 1);
	EXPECT_EQ(unwrappedWidth, 7 * 19);
	EXPECT_EQ(cut, "abcde");
	EXPECT_EQ(full, "abcde");
	EXPECT_EQ(end.x, 97);
	TextSystem::SetDefault(nullptr);
}


// The "text/plain" of the system clipboard.
std::string
ClipboardText()
{
	BClipboard& clipboard = BClipboard::System();
	std::string text;
	if (clipboard.Lock()) {
		if (auto data = clipboard.Data()->FindData("text/plain", BTypeCode::Mime))
			text.assign(reinterpret_cast<const char*>(data->data()), data->size());
		clipboard.Unlock();
	}
	return text;
}


TEST(TextViewTest, TheClipboardAndUndo)
{
	// What is tested: In a focused text view: "Hello world" typed, "world"
	// selected with Shift+Left and copied (Command+C), the caret moved to
	// the end, pasted (Command+V), undone and redone (kUndo); then all
	// selected and cut (Command+X), undone; then "abc" typed, undone and
	// redone.
	// Success criteria: Command+C puts "world" on the system clipboard as
	// "text/plain"; Command+V inserts it at the caret; undo restores the
	// text before the paste and the caret, redo pastes again. Cutting
	// empties the text and puts it on the clipboard; undo brings it back.
	// Typing is undone as one change and redone.
	// Why it matters: Cut, copy, paste and undo are what users expect of
	// every text field (Haiku's UndoBuffer).
	//
	// Given
	RunningText text;
	text.Type("Hello world");

	// When / Then
	for (int i = 0; i < 5; i++)
		text.Key("\x1c", BModifier::Shift);
	text.Key("c", BModifier::Command);
	EXPECT_EQ(text.Ask().end, 11);
	EXPECT_EQ(ClipboardText(), "world");
	text.Key("\x1d");
	text.Key("v", BModifier::Command);
	EXPECT_EQ(text.Ask().text, "Hello worldworld");
	text.Send(kUndo);
	const TextState undone = text.Ask();
	EXPECT_EQ(undone.text, "Hello world");
	EXPECT_EQ(undone.start, 11);
	text.Send(kUndo);
	EXPECT_EQ(text.Ask().text, "Hello worldworld");
	text.Key("a", BModifier::Command);
	text.Key("x", BModifier::Command);
	EXPECT_EQ(text.Ask().text, "");
	EXPECT_EQ(ClipboardText(), "Hello worldworld");
	text.Send(kUndo);
	EXPECT_EQ(text.Ask().text, "Hello worldworld");
	text.SetText("");
	text.Type("abc");
	text.Send(kUndo);
	EXPECT_EQ(text.Ask().text, "");
	text.Send(kUndo);
	EXPECT_EQ(text.Ask().text, "abc");
}


TEST(TextViewTest, InputMethodsComposeInline)
{
	// What is tested: After typing "a": an input method's composition
	// "ni", then "nih"; Backspace while composing; the composition
	// confirmed as "日本"; a new composition "x" stopped; a composition "y"
	// ended by the key event of its final text "Y".
	// Success criteria: The composed text stands at the caret in the text
	// and is replaced as it changes; the input method's Backspace leaves
	// the text alone; confirmed, it is typed (the caret after it); stopped,
	// the composition goes; a typed text replaces a composition.
	// Why it matters: Japanese, Chinese and Korean are typed this way, and
	// so are many composed characters (Haiku's B_INPUT_METHOD_EVENT).
	//
	// Given
	RunningText text;
	text.Type("a");

	// When / Then
	text.Compose("ni");
	EXPECT_EQ(text.Ask().text, "ani");
	text.Compose("nih");
	const TextState composing = text.Ask();
	EXPECT_EQ(composing.text, "anih");
	EXPECT_EQ(composing.start, 4);
	text.Key("\x08");
	EXPECT_EQ(text.Ask().text, "anih");
	text.Compose("\xE6\x97\xA5\xE6\x9C\xAC", true);
	const TextState confirmed = text.Ask();
	EXPECT_EQ(confirmed.text, "a\xE6\x97\xA5\xE6\x9C\xAC");
	EXPECT_EQ(confirmed.start, 7);
	text.Compose("x");
	EXPECT_EQ(text.Ask().text, "a\xE6\x97\xA5\xE6\x9C\xAC" "x");
	text.Compose("");
	EXPECT_EQ(text.Ask().text, "a\xE6\x97\xA5\xE6\x9C\xAC");
	text.Compose("y");
	text.Key("Y");
	EXPECT_EQ(text.Ask().text, "a\xE6\x97\xA5\xE6\x9C\xAC" "Y");
}


TEST(TextViewTest, NavigationKeysMoveAndSelect)
{
	// What is tested: In "one two three\nfour five\nsix\n" (lines of 14,
	// 10 and 4 bytes, then an empty one): Command+End, Command+Home; End,
	// Shift+Home; Down, Shift+Down; Option+Right twice, Option+Shift+Right,
	// Option+Left twice; Command+Right, Command+Left; Option+Backspace.
	// Success criteria: Command+End and Command+Home go to the end and
	// start of the text; End stops before the line break, Shift+Home
	// selects back to the line's start; Down keeps the column on the next
	// line, Shift+Down selects a line; Option+Right goes to the end of the
	// next word, with Shift it selects it; Option+Left first collapses the
	// selection to its start, then goes to the start of the word before;
	// Command+Right and Command+Left go by words as Option does (Haiku);
	// Option+Backspace removes the word before the caret.
	// Why it matters: These are the keys of every text field and editor,
	// as Haiku binds them (its navigation shortcuts included).
	//
	// Given
	RunningText text;
	text.SetText("one two three\nfour five\nsix\n");
	// asked after each key: a key that is a shortcut of the window reaches
	// the view as a message of its own, posted behind the keys already
	// waiting (as in Haiku)
	const auto caret = [&text] {
		const TextState state = text.Ask();
		return std::pair{state.start, state.end};
	};
	using Range = std::pair<int32, int32>;

	// When / Then
	text.Key("\x04", BModifier::Command);	// End
	EXPECT_EQ(caret(), (Range{28, 28}));
	text.Key("\x01", BModifier::Command);	// Home
	EXPECT_EQ(caret(), (Range{0, 0}));
	text.Key("\x04");
	EXPECT_EQ(caret(), (Range{13, 13}));
	text.Key("\x01", BModifier::Shift);
	EXPECT_EQ(caret(), (Range{0, 13}));
	text.Key("\x1d");
	text.Key("\x1f");						// Down from column 13 of line 0
	EXPECT_EQ(caret(), (Range{23, 23}));
	text.Key("\x1f", BModifier::Shift);
	EXPECT_EQ(caret(), (Range{23, 27}));
	text.Key("\x01", BModifier::Command);
	EXPECT_EQ(caret(), (Range{0, 0}));
	text.Key("\x1d", BModifier::Option);
	EXPECT_EQ(caret(), (Range{3, 3}));
	text.Key("\x1d", BModifier::Option);
	EXPECT_EQ(caret(), (Range{7, 7}));
	text.Key("\x1d", BModifier::Option | BModifier::Shift);
	EXPECT_EQ(caret(), (Range{7, 13}));
	text.Key("\x1c", BModifier::Option);
	EXPECT_EQ(caret(), (Range{7, 7}));
	text.Key("\x1c", BModifier::Option);
	EXPECT_EQ(caret(), (Range{4, 4}));
	text.Key("\x1d", BModifier::Command);
	EXPECT_EQ(caret(), (Range{7, 7}));
	text.Key("\x1c", BModifier::Command);
	EXPECT_EQ(caret(), (Range{4, 4}));
	text.Key("\x08", BModifier::Option);	// Backspace
	EXPECT_EQ(text.Ask().text, "two three\nfour five\nsix\n");
}


TEST(TextViewTest, PageKeysGoByTheViewsHeight)
{
	// What is tested: In a text of 30 lines in the view of 100 pixels
	// (lines of 14): Page Down, Page Down with Shift, Page Up; Page Down,
	// then Page Up with Shift twice.
	// Success criteria: Page Down moves the caret about a view's height
	// down (6 or 7 lines), Shift extends the selection by the same; Page Up
	// moves back up; with Shift up to the start, the selection reaches
	// from the start to where the caret was (its anchor).
	// Why it matters: Long texts are read and selected page by page.
	//
	// Given
	RunningText text;
	std::string lines;
	for (int i = 0; i < 30; i++)
		lines += "line " + std::to_string(i) + "\n";
	text.SetText(lines);
	const auto lineOf = [&](int32 offset) {
		return static_cast<int32>(std::count(lines.begin(), lines.begin() + offset, '\n'));
	};

	// When
	text.Key("\x0c");						// Page Down
	const TextState down = text.Ask();
	text.Key("\x0c", BModifier::Shift);
	const TextState selected = text.Ask();
	text.Key("\x0b");						// Page Up
	const TextState up = text.Ask();
	text.Key("\x0c");
	const TextState again = text.Ask();
	text.Key("\x0b", BModifier::Shift);
	text.Key("\x0b", BModifier::Shift);
	const TextState turned = text.Ask();

	// Then
	EXPECT_EQ(down.start, down.end);
	EXPECT_GE(lineOf(down.start), 6);
	EXPECT_LE(lineOf(down.start), 7);
	EXPECT_EQ(selected.start, down.start);
	EXPECT_EQ(lineOf(selected.end) - lineOf(selected.start), lineOf(down.start));
	EXPECT_EQ(up.start, up.end);
	EXPECT_LT(up.start, selected.end);
	EXPECT_EQ(turned.start, 0);
	EXPECT_EQ(turned.end, again.start);
}


TEST(TextViewTest, SizesForTheLayout)
{
	// What is tested: With the fixed text system: a text view of 200 × 100
	// (not in a window) with "aaaa bbbb cccc dddd": SetInsets() and
	// GetInsets(); SetTabWidth() with "a\tb"; not editable, the height for
	// a width of 50 and of 300; MinSize() and PreferredSize() wrapping and
	// not wrapping.
	// Success criteria: The insets come back and move the text rect; the
	// tab stops at the tab width; not editable it has a height for width:
	// more lines when narrower, each 14 pixels plus the insets; its
	// minimum is three lines wide and one high (with the insets), without
	// wrapping as wide as the text (NewGUI: SetWordWrap() invalidates the
	// layout; Haiku kept the old size).
	// Why it matters: Text views in layouts (labels of alerts, forms) get
	// their size from these methods (Haiku's _ValidateLayoutData()).
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextView view({0, 0, 200, 100}, "view", {0, 0, 200, 100});
	view.SetText("aaaa bbbb cccc dddd");

	// When
	view.SetInsets(5, 6, 7, 8);
	float left = 0;
	float top = 0;
	float right = 0;
	float bottom = 0;
	view.GetInsets(&left, &top, &right, &bottom);
	const gfx::Rect textRect = view.TextRect();
	view.SetText("a\tb");
	view.SetTabWidth(40);
	const float tabbed = view.PointAt(2).x - view.PointAt(0).x;
	view.SetText("aaaa bbbb cccc dddd");
	view.MakeEditable(false);
	const bool heightForWidth = view.HasHeightForWidth();
	float narrow = 0;
	float wide = 0;
	view.GetHeightForWidth(50, nullptr, nullptr, &narrow);
	view.GetHeightForWidth(300, nullptr, nullptr, &wide);
	const gfx::Size min = view.MinSize();
	view.SetWordWrap(false);
	const gfx::Size unwrapped = view.MinSize();
	const gfx::Size preferred = view.PreferredSize();

	// Then
	EXPECT_EQ(left, 5);
	EXPECT_EQ(top, 6);
	EXPECT_EQ(right, 7);
	EXPECT_EQ(bottom, 8);
	EXPECT_EQ(textRect.x0, 5);
	EXPECT_EQ(textRect.y0, 6);
	EXPECT_EQ(textRect.x1, 200 - 7);
	EXPECT_EQ(tabbed, 40);
	EXPECT_TRUE(heightForWidth);
	EXPECT_EQ(wide, 14 + 6 + 8);
	EXPECT_GT(narrow, wide);
	EXPECT_EQ(std::fmod(narrow - 14, 14), 0);
	EXPECT_EQ(min.w, 3 * 14 + 5 + 7);
	EXPECT_EQ(min.h, 14 + 6 + 8);
	EXPECT_EQ(unwrapped.w, 7 * 19 + 5 + 7);
	EXPECT_EQ(preferred.w, unwrapped.w);
	TextSystem::SetDefault(nullptr);
}


} // namespace
