/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// NSAccessibility on macOS through AccessKit's adapter (toolkit scope 11.3):
// with SDL's Cocoa driver, whose windows are NSWindows. Cocoa wants the
// process's main thread, so the application runs on the test's own thread
// and the test steps on another one; without a window server (a machine
// without a login session) the test is skipped.


#include <ngui/platform/SdlPlatform.h>

#import <AppKit/AppKit.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Button.h>
#include <ngui/Clock.h>
#include <ngui/Looper.h>
#include <ngui/Messenger.h>
#include <ngui/Window.h>

#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kPressed = 0x70726573;	// 'pres'


// A window "Accessible" with the button "Press me", counting its presses.
class ButtonWindow : public BWindow {
public:
	explicit ButtonWindow(std::atomic<int>& presses)
		:
		BWindow({200, 200, 420, 300}, "Accessible", BWindowLook::Titled),
		fPresses(presses)
	{
		AddChild(std::make_unique<BButton>(gfx::Rect{20, 20, 200, 60}, "press", "Press me",
			std::make_unique<BMessage>(kPressed)));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kPressed) {
			fPresses++;
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	std::atomic<int>& fPresses;
};


// The first element below \a element (depth first, at most \a depth levels)
// that is a button named \a name.
id
FindButton(id element, NSString* name, int depth)
{
	if (depth < 0 || element == nil)
		return nil;
	if ([element respondsToSelector:@selector(accessibilityRole)]
		&& [[element accessibilityRole] isEqualToString:NSAccessibilityButtonRole]
		&& (([element respondsToSelector:@selector(accessibilityTitle)]
				&& [[element accessibilityTitle] isEqual:name])
			|| ([element respondsToSelector:@selector(accessibilityLabel)]
				&& [[element accessibilityLabel] isEqual:name]))) {
		return element;
	}
	if (![element respondsToSelector:@selector(accessibilityChildren)])
		return nil;
	for (id child in [element accessibilityChildren]) {
		if (id found = FindButton(child, name, depth - 1))
			return found;
	}
	return nil;
}


TEST(SdlCocoaAccessibilityTest, VoiceOverFindsAndPressesAButton)
{
	// What is tested: An application on the SDL platform with SDL's Cocoa
	// driver, a window with the button "Press me"; its content view asked
	// for its accessibility children as VoiceOver asks (which starts
	// AccessKit's adapter), until the window's tree arrives; the button
	// found there pressed through NSAccessibility.
	// Success criteria: The content view has an element with the role
	// NSAccessibilityButtonRole named "Press me"; accessibilityPerformPress
	// on it succeeds and the button sends its message once. Skipped
	// without a window server.
	// Why it matters: On macOS screen readers see and work an application
	// through NSAccessibility; AccessKit's adapter must hang the window's
	// tree into the NSWindow SDL made, and its actions must reach the
	// views.
	//
	// Given
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "cocoa");
	auto platform = SdlPlatform::Create();
	if (!platform.has_value())
		GTEST_SKIP() << "no Cocoa video: " << platform.error().message;
	SdlPlatform* raw = platform->get();
	BApplication app("application/x-vnd.ngui-cocoa-test");
	app.SetPulseRate(Clock::Time::zero());
	(void)app.UsePlatform(std::move(*platform));
	std::atomic<int> presses = 0;
	bool found = false;
	bool pressed = false;

	// When
	std::thread steps([&] {
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		while (!AppMessenger().IsValid() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		auto window = std::make_unique<ButtonWindow>(presses);
		window->Show();
		(void)BLooper::Run(std::move(window));
		// on the main thread, as AppKit wants: until the tree is there
		const auto onMain = [raw](std::function<void()> work) {
			std::promise<void> done;
			raw->RunOnMainThread([&work, &done] {
				work();
				done.set_value();
			});
			done.get_future().wait();
		};
		while (!found && std::chrono::steady_clock::now() < deadline) {
			onMain([&] {
				for (NSWindow* nsWindow in [NSApp windows]) {
					if (![[nsWindow title] isEqualToString:@"Accessible"])
						continue;
					if (id button = FindButton([nsWindow contentView], @"Press me", 8)) {
						found = true;
						pressed = [button accessibilityPerformPress];
					}
				}
			});
			if (!found)
				std::this_thread::sleep_for(50ms);
		}
		while (found && presses == 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		(void)AppMessenger().SendMessage(kQuitRequested);
	});
	(void)app.Run();
	steps.join();

	// Then
	EXPECT_TRUE(found);
	EXPECT_TRUE(pressed);
	EXPECT_EQ(presses, 1);
}


} // namespace
