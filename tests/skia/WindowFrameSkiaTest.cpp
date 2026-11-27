/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The frame of the look "be" drawn by Skia (BWindowFrames::Look): Haiku's
// BeDecorator around a window's content, the yellow tab of BeOS R5 with its
// close and zoom buttons, on a transparent window.


#include <chrono>
#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Button.h>
#include <ngui/LookRegistry.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/Driver.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Image.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// What \a window shows, its frame with it (a snapshot renders the views
// only).
Image
Shown(const WindowRef& window)
{
	const auto shown = window.CaptureFrame();
	return shown.has_value() ? Image::FromBitmap(*shown) : Image();
}


// A window "Frames" of 240 × 120 with a button, drawn by Skia.
class FramedWindow : public BWindow {
public:
	FramedWindow()
		:
		BWindow({100, 100, 340, 220}, "Frames", BWindowLook::Titled)
	{
		(void)UseBackend(std::make_unique<gfx::SkiaBackend>());
		AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "ok", "OK",
			std::make_unique<BMessage>(1)));
	}
};


TEST(WindowFrameSkiaTest, TheBeFrameMatchesItsSnapshot)
{
	// What is tested: An application with the bundled fonts on the headless
	// platform asks for the frames of the look; a window "Frames" of 240 ×
	// 120 in the look "be", active; then with its close button pressed; then
	// inactive (another window is activated).
	// Success criteria: The window as shown matches
	// WindowFrameSkiaTest_Active.be.skia.1x.png, ..._Pressed and
	// ..._Inactive within the text tolerance: BeOS R5's yellow tab (grey
	// when inactive) with the bold title, the close and the zoom button,
	// the five lines of the grey border with its resize marks, and the
	// desktop transparent beside the tab.
	// Why it matters: The frame is Haiku's BeDecorator ported line by line;
	// the references, checked by eye against Haiku's BeDecorator, pin its
	// pixels.
	//
	// Given
	TestEnvironment environment;
	auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	BApplication::SetWindowFrames(BWindowFrames::Look);
	BApplication app("application/x-vnd.ngui-window-frame-skia-test");
	auto driver = std::make_unique<Driver>(app);
	auto owned = std::make_unique<FramedWindow>();
	owned->Show();
	const BMessenger framed = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(driver->WaitForIdle().has_value());
	WindowRef window = driver->Window("Frames");
	window.PlatformWindow()->Activate();
	ASSERT_TRUE(driver->WaitForIdle().has_value());

	// When / Then
	EXPECT_IMAGE_SNAPSHOT(Shown(window), "Active",
		{.look = "be", .backend = "skia", .tolerance = Tolerance::Text()});

	// the close button pressed (the mouse stays on it)
	platform::HeadlessWindow* platformWindow = window.PlatformWindow();
	const gfx::Point offset = platformWindow->FrameLayout().contentOffset;
	BMessage down(kMouseDown);
	(void)down.AddPoint("where", {offset.x + 5, offset.y - 12});
	(void)down.AddInt32("buttons", 1);
	(void)down.AddInt32("clicks", 1);
	(void)platformWindow->Send(down);
	ASSERT_TRUE(driver->WaitForIdle().has_value());
	EXPECT_IMAGE_SNAPSHOT(Shown(window), "Pressed",
		{.look = "be", .backend = "skia", .tolerance = Tolerance::Text()});
	BMessage up(kMouseMoved);
	(void)up.AddPoint("where", {offset.x + 100, offset.y + 60});
	(void)up.AddInt32("buttons", 1);
	(void)platformWindow->Send(up);
	BMessage released(kMouseUp);
	(void)released.AddPoint("where", {offset.x + 100, offset.y + 60});
	(void)released.AddInt32("buttons", 0);
	(void)platformWindow->Send(released);

	// another window takes the focus
	auto other = std::make_unique<BWindow>(gfx::Rect{500, 500, 600, 560}, "Other",
		BWindowLook::Titled);
	other->Show();
	const BMessenger otherWindow = *BLooper::Run(std::move(other));
	ASSERT_TRUE(driver->WaitForIdle().has_value());
	driver->Window("Other").PlatformWindow()->Activate();
	ASSERT_TRUE(driver->WaitForIdle().has_value());
	EXPECT_IMAGE_SNAPSHOT(Shown(window), "Inactive",
		{.look = "be", .backend = "skia", .tolerance = Tolerance::Text()});

	(void)otherWindow.SendMessage(kQuitRequested);
	(void)framed.SendMessage(kQuitRequested);
	(void)otherWindow.WaitForLooperQuit(5s);
	(void)framed.WaitForLooperQuit(5s);
	driver.reset();
	BApplication::SetWindowFrames(BWindowFrames::System);
	TextSystem::SetDefault(nullptr);
}


TEST(WindowFrameSkiaTest, TheHaikuAndFlatFramesMatchTheirSnapshots)
{
	// What is tested: The window "Frames" of TheBeFrameMatchesItsSnapshot
	// with the frames of the look, in the looks "haiku" and "flat": active,
	// then inactive (another window is activated).
	// Success criteria: Each matches WindowFrameSkiaTest_Active.<look>... and
	// _Inactive.<look>.skia.1x.png within the text tolerance: Haiku's
	// DefaultDecorator (the tab with its gradient, the blended square
	// buttons, the frame's five shades) and the FlatDecorator (a flat tab
	// that runs into the border).
	// Why it matters: The frames are Haiku's decorators ported line by line;
	// the references, checked by eye against Haiku, pin their pixels.
	//
	// Given
	TestEnvironment environment;
	auto text = std::move(*text::FontTextSystem::CreateBundled());
	TextSystem::SetDefault(text.get());
	(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	BApplication::SetWindowFrames(BWindowFrames::Look);

	for (const char* look : {"haiku", "flat"}) {
		(void)LookRegistry::Default().SetCurrent(look);
		BApplication app("application/x-vnd.ngui-window-frame-skia-test");
		auto driver = std::make_unique<Driver>(app);
		auto owned = std::make_unique<FramedWindow>();
		owned->Show();
		const BMessenger framed = *BLooper::Run(std::move(owned));
		ASSERT_TRUE(driver->WaitForIdle().has_value());
		WindowRef window = driver->Window("Frames");
		window.PlatformWindow()->Activate();
		ASSERT_TRUE(driver->WaitForIdle().has_value());

		// When / Then
		EXPECT_IMAGE_SNAPSHOT(Shown(window), "Active",
			{.look = look, .backend = "skia", .tolerance = Tolerance::Text()});

		auto other = std::make_unique<BWindow>(gfx::Rect{500, 500, 600, 560}, "Other",
			BWindowLook::Titled);
		other->Show();
		const BMessenger otherWindow = *BLooper::Run(std::move(other));
		ASSERT_TRUE(driver->WaitForIdle().has_value());
		driver->Window("Other").PlatformWindow()->Activate();
		ASSERT_TRUE(driver->WaitForIdle().has_value());
		EXPECT_IMAGE_SNAPSHOT(Shown(window), "Inactive",
			{.look = look, .backend = "skia", .tolerance = Tolerance::Text()});

		(void)otherWindow.SendMessage(kQuitRequested);
		(void)framed.SendMessage(kQuitRequested);
		(void)otherWindow.WaitForLooperQuit(5s);
		(void)framed.WaitForLooperQuit(5s);
		driver.reset();
	}

	(void)LookRegistry::Default().SetCurrent("be");
	BApplication::SetWindowFrames(BWindowFrames::System);
	TextSystem::SetDefault(nullptr);
}


} // namespace
