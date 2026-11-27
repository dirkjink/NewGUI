/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The frames NewGUI draws around windows (BWindowFrames::Look, specification
// 24 point 9): the look's decorator (Haiku's BeDecorator for "be") around
// the content, on the headless platform, and what the mouse does on it, as
// Haiku's DefaultWindowBehaviour. Text metrics are FixedTextSystem's (size
// 12, ascent 9, descent 3, 7 pixels per byte), so the frame has Haiku's
// sizes: a border of 5, a tab of 20 rows above it.


#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/LookRegistry.h>
#include <ngui/Looper.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/test/Driver.h>

#include "decor/Decorator.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;
using Region = ngui::detail::Decorator::Region;


// The frames of the look for the windows of a test.
class LookFrames {
public:
	LookFrames()
	{
		BApplication::SetWindowFrames(BWindowFrames::Look);
	}

	~LookFrames()
	{
		BApplication::SetWindowFrames(BWindowFrames::System);
	}

	LookFrames(const LookFrames&) = delete;
	LookFrames& operator=(const LookFrames&) = delete;
};


// A window "Frames" of 200 × 100 at (100, 100) with \a flags, drawing into
// pixel buffers; it counts its quit requests that were granted.
class FramedWindow : public BWindow {
public:
	explicit FramedWindow(BWindowFlags flags = {},
		BWindowLook look = BWindowLook::Titled)
		:
		BWindow({100, 100, 300, 200}, "Frames", look, flags)
	{
		(void)UseBackend(std::make_unique<PixelBackend>());
	}
};


// The application on the headless platform with a framed window.
class FramedApp {
public:
	explicit FramedApp(BWindowFlags flags = {}, BWindowLook look = BWindowLook::Titled)
		:
		fApp("application/x-vnd.ngui-window-frames-test")
	{
		TextSystem::SetDefault(&fText);
		auto owned = std::make_unique<HeadlessPlatform>();
		fPlatform = owned.get();
		(void)fApp.UsePlatform(std::move(owned));
		fDriver = std::make_unique<Driver>(fApp);
		auto window = std::make_unique<FramedWindow>(flags, look);
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver->WaitForIdle();
	}

	~FramedApp()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		fDriver.reset();
		TextSystem::SetDefault(nullptr);
	}

	Driver& Drive()
	{
		return *fDriver;
	}

	WindowRef Window()
	{
		return fDriver->Window("Frames");
	}

	HeadlessWindow* Platform()
	{
		return Window().PlatformWindow();
	}

	gfx::Rect Frame()
	{
		(void)fDriver->WaitForIdle();
		return Window().Frame();
	}

	// A mouse event in the content's coordinates, with \a buttons and
	// \a modifiers, as the window system sends it (in the window).
	void Mouse(std::uint32_t what, gfx::Point where, std::int32_t buttons,
		std::uint32_t modifiers = 0)
	{
		HeadlessWindow* window = Platform();
		const gfx::Point offset = window->FrameLayout().contentOffset;
		BMessage message(what);
		(void)message.AddPoint("where", {where.x + offset.x, where.y + offset.y});
		(void)message.AddInt32("buttons", buttons);
		(void)message.AddInt32("modifiers", static_cast<std::int32_t>(modifiers));
		if (what == kMouseDown)
			(void)message.AddInt32("clicks", 1);
		(void)window->Send(message);
		(void)fDriver->WaitForIdle();
	}

	BMessenger Messenger() const
	{
		return fWindow;
	}

private:
	TestEnvironment fEnvironment;
	FixedTextSystem fText;
	BApplication fApp;
	HeadlessPlatform* fPlatform = nullptr;
	std::unique_ptr<Driver> fDriver;
	BMessenger fWindow;
};


TEST(WindowFramesTest, TheBeDecoratorHasHaikusGeometry)
{
	// What is tested: The decorator of the look "be" (BeDecorator) around
	// content of 200 × 100 with the title "Frames"; then without the close
	// and zoom buttons; then as a bordered window.
	// Success criteria: The border is 5 wide; the tab spans -5 to 108 and
	// -24 to -4 (20 rows, ending on the border's top row), the bounds
	// {-5, -24, 205, 105}; the close button is {0, -19, 13, -6}, the zoom
	// button {90, -19, 103, -6}; the regions under points are the tab, the
	// buttons, the borders and the resize corner at the bottom right. Without
	// buttons the tab is narrower (to 74) and both button areas are tab. The
	// bordered window has a border of 1 and no tab.
	// Why it matters: These are the sizes of Haiku's TabDecorator with the
	// BeDecorator's buttons (offset 5, size tab height - 7); the window
	// system places the content and the mouse by them.
	//
	// Given
	FixedTextSystem text;
	std::unique_ptr<detail::Decorator> decorator = detail::MakeDecorator("be", text);
	ASSERT_NE(decorator, nullptr);

	// When
	decorator->SetTitle("Frames");
	decorator->SetContentSize(200, 100);
	const gfx::Rect bounds = decorator->Bounds();
	const gfx::Rect tab = decorator->TabRect();
	const gfx::Rect close = decorator->CloseRect();
	const gfx::Rect zoom = decorator->ZoomRect();
	const std::vector<Region> regions = {decorator->RegionAt({50, -15}),
		decorator->RegionAt({5, -12}), decorator->RegionAt({95, -12}),
		decorator->RegionAt({-3, 50}), decorator->RegionAt({202, 50}),
		decorator->RegionAt({100, -2}), decorator->RegionAt({100, 102}),
		decorator->RegionAt({203, 103}), decorator->RegionAt({150, -15}),
		decorator->RegionAt({100, 50})};
	decorator->SetFlags(false, false, true);
	const gfx::Rect plainTab = decorator->TabRect();
	const Region formerClose = decorator->RegionAt({5, -12});
	std::unique_ptr<detail::Decorator> bordered = detail::MakeDecorator("be", text);
	bordered->SetLook(detail::Decorator::Look::Bordered);
	bordered->SetContentSize(200, 100);

	// Then
	EXPECT_EQ(decorator->BorderWidth(), 5);
	EXPECT_EQ(tab, (gfx::Rect{-5, -24, 108, -4}));
	EXPECT_EQ(bounds, (gfx::Rect{-5, -24, 205, 105}));
	EXPECT_EQ(close, (gfx::Rect{0, -19, 13, -6}));
	EXPECT_EQ(zoom, (gfx::Rect{90, -19, 103, -6}));
	EXPECT_EQ(regions, (std::vector<Region>{Region::Tab, Region::CloseButton,
		Region::ZoomButton, Region::LeftBorder, Region::RightBorder, Region::TopBorder,
		Region::BottomBorder, Region::RightBottomCorner, Region::None, Region::None}));
	EXPECT_EQ(plainTab, (gfx::Rect{-5, -24, 74, -4}));
	EXPECT_EQ(formerClose, Region::Tab);
	EXPECT_EQ(bordered->BorderWidth(), 1);
	EXPECT_EQ(bordered->Bounds(), (gfx::Rect{-1, -1, 201, 101}));
	EXPECT_EQ(bordered->TabRect(), (gfx::Rect{0, 0, 0, 0}));
}


TEST(WindowFramesTest, AWindowGetsTheFrameOfItsLook)
{
	// What is tested: An application on the headless platform (which has
	// transparent windows) asks for the frames of the look; a titled window
	// "Frames" of 200 × 100 at (100, 100) in the look "be". Then a window
	// without that wish.
	// Success criteria: The platform's window is the content with the
	// frame: {95, 76, 305, 205}; the content lies at (5, 24) in it; the
	// frame's shape has the four borders and the tab. Frame() stays the
	// content, {100, 100, 300, 200}; a click into the content reaches the
	// content's coordinates as before (the driver places it by the layout).
	// Without the wish the platform's window is the content itself.
	// Why it matters: Programs keep Haiku's meaning of Frame() and of view
	// coordinates, while the window system gets the whole window and knows
	// where the content is.
	//
	// Given
	std::optional<gfx::Rect> outer;
	WindowFrameLayout layout;
	gfx::Rect content;
	{
		const LookFrames frames;
		FramedApp app;

		// When
		outer = app.Platform()->Frame();
		layout = app.Platform()->FrameLayout();
		content = app.Frame();
	}
	FramedApp plain;
	const gfx::Rect plainOuter = plain.Platform()->Frame();
	const WindowFrameLayout plainLayout = plain.Platform()->FrameLayout();

	// Then
	ASSERT_TRUE(outer.has_value());
	EXPECT_EQ(*outer, (gfx::Rect{95, 76, 305, 205}));
	EXPECT_EQ(layout.contentOffset, (gfx::Point{5, 24}));
	EXPECT_EQ(layout.shape.size(), 5u);
	EXPECT_EQ(content, (gfx::Rect{100, 100, 300, 200}));
	EXPECT_EQ(plainOuter, (gfx::Rect{100, 100, 300, 200}));
	EXPECT_EQ(plainLayout.contentOffset, (gfx::Point{0, 0}));
	EXPECT_TRUE(plainLayout.shape.empty());
}


TEST(WindowFramesTest, TheTabMovesTheWindow)
{
	// What is tested: A framed window; the tab is dragged by 40, 30 (from
	// (50, -15) of the content); then, with Shift, by 60 to the right.
	// Success criteria: The first drag moves the window by 40, 30: content
	// at {140, 130, 340, 230}. With Shift the tab slides along the top
	// instead (Haiku's SlideTabState): the window stays, the tab begins 60
	// further right.
	// Why it matters: Without the window system's frame the window must be
	// moved by its own tab, and Haiku's sliding tabs come with it.
	//
	// Given
	const LookFrames frames;
	FramedApp app;
	WindowRef window = app.Window();

	// When
	ASSERT_TRUE(app.Drive().Drag(window, {50, -15}, {90, 15}, 1).has_value());
	const gfx::Rect moved = app.Frame();
	const float tabBefore = app.Platform()->FrameLayout().shape.back().x0;
	app.Mouse(kMouseDown, {50, -15}, 1, static_cast<std::uint32_t>(BModifier::Shift));
	app.Mouse(kMouseMoved, {110, -15}, 1, static_cast<std::uint32_t>(BModifier::Shift));
	app.Mouse(kMouseUp, {110, -15}, 0, static_cast<std::uint32_t>(BModifier::Shift));
	const gfx::Rect slid = app.Frame();
	const float tabAfter = app.Platform()->FrameLayout().shape.back().x0;

	// Then
	EXPECT_EQ(moved, (gfx::Rect{140, 130, 340, 230}));
	EXPECT_EQ(slid, moved);
	EXPECT_FLOAT_EQ(tabAfter - tabBefore, 60);
}


TEST(WindowFramesTest, TheBordersResizeTheWindow)
{
	// What is tested: A framed window; the resize corner at the bottom right
	// is dragged by 50, 20; then the left border is dragged with the
	// secondary button by -30 (Haiku's ResizeBorderState).
	// Success criteria: The corner gives content of 250 × 120 at (100, 100);
	// the left border keeps the right edge (350) and makes the content 280
	// wide from x = 70. The platform's window follows with the frame.
	// Why it matters: Windows without the system's frame must still be
	// resized by the user, as on Haiku.
	//
	// Given
	const LookFrames frames;
	FramedApp app;
	WindowRef window = app.Window();

	// When
	ASSERT_TRUE(app.Drive().Drag(window, {203, 103}, {253, 123}).has_value());
	const gfx::Rect cornered = app.Frame();
	app.Mouse(kMouseDown, {-3, 50}, 2);
	app.Mouse(kMouseMoved, {-33, 50}, 2);
	app.Mouse(kMouseUp, {-33, 50}, 0);
	const gfx::Rect bordered = app.Frame();
	const gfx::Rect outer = app.Platform()->Frame();

	// Then
	EXPECT_EQ(cornered, (gfx::Rect{100, 100, 350, 220}));
	EXPECT_EQ(bordered, (gfx::Rect{70, 100, 350, 220}));
	EXPECT_EQ(outer, (gfx::Rect{65, 76, 355, 225}));
}


TEST(WindowFramesTest, TheButtonsCloseAndZoom)
{
	// What is tested: A framed window on the headless screen (1920 × 1080);
	// the zoom button is clicked, then again; the close button is pressed,
	// the mouse leaves it and is released; then the close button is
	// clicked.
	// Success criteria: The first zoom gives the screen without the frame:
	// content {5, 24, 1915, 1075}; the second puts it back to {100, 100,
	// 300, 200}. Leaving the close button before the release keeps the
	// window; the click closes it.
	// Why it matters: The tab's buttons are the window's close and zoom
	// (Haiku's DecoratorButtonState: only a release on the button counts).
	//
	// Given
	const LookFrames frames;
	FramedApp app;
	WindowRef window = app.Window();

	// When
	ASSERT_TRUE(app.Drive().ClickAt(window, {95, -12}).has_value());
	const gfx::Rect zoomed = app.Frame();
	ASSERT_TRUE(app.Drive().ClickAt(window, {95, -12}).has_value());
	const gfx::Rect back = app.Frame();
	app.Mouse(kMouseDown, {5, -12}, 1);
	app.Mouse(kMouseMoved, {50, 50}, 1);
	app.Mouse(kMouseUp, {50, 50}, 0);
	const bool kept = app.Messenger().IsValid();
	ASSERT_TRUE(app.Drive().ClickAt(window, {5, -12}).has_value());
	const bool closed = app.Messenger().WaitForLooperQuit(5s).has_value();

	// Then
	EXPECT_EQ(zoomed, (gfx::Rect{5, 24, 1915, 1075}));
	EXPECT_EQ(back, (gfx::Rect{100, 100, 300, 200}));
	EXPECT_TRUE(kept);
	EXPECT_TRUE(closed);
}


TEST(WindowFramesTest, TheFlagsShapeTheFrame)
{
	// What is tested: A framed window that is not closable, not zoomable and
	// not resizable: clicks where the buttons would be, a drag of the
	// corner, a drag with the secondary button on the left border.
	// Success criteria: The window stays open, keeps its size and place
	// (the clicks are on the tab, which moves nothing without dragging).
	// Why it matters: B_NOT_CLOSABLE, B_NOT_ZOOMABLE and B_NOT_RESIZABLE
	// take the frame's buttons and resizing away, as on Haiku.
	//
	// Given
	const LookFrames frames;
	FramedApp app(BWindowFlag::NotClosable | BWindowFlag::NotZoomable
		| BWindowFlag::NotResizable);
	WindowRef window = app.Window();

	// When
	ASSERT_TRUE(app.Drive().ClickAt(window, {5, -12}).has_value());
	ASSERT_TRUE(app.Drive().ClickAt(window, {60, -12}).has_value());
	ASSERT_TRUE(app.Drive().Drag(window, {203, 103}, {253, 123}, 1).has_value());
	const gfx::Rect afterCorner = app.Frame();
	app.Mouse(kMouseDown, {-3, 50}, 2);
	app.Mouse(kMouseMoved, {-33, 50}, 2);
	app.Mouse(kMouseUp, {-33, 50}, 0);
	const gfx::Rect afterBorder = app.Frame();
	const bool open = app.Messenger().IsValid();

	// Then
	EXPECT_TRUE(open);
	// the corner is a border without resizing: dragging it moves the window
	EXPECT_EQ(afterCorner, (gfx::Rect{150, 120, 350, 220}));
	EXPECT_EQ(afterBorder, afterCorner);
}


TEST(WindowFramesTest, TheHaikuAndFlatDecoratorsHaveTabDecoratorsButtons)
{
	// What is tested: The decorators of the looks "haiku" (Haiku's
	// DefaultDecorator) and "flat" (FlatDecorator) around content of 200 ×
	// 100 with the title "Frames"; a look without a decorator.
	// Success criteria: Both have the border of 5 and TabDecorator's buttons
	// (offset 5, inset 2, size tab height - 10 + 2 = 11): the tab spans -5
	// to 110, the close button {0, -19, 12, -7}, the zoom button {93, -19,
	// 105, -7}. A look without a decorator gives none.
	// Why it matters: Haiku's own decorators keep TabDecorator's layout; only
	// the BeDecorator has buttons of its own size. A look that brings no
	// frame leaves the window system's.
	//
	// Given
	FixedTextSystem text;

	for (const char* look : {"haiku", "flat"}) {
		// When
		std::unique_ptr<detail::Decorator> decorator = detail::MakeDecorator(look, text);
		ASSERT_NE(decorator, nullptr) << look;
		decorator->SetTitle("Frames");
		decorator->SetContentSize(200, 100);

		// Then
		EXPECT_EQ(decorator->BorderWidth(), 5) << look;
		EXPECT_EQ(decorator->TabRect(), (gfx::Rect{-5, -24, 110, -4})) << look;
		EXPECT_EQ(decorator->CloseRect(), (gfx::Rect{0, -19, 12, -7})) << look;
		EXPECT_EQ(decorator->ZoomRect(), (gfx::Rect{93, -19, 105, -7})) << look;
	}
	EXPECT_EQ(detail::MakeDecorator("plugin", text), nullptr);
}


TEST(WindowFramesTest, TheFrameFollowsTheLook)
{
	// What is tested: A framed window in the look "be"; the look "haiku"
	// becomes current, then "be" again.
	// Success criteria: In "haiku" the window's tab is Haiku's (115 pixels
	// wide instead of the BeDecorator's 113); the content stays at {100,
	// 100, 300, 200}. Back in "be" the tab is 113 wide again.
	// Why it matters: The demo and the user switch looks while windows are
	// open; the frame belongs to the look (as decided for this block).
	//
	// Given
	const LookFrames frames;
	FramedApp app;
	const auto tabWidth = [&app]() {
		const gfx::Rect tab = app.Platform()->FrameLayout().shape.back();
		return tab.x1 - tab.x0;
	};
	const float be = tabWidth();

	// When
	(void)LookRegistry::Default().SetCurrent("haiku");
	const gfx::Rect content = app.Frame();
	const float haiku = tabWidth();
	(void)LookRegistry::Default().SetCurrent("be");
	(void)app.Drive().WaitForIdle();
	const float back = tabWidth();

	// Then
	EXPECT_FLOAT_EQ(be, 113);
	EXPECT_FLOAT_EQ(haiku, 115);
	EXPECT_EQ(content, (gfx::Rect{100, 100, 300, 200}));
	EXPECT_FLOAT_EQ(back, 113);
}


} // namespace
