/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/LookRegistry.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Button.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>

#include "look/StubLook.h"
#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// Makes "be" current again at the end of a test, whatever happened.
class BackToBe {
public:
	BackToBe() = default;

	~BackToBe()
	{
		(void)LookRegistry::Default().SetCurrent("be");
	}

	BackToBe(const BackToBe&) = delete;
	BackToBe& operator=(const BackToBe&) = delete;
};


// A window of 120 × 60 with a button "ok", on the pixel backend; its
// backend in \a backend, if given, to count the frames it renders.
BMessenger
OpenWindow(std::string_view title, PixelBackend** backend = nullptr)
{
	auto window = std::make_unique<BWindow>(gfx::Rect{0, 0, 120, 60}, title,
		BWindowLook::Titled);
	auto pixels = std::make_unique<PixelBackend>();
	if (backend != nullptr)
		*backend = pixels.get();
	(void)window->UseBackend(std::move(pixels));
	window->AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "ok", "OK",
		nullptr));
	window->Show();
	return *BLooper::Run(std::move(window));
}


// The id of the look \a window draws with now.
std::string
LookOf(const WindowRef& window)
{
	std::string id;
	(void)window.Find("ok").Run([&](BView& view) {
		id = std::string(view.Window()->Look().Info().id);
	});
	return id;
}


// SetControlLook(\a id) on \a window's thread.
std::expected<void, Error>
SetOwnLook(const WindowRef& window, std::string_view id)
{
	std::expected<void, Error> result;
	(void)window.Find("ok").Run([&](BView& view) {
		result = view.Window()->SetControlLook(id);
	});
	return result;
}


void
Close(const BMessenger& window)
{
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
}


TEST(LookSwitchTest, TheLookChangesInRunningWindows)
{
	// What is tested: Two running windows A and B while the current look
	// changes from "be" to "haiku" and back, and while A has a look of its
	// own (SetControlLook("be"), then "" again, then an unknown id).
	// Success criteria: After SetCurrent("haiku") both windows draw with
	// "haiku" and B has drawn a new frame. A's own look "be"
	// stays through SetCurrent("be") and SetCurrent("haiku") while B
	// follows; SetControlLook("") makes A follow again. An unknown id is
	// NotFound and changes nothing.
	// Why it matters: Haiku cannot change its look at run time; the toolkit
	// does it by message (LOOK_CHANGED, 18.5), and a window of its own look
	// is a live preview in a settings dialog.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	const BackToBe backToBe;
	BApplication app("application/x-vnd.ngui-look-switch-test");
	Driver driver(app);
	const BMessenger windowA = OpenWindow("A");
	PixelBackend* backendB = nullptr;
	const BMessenger windowB = OpenWindow("B", &backendB);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef a = driver.Window("A");
	WindowRef b = driver.Window("B");
	const int beRenders = backendB->renders;

	// When
	ASSERT_TRUE(LookRegistry::Default().SetCurrent("haiku").has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::string bothA = LookOf(a);
	const std::string bothB = LookOf(b);
	const int haikuRenders = backendB->renders;

	const auto own = SetOwnLook(a, "be");
	ASSERT_TRUE(LookRegistry::Default().SetCurrent("be").has_value());
	ASSERT_TRUE(LookRegistry::Default().SetCurrent("haiku").has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::string ownA = LookOf(a);
	const std::string followingB = LookOf(b);

	const auto back = SetOwnLook(a, "");
	const std::string followingA = LookOf(a);
	const auto unknown = SetOwnLook(a, "nonexistent");
	const std::string keptA = LookOf(a);

	Close(windowA);
	Close(windowB);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(bothA, "haiku");
	EXPECT_EQ(bothB, "haiku");
	EXPECT_GT(haikuRenders, beRenders);
	EXPECT_TRUE(own.has_value());
	EXPECT_EQ(ownA, "be");
	EXPECT_EQ(followingB, "haiku");
	EXPECT_TRUE(back.has_value());
	EXPECT_EQ(followingA, "haiku");
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
	EXPECT_EQ(keptA, "haiku");
}


TEST(LookSwitchTest, ALookMustFitTheBackend)
{
	// What is tested: A look that needs radial gradients (level Extended),
	// registered in the registry of the process, set as the current look
	// and as the own look of a window on a backend with the core profile.
	// Success criteria: Both are refused with NotSupported; the window keeps
	// drawing with "be".
	// Why it matters: Rule L9: a look is only activated where the backend
	// can draw it; a window has its own backend, so its own look is checked
	// against that.
	//
	// Given
	class RadialLook : public StubLook {
	public:
		LookInfo Info() const override
		{
			gfx::Caps needs;
			needs.radialGradient = true;
			return {"test-radial", "Radial", 1, ConformanceLevel::Extended, needs};
		}
	};
	(void)LookRegistry::Default().Register("test-radial",
		[] { return std::make_shared<const RadialLook>(); });
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	const BackToBe backToBe;
	BApplication app("application/x-vnd.ngui-look-fit-test");
	Driver driver(app);
	const BMessenger window = OpenWindow("Fit");
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef fit = driver.Window("Fit");

	// When
	const auto current = LookRegistry::Default().SetCurrent("test-radial");
	const auto own = SetOwnLook(fit, "test-radial");
	const std::string look = LookOf(fit);
	Close(window);
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_FALSE(current.has_value());
	EXPECT_EQ(current.error().code, ErrorCode::NotSupported);
	ASSERT_FALSE(own.has_value());
	EXPECT_EQ(own.error().code, ErrorCode::NotSupported);
	EXPECT_EQ(look, "be");
}


TEST(LookSwitchTest, SwitchingWhileWindowsDrawIsSafe)
{
	// What is tested: The current look switched 200 times between "be" and
	// "haiku" from the test thread while three windows lay out and draw for
	// each change, then "be" once more.
	// Success criteria: Nothing crashes or hangs; afterwards every window
	// draws with "be". Under ThreadSanitizer, no data race.
	// Why it matters: Specification 18.7 asks for this stress test: each
	// window takes its look at the start of a cycle (L10) while another
	// thread replaces it; the old look lives on while a cycle holds it.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	const BackToBe backToBe;
	BApplication app("application/x-vnd.ngui-look-stress-test");
	Driver driver(app);
	std::vector<BMessenger> windows;
	for (const char* title : {"One", "Two", "Three"})
		windows.push_back(OpenWindow(title));
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// When
	for (int i = 0; i < 200; i++)
		(void)LookRegistry::Default().SetCurrent(i % 2 == 0 ? "haiku" : "be");
	ASSERT_TRUE(LookRegistry::Default().SetCurrent("be").has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	std::vector<std::string> looks;
	for (const char* title : {"One", "Two", "Three"})
		looks.push_back(LookOf(driver.Window(title)));
	for (const BMessenger& window : windows)
		Close(window);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(looks, (std::vector<std::string>{"be", "be", "be"}));
}


} // namespace
