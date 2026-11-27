/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The key menu bar in a menu bar of the system (macOS, module P3), on the
// headless platform acting as one that has it (Options::systemMenuBar).


#include <ngui/MenuBar.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Menu.h>
#include <ngui/MenuItem.h>
#include <ngui/SeparatorItem.h>
#include <ngui/Window.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/test/Driver.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOpen = 0x6f70656e;		// 'open'
constexpr std::uint32_t kQuitItem = 0x71756974;	// 'quit'


// A window "Menus" with the menu bar "bar": File (Open with Command+O, a
// separator, the submenu Recent with "a.txt" marked, Quit disabled) and
// Edit (Undo with Command+Shift+Z). MenusBeginning() enables Quit.
class MenuWindow : public BWindow {
public:
	MenuWindow(std::atomic<int>& opened, std::atomic<int>& beginnings,
		std::atomic<int>& endings)
		:
		BWindow({100, 100, 400, 300}, "Menus", BWindowLook::Titled),
		fOpened(opened),
		fBeginnings(beginnings),
		fEndings(endings)
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
		auto quit = std::make_unique<BMenuItem>("Quit", std::make_unique<BMessage>(kQuitItem));
		quit->SetEnabled(false);
		fQuit = quit.get();
		file->AddItem(std::move(quit));
		bar->AddItem(std::move(file));
		auto edit = std::make_unique<BMenu>("Edit");
		auto undo = std::make_unique<BMenuItem>("Undo", nullptr);
		undo->SetShortcut('Z', BModifier::Shift);
		edit->AddItem(std::move(undo));
		bar->AddItem(std::move(edit));
		AddChild(std::move(bar));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kOpen) {
			fOpened++;
			return;
		}
		BWindow::MessageReceived(message);
	}

	void MenusBeginning() override
	{
		fBeginnings++;
		fQuit->SetEnabled(true);
	}

	void MenusEnded() override
	{
		fEndings++;
	}

private:
	std::atomic<int>& fOpened;
	std::atomic<int>& fBeginnings;
	std::atomic<int>& fEndings;
	BMenuItem* fQuit = nullptr;
};


// The entry labelled \a label among \a items, or an empty one.
SystemMenuItem
Entry(const std::vector<SystemMenuItem>& items, const std::string& label)
{
	for (const SystemMenuItem& item : items) {
		if (item.label == label)
			return item;
	}
	return {};
}


TEST(SystemMenuBarTest, TheKeyMenuBarGoesIntoTheSystemMenuBar)
{
	// What is tested: A window with a menu bar on a platform with a menu bar
	// of the system: the menus it passes on; the entry "Open" chosen there,
	// as a user does (menus open, entry chosen, menus close); an id that
	// names no item; then SetUsesSystemMenuBar(false).
	// Success criteria: The menus are File and Edit; File has Open with the
	// shortcut O, a separator, Recent with "a.txt" marked, Quit disabled;
	// Edit has Undo with Z and Shift. The bar is hidden in the window.
	// Choosing Open sends its message once, MenusBeginning() and
	// MenusEnded() run once, and the menus passed on again have Quit
	// enabled (as MenusBeginning() made it). An unknown id does nothing.
	// Without the system's menu bar the bar shows in the window again and
	// no menus are passed on.
	// Why it matters: Mac programs have their menus in the menu bar at the
	// top of the screen (module P3); the items must look and work as in the
	// window, and the program's hooks must run as for its own menus.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-system-menu-test");
	ASSERT_TRUE(app.UsePlatform(std::make_unique<HeadlessPlatform>(
		HeadlessPlatform::Options{.systemMenuBar = true})).has_value());
	Driver driver(app);
	std::atomic<int> opened = 0;
	std::atomic<int> beginnings = 0;
	std::atomic<int> endings = 0;
	auto owned = std::make_unique<MenuWindow>(opened, beginnings, endings);
	owned->Show();
	const BMessenger messenger = *BLooper::Run(std::move(owned));
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef window = driver.Window("Menus");
	HeadlessWindow* platform = window.PlatformWindow();
	ASSERT_NE(platform, nullptr);

	// When
	const std::vector<SystemMenuItem> menus = platform->SystemMenuBar();
	const bool barVisible = window.Find("bar").IsVisible();
	const SystemMenuItem open = Entry(Entry(menus, "File").items, "Open");
	ASSERT_TRUE(platform->ChooseSystemMenuItem(open.id).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::vector<SystemMenuItem> afterOpening = platform->SystemMenuBar();
	ASSERT_TRUE(platform->ChooseSystemMenuItem(4711).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const int openedOnce = opened;
	(void)window.Find("bar").Run([](BView& view) {
		static_cast<BMenuBar&>(view).SetUsesSystemMenuBar(false);
	});
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const bool barBack = window.Find("bar").IsVisible();
	const std::vector<SystemMenuItem> without = platform->SystemMenuBar();
	ASSERT_TRUE(messenger.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_EQ(menus.size(), 2u);
	EXPECT_EQ(menus[0].label, "File");
	EXPECT_EQ(menus[1].label, "Edit");
	const std::vector<SystemMenuItem>& file = menus[0].items;
	ASSERT_EQ(file.size(), 4u);
	EXPECT_EQ(file[0].label, "Open");
	EXPECT_EQ(file[0].shortcut, static_cast<std::uint32_t>('O'));
	EXPECT_TRUE(file[0].enabled);
	EXPECT_TRUE(file[1].separator);
	EXPECT_EQ(file[2].label, "Recent");
	ASSERT_EQ(file[2].items.size(), 1u);
	EXPECT_EQ(file[2].items[0].label, "a.txt");
	EXPECT_TRUE(file[2].items[0].marked);
	EXPECT_EQ(file[3].label, "Quit");
	EXPECT_FALSE(file[3].enabled);
	const SystemMenuItem undo = Entry(menus[1].items, "Undo");
	EXPECT_EQ(undo.shortcut, static_cast<std::uint32_t>('Z'));
	EXPECT_TRUE(undo.modifiers.Has(BModifier::Shift));
	EXPECT_FALSE(barVisible);
	EXPECT_NE(open.id, 0u);
	EXPECT_EQ(openedOnce, 1);
	EXPECT_EQ(beginnings, 2);
	EXPECT_EQ(endings, 2);
	EXPECT_TRUE(Entry(Entry(afterOpening, "File").items, "Quit").enabled);
	EXPECT_TRUE(barBack);
	EXPECT_TRUE(without.empty());
}


TEST(SystemMenuBarTest, WithoutASystemMenuBarTheBarStaysInTheWindow)
{
	// What is tested: The same window on a platform without a menu bar of
	// the system (Linux, Windows; the headless platform by default).
	// Success criteria: The bar shows in the window and no menus are passed
	// on.
	// Why it matters: Only macOS has such a menu bar; elsewhere the menus
	// must stay where the program put them.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-no-system-menu-test");
	Driver driver(app);
	std::atomic<int> opened = 0;
	std::atomic<int> beginnings = 0;
	std::atomic<int> endings = 0;
	auto owned = std::make_unique<MenuWindow>(opened, beginnings, endings);
	owned->Show();
	const BMessenger messenger = *BLooper::Run(std::move(owned));

	// When
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef window = driver.Window("Menus");
	const bool visible = window.Find("bar").IsVisible();
	const std::size_t updates = window.PlatformWindow()->CountSystemMenuBarUpdates();
	ASSERT_TRUE(messenger.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_TRUE(visible);
	EXPECT_EQ(updates, 0u);
}


} // namespace
