/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextView.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
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


} // namespace
