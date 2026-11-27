/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The frames NewGUI draws on SDL (BWindowFrames::Look): SDL's hit test from
// the frame's layout, and on X11 transparent windows only with a
// compositing manager (the test plays one by owning _NET_WM_CM_S<screen>).
// Needs an X server (Xvfb in the CI) for the window test; without one it is
// skipped.


#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>

#include "platform/sdl3/FrameHit.h"
#include "platform/sdl3/X11DragDrop.h"
#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"

// after NewGUI's headers: Xlib defines Bool, Status, None and others as
// macros
#include <X11/Xlib.h>


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


TEST(SdlX11FramesTest, TheFrameTellsSdlWhatAPressDoes)
{
	// What is tested: SDL's hit test (sdl3::HitOf()) for a frame layout with
	// a button at {5, 5, 18, 18}, the resize corner at {180, 120, 205, 145}
	// and a tab at {0, 0, 110, 24} and borders that move; points on the
	// button, the corner, the tab, a border and the content.
	// Success criteria: The button and the content are SDL_HITTEST_NORMAL
	// (the window gets the press), the corner SDL_HITTEST_RESIZE_BOTTOMRIGHT,
	// the tab and the border SDL_HITTEST_DRAGGABLE; the first area that
	// contains a point counts (the button lies on the tab).
	// Why it matters: Under Wayland only the window system moves and resizes
	// windows; it does so by this answer, and the buttons must stay the
	// window's.
	//
	// Given
	WindowFrameLayout layout;
	layout.contentOffset = {5, 24};
	layout.contentSize = {200, 100};
	layout.areas = {{{5, 5, 18, 18}, WindowFrameHit::Normal},
		{{180, 120, 210, 129}, WindowFrameHit::ResizeBottomRight},
		{{0, 0, 110, 24}, WindowFrameHit::Move},
		{{0, 19, 210, 24}, WindowFrameHit::Move},
		{{0, 124, 210, 129}, WindowFrameHit::Move}};

	// When / Then
	EXPECT_EQ(sdl3::HitOf(layout, {10, 10}), SDL_HITTEST_NORMAL);
	EXPECT_EQ(sdl3::HitOf(layout, {200, 126}), SDL_HITTEST_RESIZE_BOTTOMRIGHT);
	EXPECT_EQ(sdl3::HitOf(layout, {60, 10}), SDL_HITTEST_DRAGGABLE);
	EXPECT_EQ(sdl3::HitOf(layout, {150, 21}), SDL_HITTEST_DRAGGABLE);
	EXPECT_EQ(sdl3::HitOf(layout, {100, 70}), SDL_HITTEST_NORMAL);
}


// The SDL window of the title \a title, nullptr if there is none.
SDL_Window*
WindowTitled(const char* title)
{
	int count = 0;
	SDL_Window** windows = SDL_GetWindows(&count);
	SDL_Window* found = nullptr;
	for (int i = 0; windows != nullptr && i < count; i++) {
		const char* name = SDL_GetWindowTitle(windows[i]);
		if (name != nullptr && std::strcmp(name, title) == 0)
			found = windows[i];
	}
	SDL_free(windows);
	return found;
}


TEST(SdlX11FramesTest, OnlyACompositorMakesTheFrameTheLooks)
{
	// What is tested: SDL's X11 driver on an X server without a compositing
	// manager, then while the test owns _NET_WM_CM_S<screen> as one does; an
	// application asking for the frames of the look makes a titled window
	// "Framed" each time.
	// Success criteria: Without a compositor X11HasCompositor() is false and
	// the window keeps the system's frame (not borderless, not
	// transparent); with one it is true and the window is borderless and
	// transparent, its content inside the look's frame. Skipped without an X
	// server.
	// Why it matters: Without a compositor a transparent window shows black
	// beside the tab; there the window system's frame is the better one
	// (as decided for this block).
	//
	// Given
	Display* display = XOpenDisplay(nullptr);
	if (display == nullptr)
		GTEST_SKIP() << "no X server";
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	BApplication::SetWindowFrames(BWindowFrames::Look);
	const auto flagsOf = [](RunningApp& app) -> SDL_WindowFlags {
		auto owned = std::make_unique<BWindow>(gfx::Rect{100, 100, 300, 200}, "Framed",
			BWindowLook::Titled);
		owned->Show();
		const BMessenger window = *BLooper::Run(std::move(owned));
		SDL_WindowFlags flags = 0;
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (flags == 0 && std::chrono::steady_clock::now() < deadline) {
			flags = app.OnMain([] {
				SDL_Window* sdl = WindowTitled("Framed");
				return sdl != nullptr ? SDL_GetWindowFlags(sdl) : SDL_WindowFlags(0);
			});
			if (flags == 0)
				std::this_thread::sleep_for(20ms);
		}
		(void)window.SendMessage(kQuitRequested);
		(void)window.WaitForLooperQuit(5s);
		return flags;
	};

	// When
	const bool before = platform::sdl3::X11HasCompositor();
	SDL_WindowFlags plain = 0;
	{
		RunningApp app;
		if (app.Platform() == nullptr) {
			BApplication::SetWindowFrames(BWindowFrames::System);
			XCloseDisplay(display);
			GTEST_SKIP() << "no X11 video";
		}
		plain = flagsOf(app);
	}
	const std::string name = "_NET_WM_CM_S" + std::to_string(DefaultScreen(display));
	const Window owner = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1,
		0, 0, 0);
	(void)XSetSelectionOwner(display, XInternAtom(display, name.c_str(), False), owner,
		CurrentTime);
	(void)XSync(display, False);
	const bool after = platform::sdl3::X11HasCompositor();
	SDL_WindowFlags framed = 0;
	{
		RunningApp app;
		framed = flagsOf(app);
	}
	BApplication::SetWindowFrames(BWindowFrames::System);
	XDestroyWindow(display, owner);
	XCloseDisplay(display);

	// Then
	EXPECT_FALSE(before);
	ASSERT_NE(plain, 0u);
	EXPECT_EQ(plain & (SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT), 0u);
	EXPECT_TRUE(after);
	ASSERT_NE(framed, 0u);
	EXPECT_EQ(framed & (SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT),
		SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT);
}


} // namespace
