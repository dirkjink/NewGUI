/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drag & drop inside the application (module P4): BView::DragMessage() as in
// Haiku, between views of a window and between windows.


#include <ngui/View.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>

#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kDragged = 0x64726167;	// 'drag'
constexpr std::uint32_t kAck = 0x61636b21;		// 'ack!'


// What the views of the test saw, from their windows' threads.
struct Seen {
	std::mutex				lock;
	std::vector<std::string> moves;		// "<view> <transit> <what>"
	std::string				droppedOn;
	std::string				text;
	bool					wasDropped = false;
	gfx::Point				dropPoint;
	gfx::Point				dropOffset;
	int						popupsWhileDragging = 0;
	std::atomic<int>		acks = 0;
};


const char*
TransitName(BMouseTransit transit)
{
	switch (transit) {
		case BMouseTransit::Entered:
			return "entered";
		case BMouseTransit::Inside:
			return "inside";
		case BMouseTransit::Exited:
			return "exited";
		default:
			return "outside";
	}
}


// A view of 100 × 60 that starts a drag of kDragged ("text": its name) of
// its top left 20 × 20 when pressed, and hears the replies.
class SourceView : public BView {
public:
	SourceView(gfx::Rect frame, std::string name, Seen& seen)
		:
		BView(frame, name, kFollowLeftTop, BViewFlag::WillDraw),
		fSeen(seen)
	{
		SetViewColor(gfx::Color{0.2f, 0.4f, 0.8f, 1});
	}

	void MouseDown(gfx::Point /*where*/) override
	{
		BMessage message(kDragged);
		(void)message.AddString("text", Name());
		DragMessage(message, gfx::Rect{0, 0, 20, 20});
		std::scoped_lock lock(fSeen.lock);
		fSeen.popupsWhileDragging = Window()->CountPopups();
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kAck) {
			fSeen.acks++;
			return;
		}
		BView::MessageReceived(message);
	}

private:
	Seen& fSeen;
};


// A view that notes the drag passing and takes the drop, answering it.
class TargetView : public BView {
public:
	TargetView(gfx::Rect frame, std::string name, Seen& seen)
		:
		BView(frame, name, kFollowLeftTop, BViewFlag::WillDraw),
		fSeen(seen)
	{
	}

	void MouseMoved(gfx::Point /*where*/, BMouseTransit transit,
		const BMessage* dragMessage) override
	{
		if (dragMessage == nullptr)
			return;
		std::scoped_lock lock(fSeen.lock);
		fSeen.moves.push_back(std::string(Name()) + " " + TransitName(transit) + " "
			+ (dragMessage->what == kDragged ? "drag" : "other"));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what != kDragged) {
			BView::MessageReceived(message);
			return;
		}
		{
			std::scoped_lock lock(fSeen.lock);
			fSeen.droppedOn = Name();
			fSeen.text = message->GetString("text", "");
			fSeen.wasDropped = message->WasDropped();
			fSeen.dropPoint = message->DropPoint(&fSeen.dropOffset);
		}
		(void)message->SendReply(kAck);
	}

private:
	Seen& fSeen;
};


// A window titled \a title at \a frame on the pixel backend.
std::unique_ptr<BWindow>
MakeWindow(gfx::Rect frame, const char* title)
{
	auto window = std::make_unique<BWindow>(frame, title, BWindowLook::Titled);
	(void)window->UseBackend(std::make_unique<PixelBackend>());
	return window;
}


TEST(DragTest, AViewDragsAMessageToAnotherView)
{
	// What is tested: A window at (100, 100) with a source view at (10, 10)
	// and a target view at (150, 10); the mouse pressed at (15, 18) in the
	// source (which starts a drag of its top left 20 × 20 with DragMessage())
	// and dragged to (170, 30), released there.
	// Success criteria: While it is dragged the window has a popup (the
	// image). The target gets MouseMoved() with the message, entering and
	// inside, then exited; the drop goes to the target with its "text",
	// WasDropped(), DropPoint() on the screen (270, 130) and the offset
	// (15, 18) of the mouse in the image; its reply reaches the source view;
	// the image is gone afterwards.
	// Why it matters: Haiku's programs move data between views by dragging
	// a message (BView::DragMessage()): lists, file panels, colour wells.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-drag-test");
	Driver driver(app);
	Seen seen;
	auto owned = MakeWindow({100, 100, 400, 250}, "Drag");
	owned->AddChild(std::make_unique<SourceView>(gfx::Rect{10, 10, 110, 70}, "source", seen));
	owned->AddChild(std::make_unique<TargetView>(gfx::Rect{150, 10, 250, 70}, "target", seen));
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// When
	ASSERT_TRUE(driver.Drag(driver.Window("Drag"), {25, 28}, {170, 30}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	std::int32_t popupsAfter = -1;
	(void)driver.Window("Drag").Find("source").Run([&](BView& view) {
		popupsAfter = view.Window()->CountPopups();
	});
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(seen.popupsWhileDragging, 1);
	EXPECT_EQ(popupsAfter, 0);
	ASSERT_FALSE(seen.moves.empty());
	EXPECT_EQ(seen.moves.front(), "target entered drag");
	EXPECT_EQ(seen.moves.back(), "target exited drag");
	EXPECT_EQ(seen.droppedOn, "target");
	EXPECT_EQ(seen.text, "source");
	EXPECT_TRUE(seen.wasDropped);
	EXPECT_EQ(seen.dropPoint, (gfx::Point{270, 130}));
	EXPECT_EQ(seen.dropOffset, (gfx::Point{15, 18}));
	EXPECT_EQ(seen.acks, 1);
}


TEST(DragTest, ADragCrossesIntoAnotherWindow)
{
	// What is tested: Two windows of the application side by side, the
	// source window at (100, 100) and the target window at (450, 100) with a
	// target view at (20, 20); a drag from the source view to the screen
	// point (490, 150), which lies in the target window (the source window
	// gets all the mouse events, as with a button held down); then a second
	// drag released outside every window.
	// Success criteria: The target view in the other window gets the
	// MouseMoved() of the drag and the drop with "text" and the drop point
	// (490, 150); the reply reaches the source; the drag released outside
	// is dropped nowhere and gets no reply.
	// Why it matters: Each window runs on its own thread; a drag must still
	// go from one of the application's windows to another, as in Haiku.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-drag-windows-test");
	Driver driver(app);
	Seen seen;
	auto first = MakeWindow({100, 100, 400, 250}, "From");
	first->AddChild(std::make_unique<SourceView>(gfx::Rect{10, 10, 110, 70}, "source", seen));
	first->Show();
	auto second = MakeWindow({450, 100, 650, 250}, "To");
	second->AddChild(std::make_unique<TargetView>(gfx::Rect{20, 20, 120, 100}, "far", seen));
	second->Show();
	const BMessenger from = *BLooper::Run(std::move(first));
	const BMessenger to = *BLooper::Run(std::move(second));
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// When
	ASSERT_TRUE(driver.Drag(driver.Window("From"), {20, 20}, {390, 50}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	std::string droppedOn;
	gfx::Point dropPoint;
	std::vector<std::string> moves;
	{
		std::scoped_lock lock(seen.lock);
		droppedOn = seen.droppedOn;
		dropPoint = seen.dropPoint;
		moves = seen.moves;
		seen.droppedOn.clear();
	}
	const int acksAfterFirst = seen.acks;
	ASSERT_TRUE(driver.Drag(driver.Window("From"), {20, 20}, {320, 400}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	for (const BMessenger& window : {from, to}) {
		ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
		ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	}
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(droppedOn, "far");
	EXPECT_EQ(dropPoint, (gfx::Point{490, 150}));
	ASSERT_FALSE(moves.empty());
	EXPECT_EQ(moves.front(), "far entered drag");
	EXPECT_EQ(acksAfterFirst, 1);
	EXPECT_TRUE(seen.droppedOn.empty());
	EXPECT_EQ(seen.acks, 1);
}


} // namespace
