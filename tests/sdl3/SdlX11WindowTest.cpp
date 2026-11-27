/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The SDL platform's windows on X11 as the window manager sees them
// (WM_NORMAL_HINTS). Needs an X server (Xvfb in the CI); without one the
// test is skipped.


#include <chrono>
#include <cstring>
#include <memory>
#include <thread>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>

#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"

// after NewGUI's headers: Xlib defines Bool, Status, None and others as
// macros
#include <X11/Xlib.h>
#include <X11/Xutil.h>


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


TEST(SdlX11WindowTest, OneLimitedSideLeavesTheOtherFree)
{
	// What is tested: SDL's X11 driver; a window "Limited" whose width is
	// limited to 200 to 300 and whose height only from below (at least 100,
	// no maximum), as a layout of fixed width gives it; its WM_NORMAL_HINTS
	// as the window manager reads them.
	// Success criteria: The hints have a minimum of 200 × 100 and a maximum
	// of 300 × 32767: the height without limit is X11's largest side, not 0.
	// Skipped without an X server.
	// Why it matters: SDL writes 0 for the side without limit when the
	// other has one; window managers take it as 0, and GNOME's Mutter kept
	// such windows at their minimum height: neither the mouse nor Zoom()
	// could make them taller.
	//
	// Given
	Display* display = XOpenDisplay(nullptr);
	if (display == nullptr)
		GTEST_SKIP() << "no X server";
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	RunningApp app;
	if (app.Platform() == nullptr) {
		XCloseDisplay(display);
		GTEST_SKIP() << "no X11 video";
	}
	auto owned = std::make_unique<BWindow>(gfx::Rect{100, 100, 350, 250}, "Limited",
		BWindowLook::Titled);
	owned->SetSizeLimits(200, 300, 100, kSizeUnlimited);
	owned->Show();
	const BMessenger window = *BLooper::Run(std::move(owned));

	// When
	Window handle = 0;
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (handle == 0 && std::chrono::steady_clock::now() < deadline) {
		handle = app.OnMain([] {
			Window found = 0;
			int count = 0;
			SDL_Window** windows = SDL_GetWindows(&count);
			for (int i = 0; windows != nullptr && i < count; i++) {
				const char* title = SDL_GetWindowTitle(windows[i]);
				if (title != nullptr && std::strcmp(title, "Limited") == 0) {
					found = static_cast<Window>(SDL_GetNumberProperty(
						SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
						0));
				}
			}
			SDL_free(windows);
			return found;
		});
		if (handle == 0)
			std::this_thread::sleep_for(20ms);
	}
	XSizeHints hints{};
	long supplied = 0;
	Status read = 0;
	// the limits reach the window from the main thread a little later
	while (handle != 0 && std::chrono::steady_clock::now() < deadline) {
		read = XGetWMNormalHints(display, handle, &hints, &supplied);
		if (read != 0 && (hints.flags & PMaxSize) != 0 && hints.max_width == 300)
			break;
		std::this_thread::sleep_for(20ms);
	}
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	XCloseDisplay(display);

	// Then
	ASSERT_NE(handle, 0u);
	ASSERT_NE(read, 0);
	ASSERT_NE(hints.flags & PMaxSize, 0);
	EXPECT_EQ(hints.min_width, 200);
	EXPECT_EQ(hints.min_height, 100);
	EXPECT_EQ(hints.max_width, 300);
	EXPECT_EQ(hints.max_height, 32767);
}


} // namespace
