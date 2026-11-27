/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The application's menu bar on macOS (module P3): with SDL's Cocoa driver,
// the window's key menu bar goes into NSApp's main menu. Cocoa wants the
// process's main thread, so the application runs on the test's own thread
// and the test steps on another one; without a window server the test is
// skipped.


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
#include <ngui/Clock.h>
#include <ngui/Looper.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuItem.h>
#include <ngui/Messenger.h>
#include <ngui/SeparatorItem.h>
#include <ngui/Window.h>

#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOpen = 0x6f70656e;			// 'open'
constexpr std::uint32_t kCheckBar = 0x63686b62;		// 'chkb'


// A window "Menus" with the menu bar "bar": File (Open with Command+O, a
// separator, Recent with "a.txt" marked, Quit disabled). Counts Open and
// tells whether the bar is hidden in it.
class MenuWindow : public BWindow {
public:
	MenuWindow(std::atomic<int>& opened, std::atomic<int>& hidden)
		:
		BWindow({200, 200, 500, 360}, "Menus", BWindowLook::Titled),
		fOpened(opened),
		fHidden(hidden)
	{
		auto bar = std::make_unique<BMenuBar>(gfx::Rect{0, 0, 300, 20}, "bar");
		auto file = std::make_unique<BMenu>("File");
		auto open = std::make_unique<BMenuItem>("Open", std::make_unique<BMessage>(kOpen));
		open->SetShortcut('O', {});
		file->AddItem(std::move(open));
		file->AddItem(std::make_unique<BSeparatorItem>());
		auto recent = std::make_unique<BMenu>("Recent");
		auto a = std::make_unique<BMenuItem>("a.txt", nullptr);
		a->SetMarked(true);
		recent->AddItem(std::move(a));
		file->AddItem(std::move(recent));
		auto quit = std::make_unique<BMenuItem>("Quit", nullptr);
		quit->SetEnabled(false);
		file->AddItem(std::move(quit));
		bar->AddItem(std::move(file));
		AddChild(std::move(bar));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kOpen) {
			fOpened++;
			return;
		}
		if (message->what == kCheckBar) {
			BView* bar = FindView("bar");
			fHidden = bar != nullptr && bar->IsHidden() ? 1 : 0;
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	std::atomic<int>& fOpened;
	std::atomic<int>& fHidden;
};


std::string
Utf8(NSString* string)
{
	return string != nil ? std::string([string UTF8String]) : std::string();
}


TEST(SdlCocoaMenuBarTest, TheWindowsMenusAreInTheMenuBar)
{
	// What is tested: An application on the SDL platform with SDL's Cocoa
	// driver and a window with a menu bar (File: Open with Command+O, a
	// separator, Recent with "a.txt" marked, Quit disabled); NSApp's main
	// menu once the window's menus are there; "Open" performed through it.
	// Success criteria: The main menu has the application menu first (its
	// last entry quits with Command+Q), then File with Open (key "o",
	// Command), a separator, Recent with "a.txt" checked, Quit disabled;
	// performing Open sends its message once; the bar is hidden in the
	// window. Skipped without a window server.
	// Why it matters: Mac programs have their menus at the top of the
	// screen (module P3); a program written for Haiku's menu bar must get
	// there without changes and react to its menus there.
	//
	// Given
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "cocoa");
	auto platform = SdlPlatform::Create();
	if (!platform.has_value())
		GTEST_SKIP() << "no Cocoa video: " << platform.error().message;
	SdlPlatform* raw = platform->get();
	BApplication app("application/x-vnd.ngui-cocoa-menu-test");
	app.SetPulseRate(Clock::Time::zero());
	(void)app.UsePlatform(std::move(*platform));
	std::atomic<int> opened = 0;
	std::atomic<int> hidden = -1;
	bool found = false;
	std::string quitTitle;
	std::string quitKey;
	std::string openKey;
	bool openCommand = false;
	bool separator = false;
	bool marked = false;
	bool quitDisabled = false;

	// When
	std::thread steps([&] {
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		// until Run() has made the platform the current one: the
		// application's messenger is valid before, but a window made then
		// gets no Cocoa window
		while (ngui::platform::Current() != raw && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		auto owned = std::make_unique<MenuWindow>(opened, hidden);
		owned->Show();
		const BMessenger window = *BLooper::Run(std::move(owned));
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
				NSMenu* bar = NSApp.mainMenu;
				if (bar.numberOfItems < 2 || ![[bar itemAtIndex:1].title isEqualToString:@"File"])
					return;
				found = true;
				NSMenu* application = [bar itemAtIndex:0].submenu;
				NSMenuItem* quit = [application itemAtIndex:application.numberOfItems - 1];
				quitTitle = Utf8(quit.title);
				quitKey = Utf8(quit.keyEquivalent);
				NSMenu* file = [bar itemAtIndex:1].submenu;
				NSMenuItem* open = [file itemAtIndex:0];
				openKey = Utf8(open.keyEquivalent);
				openCommand = (open.keyEquivalentModifierMask & NSEventModifierFlagCommand) != 0;
				separator = [file itemAtIndex:1].isSeparatorItem;
				marked = [[file itemAtIndex:2].submenu itemAtIndex:0].state
					== NSControlStateValueOn;
				quitDisabled = ![file itemAtIndex:3].enabled;
				[file performActionForItemAtIndex:0];
			});
			if (!found)
				std::this_thread::sleep_for(50ms);
		}
		while (found && opened == 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		(void)window.SendMessage(kCheckBar);
		while (hidden < 0 && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		(void)AppMessenger().SendMessage(kQuitRequested);
	});
	(void)app.Run();
	steps.join();

	// Then
	ASSERT_TRUE(found);
	EXPECT_TRUE(quitTitle.starts_with("Quit ")) << quitTitle;
	EXPECT_EQ(quitKey, "q");
	EXPECT_EQ(openKey, "o");
	EXPECT_TRUE(openCommand);
	EXPECT_TRUE(separator);
	EXPECT_TRUE(marked);
	EXPECT_TRUE(quitDisabled);
	EXPECT_EQ(opened, 1);
	EXPECT_EQ(hidden, 1);
}


} // namespace
