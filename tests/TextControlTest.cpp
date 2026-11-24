/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextControl.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");


TEST(TextControlTest, TheFieldSitsRightOfTheDivider)
{
	// What is tested: With the fixed text system (7 px per byte, lines of
	// 14): a text control "Name:" with "abc" at a frame of 200 × 30; one
	// set to "a\nb"; a grid with two text controls added by
	// AddTextControl(), "Name:" and "Address:", laid out in a window of
	// 300 × 100.
	// Success criteria: The control takes the height it needs: the text
	// view's minimum (a line plus 2) plus 2 pixels of frame on each side,
	// 20; the divider is half the width (Haiku's 99) and the text view
	// sits 2 pixels inside the rest. The control is not navigable, its
	// text view is. A line break becomes a space. In the grid both fields
	// begin at the same x, right of the wider label.
	// Why it matters: The text control is the one-line field of forms; its
	// geometry follows Haiku's so that forms look the same.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BTextControl control({0, 0, 200, 30}, "name", "Name:", "abc", nullptr);
	BTextControl lines({0, 0, 200, 30}, "lines", "", "a\nb", nullptr);
	BWindow window({0, 0, 300, 100}, "form");
	auto name = std::make_unique<BTextControl>("name", "Name:", "", nullptr);
	auto address = std::make_unique<BTextControl>("address", "Address:", "", nullptr);
	BTextControl* first = name.get();
	BTextControl* second = address.get();

	// When
	BLayoutBuilder::Grid<>(&window, 5, 5)
		.AddTextControl(std::move(name), 0, 0)
		.AddTextControl(std::move(address), 0, 1);
	window.Layout(false);
	const auto fieldLeft = [](BTextControl* textControl) {
		return textControl->Frame().x0 + textControl->TextView()->Frame().x0;
	};

	// Then
	EXPECT_EQ(control.Bounds().Height(), 20);
	EXPECT_EQ(control.Divider(), 99);
	EXPECT_EQ(control.TextView()->Frame(), (gfx::Rect{101, 2, 198, 18}));
	EXPECT_EQ(control.Text(), "abc");
	EXPECT_FALSE(control.Flags().Has(BViewFlag::Navigable));
	EXPECT_TRUE(control.TextView()->Flags().Has(BViewFlag::Navigable));
	EXPECT_EQ(lines.Text(), "a b");
	EXPECT_EQ(fieldLeft(first), fieldLeft(second));
	EXPECT_EQ(fieldLeft(second), second->Frame().x0 + second->Divider() + 2);
	EXPECT_EQ(second->Divider(), 7 * 8 + 6 + 1 + 5);
	TextSystem::SetDefault(nullptr);
}


// A window with two text controls, "first" (message 1, modification
// message 2) and "second"; the first's field has the focus. It records the
// messages it gets and answers kAsk with them, the first's text and the
// name of the control whose field has the focus.
class FormWindow : public BWindow {
public:
	FormWindow()
		:
		BWindow({0, 0, 300, 200}, "form")
	{
		auto first = std::make_unique<BTextControl>(gfx::Rect{10, 10, 210, 30}, "first",
			"First:", "", std::make_unique<BMessage>(1));
		first->SetModificationMessage(std::make_unique<BMessage>(2));
		fFirst = static_cast<BTextControl*>(AddChild(std::move(first)));
		AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 50, 210, 70}, "second",
			"Second:", "", nullptr));
		fFirst->MakeFocus(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == 1 || message->what == 2) {
			fReceived.push_back(static_cast<std::int32_t>(message->what));
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (std::int32_t what : fReceived)
			(void)reply.AddInt32("received", what);
		(void)reply.AddString("text", fFirst->Text());
		BView* focus = CurrentFocus();
		(void)reply.AddString("focus", focus != nullptr && focus->Parent() != nullptr
			? focus->Parent()->Name() : "");
		(void)message->SendReply(reply);
	}

private:
	BTextControl* fFirst = nullptr;
	std::vector<std::int32_t> fReceived;
};


TEST(TextControlTest, EnterAndLeavingSendTheMessage)
{
	// What is tested: Typing "ab" into the focused first field; Enter;
	// Enter again; typing "c"; Tab; Tab back (Shift+Tab) and Tab again
	// without a change.
	// Success criteria: Every typed character sends the modification
	// message (2); Enter sends the control's message (1) and selects the
	// text, a second Enter without a change sends nothing; Tab moves the
	// focus to the second control and, as the text changed since Enter,
	// sends the message once more; leaving without a change sends nothing.
	// Why it matters: Applications take the value of a text field when it
	// is confirmed or left, as in Haiku.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	BMessenger window = *BLooper::Run(std::make_unique<FormWindow>());
	BMessage activated(kWindowActivated);
	(void)activated.AddBool("active", true);
	(void)window.SendMessage(activated);
	const auto key = [&](std::string_view bytes, BModifierFlags modifiers = {}) {
		BMessage message(kKeyDown);
		(void)message.AddString("bytes", bytes);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		(void)window.SendMessage(message);
	};
	const auto ask = [&] {
		BMessage reply;
		(void)window.SendMessage(BMessage(kAsk), &reply);
		(void)window.SendMessage(BMessage(kAsk), &reply);
		return reply;
	};
	const auto received = [](const BMessage& reply) {
		std::vector<std::int32_t> values;
		for (std::int32_t i = 0; auto value = reply.FindInt32("received", i); i++)
			values.push_back(*value);
		return values;
	};

	// When
	key("a");
	key("b");
	const BMessage typed = ask();
	key("\n");
	key("\n");
	const BMessage entered = ask();
	key("c");
	key("\t");
	const BMessage tabbed = ask();
	key("\t", BModifier::Shift);
	key("\t");
	const BMessage left = ask();

	// Then
	EXPECT_EQ(received(typed), (std::vector<std::int32_t>{2, 2}));
	EXPECT_EQ(typed.GetString("text", "?"), "ab");
	EXPECT_EQ(received(entered), (std::vector<std::int32_t>{2, 2, 1}));
	EXPECT_EQ(received(tabbed), (std::vector<std::int32_t>{2, 2, 1, 2, 2, 1}));
	EXPECT_EQ(tabbed.GetString("text", "?"), "c");
	EXPECT_EQ(tabbed.GetString("focus", "?"), "second");
	EXPECT_EQ(received(left), received(tabbed));
	EXPECT_EQ(left.GetString("focus", "?"), "second");

	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
}


} // namespace
