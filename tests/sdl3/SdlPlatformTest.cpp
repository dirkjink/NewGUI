/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/SdlPlatform.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>

#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


// An application with the SDL platform, run on a thread of the test as on
// a main thread. SDL is initialized on that thread, as it must be; the
// test thread uses the platform as any other thread of a program would.
class RunningApp {
public:
	RunningApp()
	{
		std::promise<SdlPlatform*> started;
		fThread = std::thread([this, &started] {
			auto platform = SdlPlatform::Create();
			if (!platform.has_value()) {
				started.set_value(nullptr);
				return;
			}
			SdlPlatform* raw = platform->get();
			BApplication app("application/x-vnd.ngui-sdl-test");
			app.SetPulseRate(Clock::Time::zero());
			(void)app.UsePlatform(std::move(*platform));
			started.set_value(raw);
			(void)app.Run();
		});
		fPlatform = started.get_future().get();
	}

	~RunningApp()
	{
		if (fPlatform != nullptr) {
			const BMessenger app = WaitForApp();
			(void)app.SendMessage(kQuitRequested);
		}
		fThread.join();
	}

	SdlPlatform* Platform() const
	{
		return fPlatform;
	}

	// Runs \a work on the main thread and waits for it.
	template<typename Work>
	auto OnMain(Work work) -> decltype(work())
	{
		std::packaged_task<decltype(work())()> task(std::move(work));
		auto result = task.get_future();
		fPlatform->RunOnMainThread([&task] { task(); });
		return result.get();
	}

private:
	static BMessenger WaitForApp()
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (!AppMessenger().IsValid() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		return AppMessenger();
	}

	std::thread fThread;
	SdlPlatform* fPlatform = nullptr;
};


TEST(SdlPlatformTest, ItRunsWithoutADisplay)
{
	// What is tested: SdlPlatform with SDL's offscreen video driver (set by
	// CTest), on the main thread of a running application; Displays() from
	// another thread.
	// Success criteria: The platform starts with the driver "offscreen"; it
	// reports at least one screen, the primary one first.
	// Why it matters: The tests of the platform and later of windows run
	// in CI without a display (specification 20.1).
	//
	// Given
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);

	// When
	const std::vector<DisplayInfo> displays = app.Platform()->Displays();

	// Then
	EXPECT_EQ(app.Platform()->Name(), "sdl3");
	EXPECT_EQ(app.Platform()->VideoDriver(), "offscreen");
	ASSERT_FALSE(displays.empty());
	EXPECT_TRUE(displays.front().primary);
	EXPECT_GT(displays.front().frame.Width(), 0);
}


TEST(SdlPlatformTest, WindowsShowWhatIsPresented)
{
	// What is tested: A window created from another thread, its geometry,
	// Present() of a red frame and then of a blue area in it, read back
	// from SDL's window surface; and the close button, pushed as an SDL
	// event.
	// Success criteria: The window has the frame of its spec at scale 1;
	// after the first present the surface is red, after the second the
	// dirty area is blue and the rest stays red; the close button reaches
	// the window's target as kQuitRequested.
	// Why it matters: This is the path of every frame (specification 12:
	// CPU surface, presenter, window) and of the window's events.
	//
	// Given
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	// The events go to a handler, not to the looper itself: kQuitRequested
	// to a looper would quit it.
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<RecordingLooper>(record);
	RecordingHandler handler("window", record);
	owned->AddHandler(&handler);
	WindowSpec spec;
	spec.title = "test";
	spec.frame = {10, 20, 40, 30};
	spec.target = BMessenger(&handler);
	const BMessenger looper = *BLooper::Run(std::move(owned));
	std::unique_ptr<PlatformWindow> window = *app.Platform()->CreateWindow(spec);
	window->Show();
	const std::int32_t width = 30;
	const std::int32_t height = 10;
	const auto frame = [&](std::uint8_t b, std::uint8_t g, std::uint8_t r) {
		std::vector<std::byte> pixels(static_cast<std::size_t>(width * height * 4));
		for (std::size_t i = 0; i < pixels.size(); i += 4) {
			pixels[i] = std::byte{b};
			pixels[i + 1] = std::byte{g};
			pixels[i + 2] = std::byte{r};
			pixels[i + 3] = std::byte{255};
		}
		return pixels;
	};
	// The SDL window behind it: the only one there is.
	const auto sdlWindow = [] {
		int count = 0;
		SDL_Window** windows = SDL_GetWindows(&count);
		SDL_Window* first = count > 0 ? windows[0] : nullptr;
		SDL_free(windows);
		return first;
	};
	const auto pixelAt = [&](int x, int y) {
		return app.OnMain([&] {
			SDL_Window* shown = sdlWindow();
			SDL_Surface* surface = shown != nullptr ? SDL_GetWindowSurface(shown) : nullptr;
			std::array<std::uint8_t, 3> rgb{};
			if (surface != nullptr)
				SDL_ReadSurfacePixel(surface, x, y, &rgb[0], &rgb[1], &rgb[2], nullptr);
			return rgb;
		});
	};

	// When
	const std::vector<std::byte> red = frame(0, 0, 255);
	const std::array<gfx::IntRect, 1> all = {{{0, 0, width, height}}};
	ASSERT_TRUE(window->Present({width, height, width * 4u, gfx::PixelFormat::Bgra8Premul, red},
		all).has_value());
	const auto first = pixelAt(5, 5);
	const std::vector<std::byte> blue = frame(255, 0, 0);
	const std::array<gfx::IntRect, 1> part = {{{0, 0, 10, 10}}};
	ASSERT_TRUE(window->Present({width, height, width * 4u, gfx::PixelFormat::Bgra8Premul,
		blue}, part).has_value());
	const auto inside = pixelAt(5, 5);
	const auto outside = pixelAt(20, 5);
	app.OnMain([&] {
		SDL_Event close{};
		close.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
		close.window.windowID = SDL_GetWindowID(sdlWindow());
		SDL_PushEvent(&close);
		return 0;
	});
	const bool closed = record->WaitForEntry("window:" + std::to_string(kQuitRequested), 1);

	// Then
	EXPECT_EQ(window->Frame(), (gfx::Rect{10, 20, 40, 30}));
	EXPECT_EQ(window->Scale(), 1);
	EXPECT_EQ(first, (std::array<std::uint8_t, 3>{255, 0, 0}));
	EXPECT_EQ(inside, (std::array<std::uint8_t, 3>{0, 0, 255}));
	EXPECT_EQ(outside, (std::array<std::uint8_t, 3>{255, 0, 0}));
	EXPECT_TRUE(closed);
	window.reset();
	(void)looper.SendMessage(kQuitRequested);
	(void)looper.WaitForLooperQuit(5s);
}


TEST(SdlPlatformTest, PopupsArePlacedRelativeToTheirParent)
{
	// What is tested: A window at (10, 20, 40, 30) and a popup of it at
	// (25, 35, 45, 45) on the desktop, created from another thread; the
	// popup moved to (30, 40).
	// Success criteria: SDL makes the popup a popup menu window whose
	// parent is the window, 15 pixels right of and below the window's
	// content area (SDL places popups relative to their parent); its frame
	// is reported on the desktop. Moved, it is at (20, 20) relative to the
	// window and at (30, 40, 50, 50) on the desktop.
	// Why it matters: Menus are popups of their window (W3b), also under
	// Wayland, where only relative placement exists.
	//
	// Given
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	WindowSpec spec;
	spec.title = "test";
	spec.frame = {10, 20, 40, 30};
	std::unique_ptr<PlatformWindow> window = *app.Platform()->CreateWindow(spec);
	WindowSpec popupSpec;
	popupSpec.frame = {25, 35, 45, 45};
	popupSpec.parent = window.get();
	popupSpec.popup = true;
	popupSpec.decorated = false;
	popupSpec.resizable = false;

	// When
	auto created = app.Platform()->CreateWindow(popupSpec);
	if (!created.has_value())
		GTEST_SKIP() << "the video driver has no popups: " << created.error().message;
	std::unique_ptr<PlatformWindow> popup = std::move(*created);
	const gfx::Rect frame = popup->Frame();
	const auto sdlState = [&] {
		return app.OnMain([] {
			int count = 0;
			SDL_Window** windows = SDL_GetWindows(&count);
			SDL_Window* found = nullptr;
			for (int i = 0; i < count; i++) {
				if ((SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_POPUP_MENU) != 0)
					found = windows[i];
			}
			SDL_free(windows);
			std::array<int, 3> state{-1, -1, 0};
			if (found != nullptr) {
				SDL_GetWindowPosition(found, &state[0], &state[1]);
				state[2] = SDL_GetWindowParent(found) != nullptr ? 1 : 0;
			}
			return state;
		});
	};
	const std::array<int, 3> before = sdlState();
	popup->MoveTo({30, 40});
	const std::array<int, 3> after = sdlState();
	const gfx::Rect movedFrame = popup->Frame();
	popup.reset();
	window.reset();

	// Then
	EXPECT_EQ(frame, (gfx::Rect{25, 35, 45, 45}));
	EXPECT_EQ(before, (std::array<int, 3>{15, 15, 1}));
	EXPECT_EQ(after, (std::array<int, 3>{20, 20, 1}));
	EXPECT_EQ(movedFrame, (gfx::Rect{30, 40, 50, 50}));
}


} // namespace
