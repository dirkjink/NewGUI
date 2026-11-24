/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Alert.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Button.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Invoker.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/TextView.h>
#include <ngui/View.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kAnswered = FourCharCode("ANSW");


// An alert that answers kAsk with its size, its buttons' frames (in the
// window), which one is the default and its text view's state.
class AskableAlert : public BAlert {
public:
	using BAlert::BAlert;

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kAsk) {
			BAlert::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		(void)reply.AddRect("frame", Frame());
		for (int32 i = 0; i < CountButtons(); i++) {
			BButton* button = ButtonAt(i);
			const gfx::Point origin = button->ConvertToWindow(gfx::Point{0, 0});
			(void)reply.AddRect("button", {origin.x, origin.y,
				origin.x + button->Bounds().Width(), origin.y + button->Bounds().Height()});
			if (DefaultButton() == button)
				(void)reply.AddInt32("default", i);
		}
		(void)reply.AddBool("editable", TextView()->IsEditable());
		(void)reply.AddString("text", TextView()->Text());
		(void)message->SendReply(reply);
	}
};


// What an AskableAlert answers.
struct AlertState {
	gfx::Rect frame;
	std::vector<gfx::Rect> buttons;
	int32 defaultButton = -1;
	bool editable = true;
	std::string text;
};


AlertState
Ask(const BMessenger& alert)
{
	BMessage reply;
	(void)alert.SendMessage(BMessage(kAsk), &reply);
	AlertState state;
	state.frame = reply.GetRect("frame", {});
	for (std::int32_t i = 0; auto button = reply.FindRect("button", i); i++)
		state.buttons.push_back(*button);
	state.defaultButton = reply.GetInt32("default", -1);
	state.editable = reply.GetBool("editable", true);
	state.text = reply.GetString("text", "");
	return state;
}


void
Key(const BMessenger& window, std::string_view bytes)
{
	BMessage message(kKeyDown);
	(void)message.AddString("bytes", bytes);
	(void)window.SendMessage(message);
}


TEST(AlertTest, ButtonsSitAtTheBottomRightAsInHaiku)
{
	// What is tested: With the fixed text system (plain font 12, 7 px per
	// byte): alerts with "Cancel" and "OK", shown by Go(): with buttons as
	// usual, as wide as the widest, and with offset spacing; an alert
	// without buttons.
	// Success criteria: As usual, a button is at least 55 × 12 / 11
	// pixels wide; from the widest, both are equally wide; with offset
	// spacing the first is at least 62 × 12 / 11 pixels apart from the
	// second. The buttons share a line, the last is the default button,
	// and the window is at least 310 (offset: 335) × 12 / 11 pixels wide.
	// The text is shown, not editable. Without buttons Go() fails. Of the
	// three modal alerts, the one shown last takes the keys (Enter) first.
	// Why it matters: Alerts look and behave as Haiku's, whose
	// applications are ported.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	const float factor = 12.0f / 11.0f;
	const auto go = [](BButtonWidth width, BButtonSpacing spacing) {
		auto alert = std::make_unique<AskableAlert>("Title", "Something happened.", "Cancel",
			"OK", "", width, spacing, BAlertType::Warning);
		return *BAlert::Go(std::move(alert), [](int32) {});
	};

	// When
	const BMessenger usual = go(BButtonWidth::AsUsual, BButtonSpacing::Even);
	const AlertState usualState = Ask(usual);
	const BMessenger widest = go(BButtonWidth::FromWidest, BButtonSpacing::Even);
	const AlertState widestState = Ask(widest);
	const BMessenger offset = go(BButtonWidth::FromLabel, BButtonSpacing::Offset);
	const AlertState offsetState = Ask(offset);
	const auto empty = BAlert::Go(std::make_unique<BAlert>(), [](int32) {});

	// Then
	ASSERT_EQ(usualState.buttons.size(), 2u);
	EXPECT_GE(usualState.buttons[1].Width(), 55 * factor);
	// on one line (the default button is bigger, as in Haiku)
	EXPECT_EQ(usualState.buttons[0].y0 + usualState.buttons[0].Height() / 2,
		usualState.buttons[1].y0 + usualState.buttons[1].Height() / 2);
	EXPECT_LT(usualState.buttons[0].x1, usualState.buttons[1].x0);
	EXPECT_GT(usualState.buttons[1].x1, usualState.frame.Width() - 20);
	EXPECT_GT(usualState.buttons[1].y1, usualState.frame.Height() - 20);
	EXPECT_EQ(usualState.defaultButton, 1);
	EXPECT_GE(usualState.frame.Width(), 310 * factor);
	EXPECT_EQ(usualState.text, "Something happened.");
	EXPECT_FALSE(usualState.editable);
	ASSERT_EQ(widestState.buttons.size(), 2u);
	EXPECT_EQ(widestState.buttons[0].Width(), widestState.buttons[1].Width());
	ASSERT_EQ(offsetState.buttons.size(), 2u);
	EXPECT_GE(offsetState.buttons[1].x0 - offsetState.buttons[0].x1, 62 * factor);
	EXPECT_GE(offsetState.frame.Width(), 335 * factor);
	ASSERT_FALSE(empty.has_value());
	EXPECT_EQ(empty.error().code, ErrorCode::InvalidArgument);

	// the alert shown last has the input: closed in the other order
	for (const BMessenger& alert : {offset, widest, usual}) {
		Key(alert, "\n");
		(void)alert.WaitForLooperQuit(5s);
	}
	TextSystem::SetDefault(nullptr);
}


// A looper that keeps the "which" of the kAnswered messages it gets.
class AnswerLooper : public BLooper {
public:
	explicit AnswerLooper(std::shared_ptr<std::vector<int32>> answers)
		:
		BLooper("answers"),
		fAnswers(std::move(answers))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kAnswered) {
			fAnswers->push_back(message->GetInt32("which", -1));
			return;
		}
		if (message->what == kAsk) {
			BMessage reply(kAsk);
			for (int32 answer : *fAnswers)
				(void)reply.AddInt32("which", answer);
			(void)message->SendReply(reply);
			return;
		}
		BLooper::MessageReceived(message);
	}

private:
	std::shared_ptr<std::vector<int32>> fAnswers;
};


TEST(AlertTest, AButtonAnswersAndCloses)
{
	// What is tested: An alert "Cancel", "OK" whose Cancel has Escape as
	// shortcut, shown with an invoker to a looper: Escape pressed. A second
	// one: Enter pressed. A third, shown with a callback: its "OK" clicked
	// (the button invoked by its key, Space, as the shortcut filter does)
	// twice before it quits.
	// Success criteria: Escape presses Cancel: the invoker's message
	// arrives with "which" 0 and the alert quits. Enter presses the default
	// button, OK: "which" 1. The callback gets 1, once.
	// Why it matters: Go() is asynchronous (NewGUI's decision): the answer
	// arrives as a message or through a callback, never by blocking.
	//
	// Given
	test::FixedTextSystem fixed;
	TextSystem::SetDefault(&fixed);
	auto answers = std::make_shared<std::vector<int32>>();
	const BMessenger looper = *BLooper::Run(std::make_unique<AnswerLooper>(answers));
	const auto alert = [] {
		auto made = std::make_unique<BAlert>("Title", "Sure?", "Cancel", "OK");
		made->SetShortcut(0, keys::kEscape);
		return made;
	};
	std::mutex lock;
	std::vector<int32> called;

	// When
	const BMessenger first = *BAlert::Go(alert(), std::make_unique<BInvoker>(
		std::make_unique<BMessage>(kAnswered), looper));
	Key(first, std::string(1, keys::kEscape));
	const bool firstQuit = first.WaitForLooperQuit(5s).has_value();
	const BMessenger second = *BAlert::Go(alert(), std::make_unique<BInvoker>(
		std::make_unique<BMessage>(kAnswered), looper));
	Key(second, "\n");
	const bool secondQuit = second.WaitForLooperQuit(5s).has_value();
	const BMessenger third = *BAlert::Go(alert(), [&](int32 which) {
		std::scoped_lock guard(lock);
		called.push_back(which);
	});
	Key(third, "\n");
	Key(third, "\n");
	const bool thirdQuit = third.WaitForLooperQuit(5s).has_value();
	BMessage reply;
	(void)looper.SendMessage(BMessage(kAsk), &reply);

	// Then
	EXPECT_TRUE(firstQuit);
	EXPECT_TRUE(secondQuit);
	EXPECT_TRUE(thirdQuit);
	EXPECT_EQ(reply.GetInt32("which", -1), 0);
	auto secondWhich = reply.FindInt32("which", 1);
	ASSERT_TRUE(secondWhich.has_value());
	EXPECT_EQ(*secondWhich, 1);
	{
		std::scoped_lock guard(lock);
		EXPECT_EQ(called, (std::vector<int32>{1}));
	}

	(void)looper.SendMessage(kQuitRequested);
	(void)looper.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
}


// A view that counts the mouse buttons pressed on it.
class ClickView : public BView {
public:
	explicit ClickView(std::shared_ptr<std::atomic<int>> clicks)
		:
		BView({0, 0, 200, 100}, "clicks", kFollowAll, {}),
		fClicks(std::move(clicks))
	{
	}

	void MouseDown(gfx::Point) override
	{
		(*fClicks)++;
	}

private:
	std::shared_ptr<std::atomic<int>> fClicks;
};


// A modal window that shows and hides itself on kShowModal and
// kHideModal, on its own thread.
constexpr std::uint32_t kShowModal = FourCharCode("SHOW");
constexpr std::uint32_t kHideModal = FourCharCode("HIDE");


class ModalWindow : public BWindow {
public:
	ModalWindow()
		:
		BWindow({0, 0, 50, 50}, "modal", BWindowLook::Bordered, BWindowFeel::ModalApp)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kShowModal)
			Show();
		else if (message->what == kHideModal)
			Hide();
		else {
			BWindow::MessageReceived(message);
			return;
		}
		(void)message->SendReply(BMessage(kReply));
	}
};


TEST(AlertTest, AModalWindowTakesTheInputOfTheOthers)
{
	// What is tested: A running window with a view that counts clicks; a
	// click; a window with BWindowFeel::ModalApp shown, a click on the
	// first; the modal window hidden, a click.
	// Success criteria: While the modal window is shown, the other window
	// gets no click; before and after, it does.
	// Why it matters: An alert must be answered before the application
	// goes on (Haiku's B_MODAL_APP_WINDOW_FEEL).
	//
	// Given
	auto clicks = std::make_shared<std::atomic<int>>(0);
	auto owned = std::make_unique<BWindow>(gfx::Rect{0, 0, 200, 100}, "normal");
	owned->AddChild(std::make_unique<ClickView>(clicks));
	const BMessenger normal = *BLooper::Run(std::move(owned));
	const BMessenger modal = *BLooper::Run(std::make_unique<ModalWindow>());
	const auto click = [&] {
		for (std::uint32_t what : {kMouseDown, kMouseUp}) {
			BMessage message(what);
			(void)message.AddPoint("where", {10, 10});
			(void)message.AddInt32("buttons", what == kMouseDown ? 1 : 0);
			(void)normal.SendMessage(message);
		}
		// a round trip, so that the clicks are dispatched
		BMessage reply;
		(void)normal.SendMessage(BMessage(kAsk), &reply);
	};
	const auto tell = [&](std::uint32_t what) {
		BMessage reply;
		(void)modal.SendMessage(BMessage(what), &reply);
	};

	// When
	click();
	const int before = *clicks;
	tell(kShowModal);
	click();
	const int whileModal = *clicks;
	tell(kHideModal);
	click();
	const int after = *clicks;

	// Then
	EXPECT_EQ(before, 1);
	EXPECT_EQ(whileModal, 1);
	EXPECT_EQ(after, 2);

	(void)modal.SendMessage(kQuitRequested);
	(void)modal.WaitForLooperQuit(5s);
	(void)normal.SendMessage(kQuitRequested);
	(void)normal.WaitForLooperQuit(5s);
}


} // namespace
