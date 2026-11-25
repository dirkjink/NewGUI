/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/TextView.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kUndoText = FourCharCode("UNDT");


// A view that records the drag transits it sees and the messages dropped on
// it, as text.
class DropTarget : public BView {
public:
	DropTarget(gfx::Rect frame, std::string_view name, std::vector<std::string>& log)
		:
		BView(frame, name, BResizingMode{}, BViewFlags{}),
		fLog(log)
	{
	}

	void MouseMoved(gfx::Point where, BMouseTransit transit, const BMessage* drag) override
	{
		if (drag == nullptr)
			return;
		const char* names[] = {"entered", "inside", "exited", "outside"};
		fLog.push_back(std::string(Name()) + " " + names[static_cast<int>(transit)] + " "
			+ std::to_string(static_cast<int>(where.x)) + ","
			+ std::to_string(static_cast<int>(where.y))
			+ (drag->what == kSimpleData ? " data" : " ?"));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->WasDropped() && message->what == kSimpleData) {
			std::string entry = std::string(Name()) + " dropped";
			for (std::int32_t i = 0; auto path = message->FindString("paths", i); i++)
				entry += " " + std::string(*path);
			const gfx::Point point = message->DropPoint();
			entry += " at " + std::to_string(static_cast<int>(point.x)) + ","
				+ std::to_string(static_cast<int>(point.y));
			fLog.push_back(entry);
			return;
		}
		BView::MessageReceived(message);
	}

private:
	std::vector<std::string>& fLog;
};


// A window at (100, 50) with "target" at (10, 10, 60, 60), "other" at (70,
// 10, 120, 60), an editable text view "editable" at (10, 80, 110, 120) with
// "Hello", and a read-only one "label" at (130, 80, 230, 120). Drops nobody
// takes are logged by the window. kAsk answers with the log and the texts;
// kUndoText undoes in the editable text view.
class DropWindow : public BWindow {
public:
	DropWindow()
		:
		BWindow({100, 50, 400, 250}, "drops")
	{
		AddChild(std::make_unique<DropTarget>(gfx::Rect{10, 10, 60, 60}, "target", fLog));
		AddChild(std::make_unique<DropTarget>(gfx::Rect{70, 10, 120, 60}, "other", fLog));
		fEditable = static_cast<BTextView*>(AddChild(std::make_unique<BTextView>(
			gfx::Rect{10, 80, 110, 120}, "editable", gfx::Rect{0, 0, 100, 40})));
		fEditable->SetText("Hello");
		auto label = std::make_unique<BTextView>(gfx::Rect{130, 80, 230, 120}, "label",
			gfx::Rect{0, 0, 100, 40});
		label->MakeEditable(false);
		fLabel = static_cast<BTextView*>(AddChild(std::move(label)));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->WasDropped()) {
			fLog.push_back(std::string("window dropped ")
				+ (message->what == kMimeData ? "text" : "data"));
			return;
		}
		if (message->what == kUndoText) {
			fEditable->Undo(nullptr);
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		BMessage reply(kAsk);
		for (const std::string& entry : fLog)
			(void)reply.AddString("log", entry);
		(void)reply.AddString("editable", std::string(fEditable->Text()));
		(void)reply.AddString("label", std::string(fLabel->Text()));
		(void)message->SendReply(reply);
	}

private:
	std::vector<std::string> fLog;
	BTextView* fEditable = nullptr;
	BTextView* fLabel = nullptr;
};


// What a DropWindow answers.
struct DropState {
	std::vector<std::string> log;
	std::string editable;
	std::string label;
};


// A running DropWindow with the fixed text system.
class RunningDrops {
public:
	RunningDrops()
	{
		TextSystem::SetDefault(&fText);
		fWindow = *BLooper::Run(std::make_unique<DropWindow>());
	}

	~RunningDrops()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		TextSystem::SetDefault(nullptr);
	}

	void Event(std::uint32_t what, gfx::Point where, std::string_view field = {},
		std::string_view value = {})
	{
		BMessage message(what);
		(void)message.AddPoint("where", where);
		if (!field.empty())
			(void)message.AddString(field, value);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(what).has_value());
	}

	DropState Ask()
	{
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
		DropState state;
		for (std::int32_t i = 0; auto entry = reply.FindString("log", i); i++)
			state.log.emplace_back(*entry);
		state.editable = reply.GetString("editable", "");
		state.label = reply.GetString("label", "");
		return state;
	}

private:
	test::FixedTextSystem fText;
	BMessenger fWindow;
};


TEST(DropTest, FilesGoToTheViewUnderTheDropPoint)
{
	// What is tested: A drag from another application over a window at
	// (100, 50): begin, positions over "target" (twice) and "other", back
	// to "target", two files, complete at (30, 40); then a second drag
	// dropped where no view is, and a third that leaves without a drop.
	// Success criteria: The views get MouseMoved() with a kSimpleData drag
	// message (entered, inside, exited, in the view's coordinates). The
	// drop goes to the view under the drop point as kSimpleData with both
	// paths in "paths", WasDropped(), and the drop point on the screen
	// (130, 90); the view under it then sees the drag exit. A drop on no
	// view reaches the window. A drag that leaves delivers nothing.
	// Why it matters: Files dragged from the file manager reach the view
	// they are dropped on, as in Haiku (B_SIMPLE_DATA; paths instead of
	// entry_refs).
	//
	// Given
	RunningDrops drops;

	// When
	drops.Event(kDropBegin, {0, 0});
	drops.Event(kDropPosition, {20, 20});
	drops.Event(kDropPosition, {25, 20});
	drops.Event(kDropPosition, {80, 20});
	drops.Event(kDropPosition, {30, 40});
	drops.Event(kDropFile, {30, 40}, "path", "/home/a.txt");
	drops.Event(kDropFile, {30, 40}, "path", "/home/b c.png");
	drops.Event(kDropComplete, {30, 40});
	drops.Event(kDropBegin, {0, 0});
	drops.Event(kDropFile, {250, 20}, "path", "/home/a.txt");
	drops.Event(kDropComplete, {250, 20});
	drops.Event(kDropBegin, {0, 0});
	drops.Event(kDropPosition, {20, 20});
	drops.Event(kDropComplete, {20, 20});
	const DropState state = drops.Ask();

	// Then
	EXPECT_EQ(state.log, (std::vector<std::string>{"target entered 10,10 data",
		"target inside 15,10 data", "target exited 70,10 data", "other entered 10,10 data",
		"other exited -40,30 data", "target entered 20,30 data", "target exited 20,30 data",
		"target dropped /home/a.txt /home/b c.png at 130,90", "window dropped data",
		"target entered 10,10 data", "target exited 10,10 data"}));
}


TEST(DropTest, TextIsDroppedIntoEditableTextViews)
{
	// What is tested: Text " world" dragged from another application and
	// dropped at the end of "Hello" in an editable text view; then undone;
	// the same text dropped on a read-only text view.
	// Success criteria: The editable text view inserts the text where it is
	// dropped (BTextView::AcceptsDrop(): editable and "text/plain"), and
	// Undo() takes it out again (Haiku's DropUndoBuffer). The read-only one
	// does not take it; the drop goes on to its parent, the window, as
	// kMimeData.
	// Why it matters: Text dragged from a browser or an editor lands in text
	// fields, as in Haiku.
	//
	// Given
	RunningDrops drops;

	// When
	drops.Event(kDropBegin, {0, 0});
	drops.Event(kDropText, {108, 85}, "text", " world");
	drops.Event(kDropComplete, {108, 85});
	const DropState dropped = drops.Ask();
	drops.Send(kUndoText);
	const DropState undone = drops.Ask();
	drops.Event(kDropBegin, {0, 0});
	drops.Event(kDropText, {150, 85}, "text", " world");
	drops.Event(kDropComplete, {150, 85});
	const DropState refused = drops.Ask();

	// Then
	EXPECT_EQ(dropped.editable, "Hello world");
	EXPECT_EQ(undone.editable, "Hello");
	EXPECT_EQ(refused.label, "");
	EXPECT_EQ(refused.log, (std::vector<std::string>{"window dropped text"}));
}


} // namespace
