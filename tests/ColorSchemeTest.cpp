/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The application's colors (module G6): Haiku's light and dark ui colors,
// the colors of the system's choice (BColorScheme::System) and their change
// while the application runs (kColorsUpdated, Haiku's B_COLORS_UPDATED); the
// UI colors of views, which follow them.


#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Palette.h>
#include <ngui/TextView.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/platform/HeadlessPlatform.h>
#include <ngui/test/Driver.h>

#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


gfx::Color
Rgb(int red, int green, int blue)
{
	return gfx::Color::FromRgba8(static_cast<std::uint8_t>(red),
		static_cast<std::uint8_t>(green), static_cast<std::uint8_t>(blue), 255);
}


TEST(ColorSchemeTest, TheDarkColorsAreHaikus)
{
	// What is tested: Palette::Dark() against Haiku's _kDefaultColorsDark,
	// some colors of it; IsDark() of the light and the dark palette.
	// Success criteria: The panel background is 43, 43, 43, the tab
	// 227, 73, 17, the document background black, its text 234, 234, 234,
	// the failure color 255, 40, 54; the dark palette is dark, the light one
	// not.
	// Why it matters: Dark mode shows Haiku's own dark colors, as Haiku's
	// Appearance preferences do (as decided for this block).
	//
	// Given
	const Palette& dark = Palette::Dark();

	// When / Then
	EXPECT_EQ(dark.Get(BColorWhich::PanelBackground), Rgb(43, 43, 43));
	EXPECT_EQ(dark.Get(BColorWhich::WindowTab), Rgb(227, 73, 17));
	EXPECT_EQ(dark.Get(BColorWhich::DocumentBackground), Rgb(0, 0, 0));
	EXPECT_EQ(dark.Get(BColorWhich::DocumentText), Rgb(234, 234, 234));
	EXPECT_EQ(dark.Get(BColorWhich::Failure), Rgb(255, 40, 54));
	EXPECT_TRUE(dark.IsDark());
	EXPECT_FALSE(Palette::Default().IsDark());
}


// A view that counts the kColorsUpdated it hears.
class ColorView : public BView {
public:
	ColorView(gfx::Rect frame, std::atomic<int>& updates)
		:
		BView(frame, "color view", kFollowAll, BViewFlag::WillDraw),
		fUpdates(updates)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kColorsUpdated)
			fUpdates++;
		BView::MessageReceived(message);
	}

private:
	std::atomic<int>& fUpdates;
};


// The application on the headless platform with a window "Colors": a view
// counting the colors' updates, a child of it with the parent's colors, a
// text view with the system's colors.
class ColorApp {
public:
	explicit ColorApp(SystemColorScheme system)
		:
		fApp("application/x-vnd.ngui-color-scheme-test")
	{
		TextSystem::SetDefault(&fText);
		HeadlessPlatform::Options options;
		options.colorScheme = system;
		auto owned = std::make_unique<HeadlessPlatform>(options);
		fPlatform = owned.get();
		(void)fApp.UsePlatform(std::move(owned));
		fDriver = std::make_unique<Driver>(fApp);
		auto window = std::make_unique<BWindow>(gfx::Rect{100, 100, 300, 200}, "Colors",
			BWindowLook::Titled);
		(void)window->UseBackend(std::make_unique<PixelBackend>());
		auto view = std::make_unique<ColorView>(gfx::Rect{0, 0, 200, 50}, fUpdates);
		view->SetViewUIColor(BColorWhich::PanelBackground, kDarken1Tint);
		auto child = std::make_unique<BView>(gfx::Rect{0, 0, 20, 20}, "child", kFollowLeftTop,
			BViewFlags());
		BView* childView = child.get();
		view->AddChild(std::move(child));
		window->AddChild(std::move(view));
		childView->AdoptParentColors();
		auto text = std::make_unique<BTextView>(gfx::Rect{0, 50, 200, 100}, "text",
			gfx::Rect{0, 0, 200, 50});
		window->AddChild(std::move(text));
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver->WaitForIdle();
	}

	~ColorApp()
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

	HeadlessPlatform& Platform()
	{
		return *fPlatform;
	}

	int Updates() const
	{
		return fUpdates;
	}

	// The background of the view \a name as the window would draw it now.
	std::optional<gfx::Color> ViewColorOf(const char* name)
	{
		(void)fDriver->WaitForIdle();
		std::optional<gfx::Color> color;
		(void)fDriver->Window("Colors").Find(name).Run([&color](BView& view) {
			color = view.ViewColor();
		});
		return color;
	}

private:
	TestEnvironment fEnvironment;
	FixedTextSystem fText;
	BApplication fApp;
	HeadlessPlatform* fPlatform = nullptr;
	std::unique_ptr<Driver> fDriver;
	BMessenger fWindow;
	std::atomic<int> fUpdates = 0;
};


TEST(ColorSchemeTest, UIColorsFollowTheApplicationsColors)
{
	// What is tested: A window with a view whose background is the UI color
	// panel background darkened once (kDarken1Tint), a child that adopted
	// its parent's colors (AdoptParentColors()) and a text view with the
	// system's colors; the application's colors are set to Haiku's dark
	// ones (Palette::SetCurrent()), then light again.
	// Success criteria: The view hears kColorsUpdated each time; its
	// background, the child's and the text view's follow: the panel color
	// darkened of the dark palette, then of the light one; the text view's
	// is the document background of each.
	// Why it matters: Views keep the colors' roles, not the colors, as
	// Haiku's ui colors do; a change of the colors must reach every window
	// and view (B_COLORS_UPDATED).
	//
	// Given
	ColorApp app(SystemColorScheme::Light);
	const Palette& dark = Palette::Dark();
	const Palette& light = Palette::Default();

	// When
	Palette::SetCurrent(dark);
	const std::optional<gfx::Color> viewDark = app.ViewColorOf("color view");
	const std::optional<gfx::Color> childDark = app.ViewColorOf("child");
	const std::optional<gfx::Color> textDark = app.ViewColorOf("text");
	const int afterDark = app.Updates();
	Palette::SetCurrent(light);
	const std::optional<gfx::Color> viewLight = app.ViewColorOf("color view");
	const std::optional<gfx::Color> textLight = app.ViewColorOf("text");
	const int afterLight = app.Updates();

	// Then
	EXPECT_EQ(afterDark, 1);
	EXPECT_EQ(afterLight, 2);
	EXPECT_EQ(viewDark, TintColor(dark.Get(BColorWhich::PanelBackground), kDarken1Tint));
	EXPECT_EQ(childDark, viewDark);
	EXPECT_EQ(textDark, dark.Get(BColorWhich::DocumentBackground));
	EXPECT_EQ(viewLight, TintColor(light.Get(BColorWhich::PanelBackground), kDarken1Tint));
	EXPECT_EQ(textLight, light.Get(BColorWhich::DocumentBackground));
}


TEST(ColorSchemeTest, TheColorsFollowTheSystemsChoice)
{
	// What is tested: An application on a headless system set to dark, with
	// the colors of the system's choice (BColorScheme::System); the user
	// switches the system to light (kSystemColorsChanged); then the
	// application asks for dark itself (BColorScheme::Dark), and the system
	// stays light.
	// Success criteria: The application starts in Haiku's dark colors; after
	// the switch it shows the light ones and its window's view heard
	// kColorsUpdated; with BColorScheme::Dark it is dark again although the
	// system is light.
	// Why it matters: By default NewGUI follows the user's light or dark
	// choice of the system, also while running; a program may still choose
	// itself (as decided for this block).
	//
	// Given
	ColorApp app(SystemColorScheme::Dark);
	BApplication::SetColorScheme(BColorScheme::System);
	(void)app.Drive().WaitForIdle();
	const Palette atStart = Palette::Current();

	// When
	app.Platform().SetColorScheme(SystemColorScheme::Light);
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (Palette::Current() != Palette::Default()
		&& std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(5ms);
	}
	(void)app.Drive().WaitForIdle();
	const Palette switched = Palette::Current();
	const int updates = app.Updates();
	BApplication::SetColorScheme(BColorScheme::Dark);
	const Palette chosen = Palette::Current();

	// Then
	EXPECT_EQ(atStart, Palette::Dark());
	EXPECT_EQ(switched, Palette::Default());
	EXPECT_GE(updates, 1);
	EXPECT_EQ(chosen, Palette::Dark());
}


} // namespace
