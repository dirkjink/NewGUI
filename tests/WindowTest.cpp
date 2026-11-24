/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <array>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Dump.h>

#include "support/FakePlatform.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"
#include "support/RecordingLooper.h"
#include "support/StackLayout.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kAsk = FourCharCode("ASK_");
constexpr std::uint32_t kInvalidate = FourCharCode("INVL");
constexpr std::uint32_t kResizeToPreferred = FourCharCode("RTPF");
constexpr std::uint32_t kResizeTo = FourCharCode("RSZT");


std::string
Name(BMouseTransit transit)
{
	switch (transit) {
		case BMouseTransit::Entered:
			return "entered";
		case BMouseTransit::Inside:
			return "inside";
		case BMouseTransit::Exited:
			return "exited";
		case BMouseTransit::Outside:
			return "outside";
	}
	return "?";
}


std::string
At(gfx::Point point)
{
	return std::to_string(static_cast<int>(point.x)) + ","
		+ std::to_string(static_cast<int>(point.y));
}


// A view that records its events and draws a counter of its Draw() calls.
class EventView : public BView {
public:
	EventView(gfx::Rect frame, std::string_view name, std::shared_ptr<LooperRecord> record,
		BResizingMode mode = kFollowLeftTop)
		:
		BView(frame, name, mode, BViewFlag::WillDraw | BViewFlag::FrameEvents),
		fRecord(std::move(record))
	{
	}

	void Draw(DrawContext&) override
	{
		draws++;
	}

	void AttachedToWindow() override
	{
		fRecord->Add(Name() + " attached");
	}

	void MouseDown(gfx::Point where) override
	{
		fRecord->Add(Name() + " down " + At(where));
		if (trackMouse) {
			SetMouseEventMask(BEventMask::Pointer);
			MakeFocus();
		}
	}

	void MouseUp(gfx::Point where) override
	{
		fRecord->Add(Name() + " up " + At(where));
	}

	void MouseMoved(gfx::Point where, BMouseTransit transit, const BMessage*) override
	{
		fRecord->Add(Name() + " " + ::Name(transit) + " " + At(where));
	}

	void KeyDown(std::string_view bytes) override
	{
		fRecord->Add(Name() + " key " + std::string(bytes));
	}

	void WindowActivated(bool active) override
	{
		fRecord->Add(Name() + (active ? " active" : " inactive"));
	}

	void FrameResized(float width, float height) override
	{
		fRecord->Add(Name() + " resized " + std::to_string(static_cast<int>(width)) + "x"
			+ std::to_string(static_cast<int>(height)));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kInvalidate) {
			// Three invalidations in one message make one frame.
			Invalidate();
			Invalidate();
			Invalidate();
			return;
		}
		BView::MessageReceived(message);
	}

	bool trackMouse = false;
	int draws = 0;

private:
	std::shared_ptr<LooperRecord> fRecord;
};


// A window that answers kAsk with what it drew, for the test thread.
class TestWindow : public BWindow {
public:
	TestWindow(gfx::Rect frame, BWindowFlags flags = {})
		:
		BWindow(frame, "test", BWindowLook::Titled, flags)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kResizeToPreferred) {
			ResizeToPreferred();
			return;
		}
		if (message->what == kResizeTo) {
			ResizeTo(message->GetFloat("width", 0), message->GetFloat("height", 0));
			return;
		}
		if (message->what != kAsk) {
			BWindow::MessageReceived(message);
			return;
		}
		// A frame that is due is drawn first, so that the answer shows
		// the state after everything sent before.
		UpdateIfNeeded();
		BMessage reply(kReply);
		(void)reply.AddInt64("frames", static_cast<std::int64_t>(FrameCount()));
		(void)reply.AddString("focus", CurrentFocus() != nullptr ? CurrentFocus()->Name() : "");
		(void)reply.AddRect("bounds", Bounds());
		for (std::int32_t i = 0; i < CountChildren(); i++)
			(void)reply.AddRect("frame", ChildAt(i)->Frame());
		float minWidth;
		float maxWidth;
		float minHeight;
		float maxHeight;
		GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
		(void)reply.AddFloat("minWidth", minWidth);
		(void)reply.AddFloat("maxWidth", maxWidth);
		(void)reply.AddFloat("minHeight", minHeight);
		(void)reply.AddFloat("maxHeight", maxHeight);
		if (DisplayList() != nullptr)
			(void)reply.AddString("dump", gfx::DumpText(*DisplayList()));
		(void)message->SendReply(reply);
	}
};


BMessage
Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons)
{
	BMessage message(what);
	(void)message.AddPoint("where", where);
	(void)message.AddInt32("buttons", buttons);
	return message;
}


BMessage
Ask(const BMessenger& window)
{
	BMessage reply;
	EXPECT_TRUE(window.SendMessage(BMessage(kAsk), &reply).has_value());
	return reply;
}


TEST(WindowTest, EventsReachTheRightViews)
{
	// What is tested: A window of 200 × 100 with views A (left half), B
	// (right half) and C inside B, sent mouse moves, a click on C that
	// tracks the mouse and takes the focus, keys and an activation.
	// Success criteria: Each view gets entered/inside/exited in its own
	// coordinates, the deepest view under the mouse first (at (120, 20)
	// that is C inside B); C gets the mouse down, then, tracking, every
	// move (also outside it) and the mouse up; afterwards the view under
	// the mouse gets the moves again and C its exit; the key goes to the
	// focus view C; every view learns of the activation.
	// Why it matters: Haiku's event distribution (_DetermineTarget,
	// SetMouseEventMask()) is what controls are written against (W2).
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 200, 100});
	owned->AddChild(std::make_unique<EventView>(gfx::Rect{0, 0, 100, 100}, "A", record));
	BView* b = owned->AddChild(std::make_unique<EventView>(gfx::Rect{100, 0, 200, 100}, "B",
		record));
	auto* c = static_cast<EventView*>(b->AddChild(std::make_unique<EventView>(
		gfx::Rect{10, 10, 50, 50}, "C", record)));
	c->trackMouse = true;
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	for (const BMessage& event : {Mouse(kMouseMoved, {20, 20}, 0),
			Mouse(kMouseMoved, {60, 20}, 0), Mouse(kMouseMoved, {120, 20}, 0),
			Mouse(kMouseMoved, {130, 30}, 0), Mouse(kMouseDown, {130, 30}, 1),
			Mouse(kMouseMoved, {10, 10}, 1), Mouse(kMouseUp, {10, 10}, 0),
			Mouse(kMouseMoved, {12, 10}, 0)}) {
		ASSERT_TRUE(window.SendMessage(event).has_value());
	}
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "x");
	ASSERT_TRUE(window.SendMessage(key).has_value());
	BMessage active(kWindowActivated);
	(void)active.AddBool("active", true);
	ASSERT_TRUE(window.SendMessage(active).has_value());
	(void)Ask(window);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(record->Received(), (std::vector<std::string>{
		"A attached", "B attached", "C attached",
		"A entered 20,20", "A inside 60,20", "A exited 120,20", "C entered 10,10",
		"C inside 20,20", "C down 20,20", "C outside -100,0", "C up -100,0",
		"C exited -98,0", "A entered 12,10", "C key x", "A active", "B active",
		"C active"}));
}


TEST(WindowTest, FramesFollowInvalidationAndSize)
{
	// What is tested: A running window without a platform: its first
	// frame; a view invalidated three times in one message; the window
	// resized to 300 × 150 with a view that follows its size.
	// Success criteria: The first frame records every view once; the three
	// invalidations give one more frame in which only that view draws
	// again; the resize resizes the root view and the following view,
	// calls its FrameResized() and draws a frame of the new size.
	// Why it matters: Specification 11.4: invalidation drops one list and
	// asks for one frame; the window's list covers the content area.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 200, 100});
	auto* still = static_cast<EventView*>(owned->AddChild(std::make_unique<EventView>(
		gfx::Rect{0, 0, 50, 50}, "still", record)));
	auto* growing = static_cast<EventView*>(owned->AddChild(std::make_unique<EventView>(
		gfx::Rect{50, 0, 200, 100}, "growing", record, kFollowAll)));
	const BMessenger toStill(still);
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	const BMessage first = Ask(window);
	ASSERT_TRUE(toStill.SendMessage(kInvalidate).has_value());
	const BMessage second = Ask(window);
	BMessage resized(kWindowResized);
	(void)resized.AddFloat("width", 300);
	(void)resized.AddFloat("height", 150);
	ASSERT_TRUE(window.SendMessage(resized).has_value());
	const BMessage third = Ask(window);
	const int stillDraws = still->draws;
	const int growingDraws = growing->draws;
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(first.GetInt64("frames", 0), 1);
	EXPECT_EQ(second.GetInt64("frames", 0), 2);
	EXPECT_EQ(third.GetInt64("frames", 0), 3);
	EXPECT_EQ(third.GetRect("bounds", {}), (gfx::Rect{0, 0, 300, 150}));
	EXPECT_EQ(stillDraws, 2);
	EXPECT_EQ(growingDraws, 2);
	EXPECT_NE(record->Received().back().find("growing resized 250x150"), std::string::npos);
	EXPECT_EQ(third.GetString("dump", "").rfind("DisplayList bounds=[0 0 300 150]", 0), 0u)
		<< third.GetString("dump", "");
}


TEST(WindowTest, ChangedAreasReachThePresenter)
{
	// What is tested: A window on a platform whose windows have a scale of
	// 2: shown, then one view of 20 × 10 at (10, 10) invalidated; and the
	// close button of a window without NotClosable and of one with it.
	// Success criteria: The platform window is shown; the first frame
	// presents the whole window in device pixels (400 × 200), the second
	// only the view's area, (20, 20)-(60, 40); the close button quits the
	// window, unless it is NotClosable.
	// Why it matters: The CPU presenter copies only the union of the
	// invalidated areas (specification 12); the close button goes through
	// QuitRequested().
	//
	// Given
	auto platform = std::make_unique<FakePlatform>();
	platform->windowScale = 2;
	FakePlatform* fake = platform.get();
	BApplication app("application/x-vnd.ngui-window-test");
	app.SetPulseRate(Clock::Time::zero());
	ASSERT_TRUE(app.UsePlatform(std::move(platform)).has_value());
	std::thread runner([&] { (void)app.Run(); });
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	ASSERT_NE(platform::Current(), nullptr);

	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{50, 60, 250, 160});
	ASSERT_TRUE(owned->UseBackend(std::make_unique<PixelBackend>()).has_value());
	BView* view = owned->AddChild(std::make_unique<EventView>(gfx::Rect{10, 10, 30, 20}, "v",
		record));
	const BMessenger toView(view);
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	auto locked = std::make_unique<TestWindow>(gfx::Rect{0, 0, 10, 10},
		BWindowFlag::NotClosable);
	const BMessenger lockedWindow = *BLooper::Run(std::move(locked));
	ASSERT_EQ(fake->Windows().size(), 2u);
	FakeWindow* platformWindow = fake->Windows()[0];
	FakeWindow* lockedPlatformWindow = fake->Windows()[1];

	// When
	ASSERT_TRUE(platformWindow->WaitForPresents(1));
	ASSERT_TRUE(toView.SendMessage(kInvalidate).has_value());
	ASSERT_TRUE(platformWindow->WaitForPresents(2));
	BMessage close(kQuitRequested);
	(void)close.AddInt64("when", 1);
	lockedPlatformWindow->Send(close);
	(void)Ask(lockedWindow);
	const bool lockedStays = lockedWindow.IsValid();
	// The platform window dies with its window: read it before.
	const bool shown = platformWindow->IsShown();
	const auto presented = platformWindow->Presented();
	const gfx::Size presentedSize = platformWindow->PresentedSize();
	platformWindow->Send(close);
	const bool closed = window.WaitForLooperQuit(5s).has_value();

	// Then
	EXPECT_TRUE(shown);
	ASSERT_GE(presented.size(), 2u);
	EXPECT_EQ(presented[0], (std::vector<gfx::IntRect>{{0, 0, 400, 200}}));
	EXPECT_EQ(presented[1], (std::vector<gfx::IntRect>{{20, 20, 60, 40}}));
	EXPECT_EQ(presentedSize, (gfx::Size{400, 200}));
	EXPECT_TRUE(lockedStays);
	EXPECT_TRUE(closed);

	(void)lockedWindow.SendMessage(kQuitRequested);
	(void)lockedWindow.WaitForLooperQuit(5s);
	(void)AppMessenger().SendMessage(kQuitRequested);
	runner.join();
}


constexpr std::uint32_t kOpenPopup = FourCharCode("OPOP");
constexpr std::uint32_t kMovePopup = FourCharCode("MPOP");
constexpr std::uint32_t kInvalidatePopup = FourCharCode("IPOP");
constexpr std::uint32_t kClosePopup = FourCharCode("CPOP");


// A window that opens, moves, invalidates and closes a popup with an
// EventView "P" at (120, 130, 180, 170) on the screen when asked, in its own
// thread; kAsk also answers the number of popups and P's screen origin.
class PopupWindow : public TestWindow {
public:
	PopupWindow(gfx::Rect frame, std::shared_ptr<LooperRecord> record)
		:
		TestWindow(frame),
		fRecord(std::move(record))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kOpenPopup:
			{
				auto view = std::make_unique<EventView>(gfx::Rect{120, 130, 180, 170}, "P",
					fRecord);
				view->trackMouse = true;
				(void)OpenPopup(std::move(view));
				return;
			}
			case kMovePopup:
				PopupAt(0)->MoveTo({140, 130});
				return;
			case kInvalidatePopup:
				PopupAt(0)->Invalidate();
				return;
			case kClosePopup:
				fClosed = ClosePopup(PopupAt(0));
				return;
			case kAsk:
			{
				UpdateIfNeeded();
				BMessage reply(kReply);
				(void)reply.AddInt32("popups", CountPopups());
				if (CountPopups() > 0)
					(void)reply.AddPoint("origin", PopupAt(0)->ConvertToScreen({0, 0}));
				(void)reply.AddBool("closed", fClosed != nullptr);
				(void)message->SendReply(reply);
				return;
			}
			default:
				TestWindow::MessageReceived(message);
		}
	}

private:
	std::shared_ptr<LooperRecord> fRecord;
	std::unique_ptr<BView> fClosed;
};


TEST(WindowTest, PopupsAreSurfacesOfTheWindow)
{
	// What is tested: A window at (50, 60) on a fake platform opens a popup
	// with a view P at (120, 130, 180, 170) on the screen; the mouse moves
	// into the popup and presses P, which tracks the mouse, moves over the
	// window and is released there; the popup is moved, P invalidated, the
	// popup closed.
	// Success criteria: The popup is a platform popup of the window's
	// platform window at P's frame, shown and presented whole (60 × 40). P
	// gets the popup's events in its coordinates; while it tracks, the
	// window's events reach it too: (20, 20) in the window is (-50, -50)
	// for P. Moving P moves the platform popup; invalidating P presents
	// only the popup, not the window again. Closing gives P back.
	// Why it matters: Menus and tooltips are popups of their window (the
	// W3b decision): drawn and fed in its thread, across surfaces.
	//
	// Given
	auto platform = std::make_unique<FakePlatform>();
	FakePlatform* fake = platform.get();
	BApplication app("application/x-vnd.ngui-popup-test");
	app.SetPulseRate(Clock::Time::zero());
	ASSERT_TRUE(app.UsePlatform(std::move(platform)).has_value());
	std::thread runner([&] { (void)app.Run(); });
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	ASSERT_NE(platform::Current(), nullptr);

	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<PopupWindow>(gfx::Rect{50, 60, 250, 160}, record);
	ASSERT_TRUE(owned->UseBackend(std::make_unique<PixelBackend>()).has_value());
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	FakeWindow* main = fake->Windows()[0];
	ASSERT_TRUE(main->WaitForPresents(1));

	// When
	ASSERT_TRUE(window.SendMessage(kOpenPopup).has_value());
	const BMessage opened = Ask(window);
	ASSERT_EQ(fake->Windows().size(), 2u);
	FakeWindow* popup = fake->Windows()[1];
	ASSERT_TRUE(popup->WaitForPresents(1));
	const std::size_t mainPresents = main->Presented().size();
	popup->Send(Mouse(kMouseMoved, {10, 10}, 0));
	popup->Send(Mouse(kMouseDown, {10, 10}, 1));
	main->Send(Mouse(kMouseMoved, {20, 20}, 1));
	main->Send(Mouse(kMouseUp, {20, 20}, 0));
	ASSERT_TRUE(window.SendMessage(kMovePopup).has_value());
	const BMessage moved = Ask(window);
	const gfx::Rect movedFrame = popup->Frame();
	ASSERT_TRUE(window.SendMessage(kInvalidatePopup).has_value());
	ASSERT_TRUE(popup->WaitForPresents(3));
	(void)Ask(window);
	const bool popupIsPopup = popup->IsPopup();
	const bool parentIsMain = popup->Parent() == main;
	const bool popupShown = popup->IsShown();
	const auto popupPresented = popup->Presented();
	// The popup's platform window dies with the popup: read it before.
	ASSERT_TRUE(window.SendMessage(kClosePopup).has_value());
	const BMessage closed = Ask(window);
	const std::size_t mainPresentsAfter = main->Presented().size();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	(void)AppMessenger().SendMessage(kQuitRequested);
	runner.join();

	// Then
	EXPECT_EQ(opened.GetInt32("popups", -1), 1);
	EXPECT_EQ(opened.GetPoint("origin", {}), (gfx::Point{120, 130}));
	EXPECT_TRUE(popupIsPopup);
	EXPECT_TRUE(parentIsMain);
	EXPECT_TRUE(popupShown);
	ASSERT_GE(popupPresented.size(), 3u);
	EXPECT_EQ(popupPresented[0], (std::vector<gfx::IntRect>{{0, 0, 60, 40}}));
	EXPECT_EQ(movedFrame, (gfx::Rect{140, 130, 200, 170}));
	EXPECT_EQ(moved.GetPoint("origin", {}), (gfx::Point{140, 130}));
	EXPECT_EQ(mainPresentsAfter, mainPresents);
	EXPECT_EQ(closed.GetInt32("popups", -1), 0);
	EXPECT_TRUE(closed.GetBool("closed", false));
	EXPECT_EQ(record->Received(), (std::vector<std::string>{"P attached", "P entered 10,10",
		"P down 10,10", "P outside -50,-50", "P up -50,-50"}));
}


// A text system with fixed metrics and two fonts of the given ids.
class FontsTextSystem : public FixedTextSystem {
public:
	FontsTextSystem(gfx::FontId first, gfx::FontId second)
		:
		fFonts{FontFile{first, FontRole::Plain, std::as_bytes(std::span(kBytes))},
			FontFile{second, FontRole::Bold, std::as_bytes(std::span(kBytes))}}
	{
	}

	std::span<const FontFile> Fonts() const override
	{
		return fFonts;
	}

private:
	static constexpr char kBytes[] = "font";
	std::array<FontFile, 2> fFonts;
};


TEST(WindowTest, TheBackendGetsTheFontsOfTheTextSystem)
{
	// What is tested: A window with a backend that records AddFont(): its
	// first frame with a default text system of the fonts 1 and 2, a second
	// frame, and a frame after the default has been replaced by one with
	// the fonts 7 and 8.
	// Success criteria: The fonts 1 and 2 are added before the first frame,
	// not again for the second; after the replacement 7 and 8 are added.
	// Why it matters: The backend draws the glyph runs the text system
	// shapes, so it must have the same font files (specification 13.4); a
	// run in a font it does not know is not drawn.
	//
	// Given
	const FontsTextSystem first(1, 2);
	const FontsTextSystem second(7, 8);
	TextSystem::SetDefault(&first);
	auto added = std::make_shared<std::vector<gfx::FontId>>();
	auto backend = std::make_unique<PixelBackend>();
	backend->addedFonts = added;
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 100, 50});
	ASSERT_TRUE(owned->UseBackend(std::move(backend)).has_value());
	const BMessenger toView(owned->AddChild(std::make_unique<EventView>(
		gfx::Rect{0, 0, 10, 10}, "v", record)));
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	(void)Ask(window);
	ASSERT_TRUE(toView.SendMessage(kInvalidate).has_value());
	const BMessage secondFrame = Ask(window);
	TextSystem::SetDefault(&second);
	ASSERT_TRUE(toView.SendMessage(kInvalidate).has_value());
	const BMessage thirdFrame = Ask(window);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(secondFrame.GetInt64("frames", 0), 2);
	EXPECT_EQ(thirdFrame.GetInt64("frames", 0), 3);
	EXPECT_EQ(*added, (std::vector<gfx::FontId>{1, 2, 7, 8}));
}


TEST(WindowTest, TheWindowLaysOutItsViews)
{
	// What is tested: A window of 200 × 100 with AutoUpdateSizeLimits and a
	// stack layout holding a view of 60 × 40 to 80 × 50, preferred 70 × 45:
	// its first frame; resized by the platform to 300 × 150; ResizeTo()
	// 30 × 20, below the minimum; then ResizeToPreferred().
	// Success criteria: Before the first frame the view is laid out,
	// centered at (60, 25) with its maximum 80 × 50, and the size limits
	// are the root view's: 60 × 40 up to unlimited. The resize lays out
	// again: the view moves to (110, 50). ResizeTo() stays within the
	// limits, as Haiku's app_server: 60 × 40. ResizeToPreferred() gives the
	// window the preferred size 70 × 45.
	// Why it matters: Windows lay out their views (module G10) and keep the
	// user from making them smaller than their content, as Haiku's
	// B_AUTO_UPDATE_SIZE_LIMITS.
	//
	// Given
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 200, 100},
		BWindowFlag::AutoUpdateSizeLimits);
	(void)owned->SetLayout(std::make_unique<StackLayout>());
	BView* view = owned->AddChild(std::make_unique<EventView>(gfx::Rect{0, 0, 10, 10}, "v",
		record));
	view->SetExplicitMinSize({60, 40});
	view->SetExplicitMaxSize({80, 50});
	view->SetExplicitPreferredSize({70, 45});
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	const BMessage first = Ask(window);
	BMessage resized(kWindowResized);
	(void)resized.AddFloat("width", 300);
	(void)resized.AddFloat("height", 150);
	ASSERT_TRUE(window.SendMessage(resized).has_value());
	const BMessage larger = Ask(window);
	BMessage tooSmall(kResizeTo);
	(void)tooSmall.AddFloat("width", 30);
	(void)tooSmall.AddFloat("height", 20);
	ASSERT_TRUE(window.SendMessage(tooSmall).has_value());
	const BMessage corrected = Ask(window);
	ASSERT_TRUE(window.SendMessage(kResizeToPreferred).has_value());
	const BMessage preferred = Ask(window);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(first.FindRect("frame").value_or(gfx::Rect{}), (gfx::Rect{60, 25, 140, 75}));
	EXPECT_EQ(first.GetFloat("minWidth", 0), 60);
	EXPECT_EQ(first.GetFloat("minHeight", 0), 40);
	EXPECT_EQ(first.GetFloat("maxWidth", 0), kSizeUnlimited);
	EXPECT_EQ(larger.FindRect("frame").value_or(gfx::Rect{}), (gfx::Rect{110, 50, 190, 100}));
	EXPECT_EQ(corrected.FindRect("bounds").value_or(gfx::Rect{}), (gfx::Rect{0, 0, 60, 40}));
	EXPECT_EQ(preferred.FindRect("bounds").value_or(gfx::Rect{}), (gfx::Rect{0, 0, 70, 45}));
}


TEST(WindowTest, SizeLimitsReachThePlatformWindow)
{
	// What is tested: A window with AutoUpdateSizeLimits on a platform,
	// with a stack layout holding a view of at least 120 × 80 and at most
	// 400 × 300.
	// Success criteria: After the first frame the platform window has the
	// limits 120 × 80 to unlimited (the layout itself has no maximum).
	// Why it matters: The window system must not let the user shrink the
	// window below its content (SDL_SetWindowMinimumSize()).
	//
	// Given
	auto platform = std::make_unique<FakePlatform>();
	FakePlatform* fake = platform.get();
	BApplication app("application/x-vnd.ngui-window-limits-test");
	app.SetPulseRate(Clock::Time::zero());
	ASSERT_TRUE(app.UsePlatform(std::move(platform)).has_value());
	std::thread runner([&] { (void)app.Run(); });
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	ASSERT_NE(platform::Current(), nullptr);
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 200, 100},
		BWindowFlag::AutoUpdateSizeLimits);
	ASSERT_TRUE(owned->UseBackend(std::make_unique<PixelBackend>()).has_value());
	(void)owned->SetLayout(std::make_unique<StackLayout>());
	BView* view = owned->AddChild(std::make_unique<EventView>(gfx::Rect{0, 0, 10, 10}, "v",
		record));
	view->SetExplicitMinSize({120, 80});
	view->SetExplicitMaxSize({400, 300});
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));
	FakeWindow* platformWindow = fake->Windows()[0];

	// When
	ASSERT_TRUE(platformWindow->WaitForPresents(1));
	const auto limits = platformWindow->SizeLimits();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	(void)window.WaitForLooperQuit(5s);
	(void)AppMessenger().SendMessage(kQuitRequested);
	runner.join();

	// Then
	EXPECT_EQ(limits.first, (gfx::Size{120, 80}));
	EXPECT_EQ(limits.second, (gfx::Size{kSizeUnlimited, kSizeUnlimited}));
}


// A view that may get the focus, with or without its own use of keys.
class FocusView : public BView {
public:
	FocusView(std::string_view name, BViewFlags flags, bool usesKeys = false)
		:
		BView(gfx::Rect{0, 0, 10, 10}, name, kFollowLeftTop, flags),
		fUsesKeys(usesKeys)
	{
	}

	void KeyDown(std::string_view bytes) override
	{
		if (!fUsesKeys)
			BView::KeyDown(bytes);
	}

private:
	bool fUsesKeys;
};


BMessage
Tab(BModifierFlags modifiers = {})
{
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "\t");
	(void)key.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
	return key;
}


TEST(WindowTest, TabMovesTheFocus)
{
	// What is tested: Tab, Shift+Tab and Option+Tab in a window with the
	// navigable views A, C (inside the plain view B) and a hidden D, a jump
	// group E, and a navigable view F that uses Tab itself.
	// Success criteria: Without a focus, Tab focuses A; Tab goes on to C,
	// skipping B and the hidden D, then to F; at F the Tab stays, since F
	// uses it; Option+Tab moves to the jump group E even from F, and stays
	// there, as it is the only one; Shift+Tab goes back from E to C (past
	// the hidden D) and A, then wraps around to F.
	// Why it matters: Haiku's keyboard navigation (_KeyboardNavigation(),
	// _FindNextNavigable()), on which every dialog relies.
	//
	// Given
	auto owned = std::make_unique<TestWindow>(gfx::Rect{0, 0, 100, 100});
	owned->AddChild(std::make_unique<FocusView>("A", BViewFlag::Navigable));
	BView* b = owned->AddChild(std::make_unique<FocusView>("B", BViewFlags()));
	b->AddChild(std::make_unique<FocusView>("C", BViewFlag::Navigable));
	owned->AddChild(std::make_unique<FocusView>("D", BViewFlag::Navigable))->Hide();
	owned->AddChild(std::make_unique<FocusView>("E", BViewFlag::NavigableJump));
	owned->AddChild(std::make_unique<FocusView>("F", BViewFlag::Navigable, true));
	const BMessenger window = *BLooper::Run(std::move(owned));
	std::vector<std::string> focus;
	const auto press = [&](BModifierFlags modifiers) {
		ASSERT_TRUE(window.SendMessage(Tab(modifiers)).has_value());
		focus.emplace_back(Ask(window).GetString("focus", "?"));
	};

	// When
	press({});
	press({});
	press({});
	press({});
	press(BModifier::Option);
	press(BModifier::Option);
	press(BModifier::Shift);
	press(BModifier::Shift);
	press(BModifier::Shift);
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());

	// Then
	EXPECT_EQ(focus, (std::vector<std::string>{"A", "C", "F", "F", "E", "E", "C", "A", "F"}));
}


} // namespace
