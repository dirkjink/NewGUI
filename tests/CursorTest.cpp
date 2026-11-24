/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Cursor.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Bitmap.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/SplitView.h>
#include <ngui/TextView.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "support/FakePlatform.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;
using test::FakePlatform;


constexpr std::uint32_t kAsk = FourCharCode("ASK ");
constexpr std::uint32_t kSetCursor = FourCharCode("SCUR");


// The system cursor \a id as the platform gets it.
platform::CursorShape
System(BCursorID id)
{
	return BCursor(id).Shape();
}


TEST(CursorTest, CursorsAreValues)
{
	// What is tested: System cursors; a cursor of a 2 × 2 B_RGB32 bitmap
	// with its hot spot outside; one of an invalid bitmap; R5 cursor data
	// (hot spot y 2, x 3; the first row black, white and hidden); data of
	// the wrong size or depth.
	// Success criteria: System cursors are equal by their id, a copy equals
	// its original. The bitmap gives an RGBA image of its pixels (B_RGB32
	// opaque), the hot spot clamped into it. An invalid bitmap or wrong data
	// fail InitCheck() with InvalidArgument. The R5 data give a 16 × 16 image
	// with the hot spot at (3, 2): a set color bit black, a clear one white,
	// a clear mask bit transparent.
	// Why it matters: Haiku programs make their own cursors from bitmaps or
	// R5 data, and compare cursors.
	//
	// Given
	BBitmap bitmap({0, 0, 2, 2}, BColorSpace::RGB32);
	// B, G, R, (unused) of the first pixel
	bitmap.Bits()[0] = 0x10;
	bitmap.Bits()[1] = 0x20;
	bitmap.Bits()[2] = 0x30;
	const BBitmap invalid({0, 0, 0, 0}, BColorSpace::RGB32);
	std::array<std::uint8_t, 68> r5{};
	r5[0] = 16;
	r5[1] = 1;
	r5[2] = 2;
	r5[3] = 3;
	// first row: pixel 0 black and shown, pixel 1 white and shown, pixel 2
	// hidden
	r5[4] = 0x80;
	r5[36] = 0xc0;
	std::array<std::uint8_t, 68> deep = r5;
	deep[1] = 8;

	// When
	const BCursor iBeam(BCursorID::IBeam);
	const BCursor copy = iBeam;
	const BCursor fromBitmap(bitmap, {5, -3});
	const BCursor fromInvalid(invalid, {0, 0});
	const BCursor fromR5{std::span<const std::uint8_t>(r5)};
	const BCursor fromDeep{std::span<const std::uint8_t>(deep)};
	const BCursor fromShort{std::span<const std::uint8_t>(r5).first(60)};

	// Then
	EXPECT_EQ(copy, iBeam);
	EXPECT_FALSE(iBeam == BCursor(BCursorID::SystemDefault));
	EXPECT_EQ(iBeam.Shape().system, 2);
	EXPECT_EQ(iBeam.Shape().image, nullptr);
	ASSERT_TRUE(fromBitmap.InitCheck().has_value());
	const platform::CursorImage& image = *fromBitmap.Shape().image;
	EXPECT_EQ(image.width, 2);
	EXPECT_EQ(image.height, 2);
	EXPECT_EQ(image.hotspot, (gfx::Point{1, 0}));
	EXPECT_EQ(std::vector<std::uint8_t>(image.rgba.begin(), image.rgba.begin() + 4),
		(std::vector<std::uint8_t>{0x30, 0x20, 0x10, 0xff}));
	EXPECT_EQ(fromInvalid.InitCheck().error().code, ErrorCode::InvalidArgument);
	ASSERT_TRUE(fromR5.InitCheck().has_value());
	const platform::CursorImage& r5Image = *fromR5.Shape().image;
	EXPECT_EQ(r5Image.width, 16);
	EXPECT_EQ(r5Image.hotspot, (gfx::Point{3, 2}));
	EXPECT_EQ(std::vector<std::uint8_t>(r5Image.rgba.begin(), r5Image.rgba.begin() + 12),
		(std::vector<std::uint8_t>{0, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0}));
	EXPECT_FALSE(fromDeep.InitCheck().has_value());
	EXPECT_FALSE(fromShort.InitCheck().has_value());
}


// A window at (0, 0) of 300 × 200 with "beam" at (10, 10, 110, 60) (the
// I-beam cursor), its child "inner" at (0, 0, 20, 20) (none) and "plain" at
// (10, 100, 110, 150) (none). kSetCursor gives "plain" the cross hair;
// kAsk answers when the messages before it are handled.
class CursorWindow : public BWindow {
public:
	CursorWindow()
		:
		BWindow({0, 0, 300, 200}, "cursors")
	{
		BView* beam = AddChild(std::make_unique<BView>(gfx::Rect{10, 10, 110, 60}, "beam",
			BResizingMode{}, BViewFlags{}));
		beam->SetViewCursor(BCursor(BCursorID::IBeam));
		beam->AddChild(std::make_unique<BView>(gfx::Rect{0, 0, 20, 20}, "inner",
			BResizingMode{}, BViewFlags{}));
		AddChild(std::make_unique<BView>(gfx::Rect{10, 100, 110, 150}, "plain",
			BResizingMode{}, BViewFlags{}));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSetCursor) {
			FindView("plain")->SetViewCursor(BCursor(BCursorID::CrossHair));
			return;
		}
		if (message->what == kAsk) {
			(void)message->SendReply(BMessage(kAsk));
			return;
		}
		BWindow::MessageReceived(message);
	}
};


// A running application on the fake platform with a window of \a Window.
template<typename Window>
class RunningCursors {
public:
	RunningCursors()
		:
		fApp("application/x-vnd.ngui-cursor-test")
	{
		TextSystem::SetDefault(&fText);
		fApp.SetPulseRate(Clock::Time::zero());
		auto fake = std::make_unique<FakePlatform>();
		fPlatform = fake.get();
		EXPECT_TRUE(fApp.UsePlatform(std::move(fake)).has_value());
		fRunner = std::thread([this] { (void)fApp.Run(); });
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		auto window = std::make_unique<Window>();
		EXPECT_TRUE(window->UseBackend(std::make_unique<test::PixelBackend>()).has_value());
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
	}

	~RunningCursors()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
		TextSystem::SetDefault(nullptr);
	}

	// The mouse moved to \a where in the window; handled when this returns.
	void Move(gfx::Point where, bool exited = false)
	{
		BMessage message(kMouseMoved);
		(void)message.AddPoint("where", where);
		(void)message.AddInt32("buttons", 0);
		if (exited)
			(void)message.AddBool("be:exited", true);
		EXPECT_TRUE(fWindow.SendMessage(message).has_value());
		Sync();
	}

	void Send(std::uint32_t what)
	{
		EXPECT_TRUE(fWindow.SendMessage(what).has_value());
		Sync();
	}

	void Sync()
	{
		BMessage reply;
		(void)fWindow.SendMessage(BMessage(kAsk), &reply);
	}

	// The cursor the platform shows now.
	platform::CursorShape Shown()
	{
		const std::vector<platform::CursorShape> cursors = fPlatform->Cursors();
		return cursors.empty() ? platform::CursorShape{} : cursors.back();
	}

	// Whether the platform shows the cursor now.
	bool Visible()
	{
		const std::vector<bool> visible = fPlatform->CursorVisibility();
		return visible.empty() || visible.back();
	}

private:
	test::FixedTextSystem fText;
	BApplication fApp;
	FakePlatform* fPlatform = nullptr;
	std::thread fRunner;
	BMessenger fWindow;
};


TEST(CursorTest, TheViewUnderTheMouseChoosesTheCursor)
{
	// What is tested: On the fake platform, the mouse over a view with the
	// I-beam cursor, over its child without one, over a view without one;
	// the application's cursor set to the cross hair while the mouse is
	// there; the mouse leaving the window; the view under the mouse given a
	// cursor; HideCursor() twice and ShowCursor() twice; ObscureCursor() and
	// a move.
	// Success criteria: As Haiku's app_server: the view under the mouse
	// shows its cursor; a child without one shows the application's (Haiku
	// does not inherit view cursors); a new application cursor shows at once
	// where no view has one, and outside the window. A view cursor given
	// under the mouse shows at once. The cursor is hidden until as many
	// ShowCursor() as HideCursor() calls; IsCursorHidden() says so. An
	// obscured cursor shows again when the mouse moves, without changing
	// IsCursorHidden().
	// Why it matters: Text views show the I-beam, split views the resize
	// arrows; applications hide the cursor while typing (ObscureCursor()).
	//
	// Given
	RunningCursors<CursorWindow> app;

	// When / Then
	app.Move({50, 30});
	EXPECT_EQ(app.Shown(), System(BCursorID::IBeam));
	app.Move({15, 15});
	EXPECT_EQ(app.Shown(), System(BCursorID::SystemDefault));
	app.Move({50, 120});
	EXPECT_EQ(app.Shown(), System(BCursorID::SystemDefault));
	BApplication::SetCursor(BCursor(BCursorID::Progress));
	EXPECT_EQ(app.Shown(), System(BCursorID::Progress));
	app.Move({50, 30});
	EXPECT_EQ(app.Shown(), System(BCursorID::IBeam));
	app.Move({400, 30}, true);
	EXPECT_EQ(app.Shown(), System(BCursorID::Progress));
	app.Move({50, 120});
	app.Send(kSetCursor);
	EXPECT_EQ(app.Shown(), System(BCursorID::CrossHair));
	BApplication::SetCursor(BCursor(BCursorID::SystemDefault));

	BApplication::HideCursor();
	BApplication::HideCursor();
	EXPECT_FALSE(app.Visible());
	EXPECT_TRUE(BApplication::IsCursorHidden());
	BApplication::ShowCursor();
	EXPECT_FALSE(app.Visible());
	BApplication::ShowCursor();
	EXPECT_TRUE(app.Visible());
	EXPECT_FALSE(BApplication::IsCursorHidden());
	BApplication::ObscureCursor();
	EXPECT_FALSE(app.Visible());
	EXPECT_FALSE(BApplication::IsCursorHidden());
	app.Move({51, 120});
	EXPECT_TRUE(app.Visible());
}


// A window with an editable text view at (0, 0, 100, 50), a read-only and
// not selectable one at (0, 60, 100, 110), and a horizontal split view at
// (120, 0, 300, 100) with two panes.
class WidgetWindow : public BWindow {
public:
	WidgetWindow()
		:
		BWindow({0, 0, 300, 200}, "widgets")
	{
		AddChild(std::make_unique<BTextView>(gfx::Rect{0, 0, 100, 50}, "editable",
			gfx::Rect{0, 0, 100, 50}, BResizingMode{}, BViewFlags{BViewFlag::WillDraw}));
		auto label = std::make_unique<BTextView>(gfx::Rect{0, 60, 100, 110}, "label",
			gfx::Rect{0, 0, 100, 50}, BResizingMode{}, BViewFlags{BViewFlag::WillDraw});
		label->MakeEditable(false);
		label->MakeSelectable(false);
		AddChild(std::move(label));
		auto* split = static_cast<BSplitView*>(AddChild(
			std::make_unique<BSplitView>(BOrientation::Horizontal)));
		split->AddChild(std::make_unique<BView>("left", BViewFlags{}), 1.0f);
		split->AddChild(std::make_unique<BView>("right", BViewFlags{}), 1.0f);
		split->MoveTo({120, 0});
		split->ResizeTo(180, 100);
		split->Layout(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kAsk) {
			(void)message->SendReply(BMessage(kAsk));
			return;
		}
		BWindow::MessageReceived(message);
	}
};


TEST(CursorTest, TextAndSplitViewsShowTheirCursors)
{
	// What is tested: The mouse over an editable text view, over one that is
	// neither editable nor selectable, over a pane of a horizontal split
	// view and over its splitter (in the middle: the panes share the 180
	// pixels).
	// Success criteria: As Haiku: the I-beam over text that can be
	// selected, the arrow over text that cannot; the arrow over a pane, the
	// east-west resize cursor over the splitter of a horizontal split view.
	// Why it matters: These cursors tell the user what the mouse can do.
	//
	// Given
	RunningCursors<WidgetWindow> app;

	// When / Then
	app.Move({50, 25});
	EXPECT_EQ(app.Shown(), System(BCursorID::IBeam));
	app.Move({50, 85});
	EXPECT_EQ(app.Shown(), System(BCursorID::SystemDefault));
	app.Move({130, 50});
	EXPECT_EQ(app.Shown(), System(BCursorID::SystemDefault));
	app.Move({210, 50});
	EXPECT_EQ(app.Shown(), System(BCursorID::ResizeEastWest));
}


} // namespace
