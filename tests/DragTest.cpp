/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drag & drop inside the application (module P4): BView::DragMessage() as in
// Haiku, between views of a window and between windows.


#include <ngui/View.h>

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/test/Driver.h>

#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
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


// A view that starts a drag of kDragged with "text/plain" ("hello", MIME)
// and, if \a withFile, "paths" ("/tmp/a b.txt") when pressed.
class MimeSourceView : public BView {
public:
	MimeSourceView(gfx::Rect frame, bool& withData)
		:
		BView(frame, "mime source", kFollowLeftTop, BViewFlag::WillDraw),
		fWithData(withData)
	{
	}

	void MouseDown(gfx::Point /*where*/) override
	{
		BMessage message(kDragged);
		if (fWithData) {
			const std::string_view hello = "hello";
			(void)message.AddData("text/plain", BTypeCode::Mime, std::as_bytes(std::span(hello)));
			(void)message.AddString("paths", "/tmp/a b.txt");
		}
		DragMessage(message, gfx::Rect{0, 0, 10, 10});
	}

private:
	bool& fWithData;
};


std::string
Text(const std::vector<std::byte>& bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}


TEST(DragTest, ADragLeavesForOtherApplications)
{
	// What is tested: On a platform whose drags go on to other applications
	// (the headless one with Options::systemDrag), a drag of a message with
	// a "text/plain" field (MIME) and "paths" dragged out of the window,
	// over no window of the application; then one without such data.
	// Success criteria: The first drag goes to the system once, with
	// text/plain "hello" and the path as a text/uri-list
	// ("file:///tmp/a%20b.txt"), and the drag in the application ends (its
	// image is gone). The second stays in the application: nothing more
	// goes to the system.
	// Why it matters: Data dragged out of a window must reach other
	// programs (module P4); what other programs cannot read stays home.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-drag-out-test");
	ASSERT_TRUE(app.UsePlatform(std::make_unique<HeadlessPlatform>(
		HeadlessPlatform::Options{.systemDrag = true})).has_value());
	Driver driver(app);
	bool withData = true;
	auto owned = MakeWindow({100, 100, 300, 200}, "Out");
	owned->AddChild(std::make_unique<MimeSourceView>(gfx::Rect{10, 10, 60, 60}, withData));
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef ref = driver.Window("Out");

	// When
	ASSERT_TRUE(driver.Drag(ref, {20, 20}, {500, 500}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::vector<std::vector<DragItem>> first = ref.PlatformWindow()->SystemDrags();
	std::int32_t popups = -1;
	(void)ref.Find("mime source").Run([&](BView& view) {
		popups = view.Window()->CountPopups();
	});
	withData = false;
	ASSERT_TRUE(driver.Drag(ref, {20, 20}, {500, 500}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::size_t drags = ref.PlatformWindow()->SystemDrags().size();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_EQ(first.size(), 1u);
	ASSERT_EQ(first[0].size(), 2u);
	EXPECT_EQ(first[0][0].type, "text/plain");
	EXPECT_EQ(Text(first[0][0].data), "hello");
	EXPECT_EQ(first[0][1].type, "text/uri-list");
	EXPECT_EQ(Text(first[0][1].data), "file:///tmp/a%20b.txt\r\n");
	EXPECT_EQ(popups, 0);
	EXPECT_EQ(drags, 1u);
}


// A view that keeps the last kSimpleData or kMimeData dropped on it.
class DropView : public BView {
public:
	DropView(gfx::Rect frame, BMessage& dropped)
		:
		BView(frame, "drop", kFollowLeftTop, BViewFlag::WillDraw),
		fDropped(dropped)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSimpleData || message->what == kMimeData) {
			fDropped = *message;
			return;
		}
		BView::MessageReceived(message);
	}

private:
	BMessage& fDropped;
};


TEST(DragTest, DataFromOtherApplicationsIsDropped)
{
	// What is tested: Drops from another application with all their data
	// at once (kDropData): a PNG and two files as a text/uri-list (a Windows
	// path with a space, a Unix path), then a PNG alone; over a view, as
	// the platform reports them.
	// Success criteria: The first drop reaches the view as kSimpleData with
	// the decoded paths "C:/x y.txt" and "/home/u/z" and the image/png
	// field; the second as kMimeData with the image/png bytes; both
	// WasDropped().
	// Why it matters: Other programs drag images, links and files; a view
	// must get them by their MIME types (module P4), files as paths as
	// before.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-drop-data-test");
	Driver driver(app);
	BMessage withFiles;
	BMessage imageOnly;
	BMessage dropped;
	auto owned = MakeWindow({100, 100, 300, 200}, "In");
	owned->AddChild(std::make_unique<DropView>(gfx::Rect{0, 0, 200, 100}, dropped));
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	HeadlessWindow* platform = driver.Window("In").PlatformWindow();
	const std::array<std::byte, 4> png = {std::byte{0x89}, std::byte{'P'}, std::byte{'N'},
		std::byte{'G'}};
	const std::string_view uris = "file:///C:/x%20y.txt\r\nfile:///home/u/z\r\n";
	const auto drop = [&](bool files) {
		BMessage position(kDropPosition);
		(void)position.AddPoint("where", {50, 50});
		BMessage data(kDropData);
		(void)data.AddPoint("where", {50, 50});
		(void)data.AddData("image/png", BTypeCode::Mime, png);
		if (files)
			(void)data.AddData("text/uri-list", BTypeCode::Mime, std::as_bytes(std::span(uris)));
		BMessage complete(kDropComplete);
		(void)complete.AddPoint("where", {50, 50});
		for (const BMessage* event : {&position, &data, &complete})
			ASSERT_TRUE(platform->Send(*event).has_value());
		ASSERT_TRUE(platform->Send(BMessage(kDropBegin)).has_value());
	};

	// When
	ASSERT_TRUE(platform->Send(BMessage(kDropBegin)).has_value());
	drop(true);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	(void)driver.Window("In").Find("drop").Run([&](BView&) { withFiles = dropped; });
	drop(false);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	(void)driver.Window("In").Find("drop").Run([&](BView&) { imageOnly = dropped; });
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(withFiles.what, kSimpleData);
	EXPECT_TRUE(withFiles.WasDropped());
	EXPECT_EQ(withFiles.GetString("paths", ""), "C:/x y.txt");
	EXPECT_EQ(withFiles.FindString("paths", 1).value_or(""), "/home/u/z");
	EXPECT_TRUE(withFiles.HasData("image/png", BTypeCode::Mime));
	EXPECT_EQ(imageOnly.what, kMimeData);
	EXPECT_TRUE(imageOnly.WasDropped());
	const auto image = imageOnly.FindData("image/png", BTypeCode::Mime);
	ASSERT_TRUE(image.has_value());
	EXPECT_EQ(image->size(), 4u);
	EXPECT_FALSE(imageOnly.HasData("paths"));
}


} // namespace
