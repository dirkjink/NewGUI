/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Driver.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/Looper.h>
#include <ngui/StringView.h>
#include <ngui/TextControl.h>
#include <ngui/Window.h>

#include "support/FakePlatform.h"
#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOk = FourCharCode("OKAY");
constexpr std::uint32_t kRemoveName = FourCharCode("RMNM");


// A form: a box with an OK button that counts in a label, a check box, a
// text field, and a disabled button.
class FormWindow : public BWindow {
public:
	FormWindow()
		:
		BWindow({100, 100, 400, 300}, "Form", BWindowLook::Titled)
	{
		auto box = std::make_unique<BBox>(gfx::Rect{10, 10, 290, 190}, "box");
		box->AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "ok", "OK",
			std::make_unique<BMessage>(kOk)));
		box->AddChild(std::make_unique<BCheckBox>(gfx::Rect{10, 45, 150, 65}, "autosave",
			"Save automatically", nullptr));
		box->AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 75, 270, 100}, "name",
			"Name:", "", nullptr));
		auto off = std::make_unique<BButton>(gfx::Rect{100, 10, 180, 35}, "off", "Off",
			std::make_unique<BMessage>(kOk));
		off->SetEnabled(false);
		box->AddChild(std::move(off));
		AddChild(std::move(box));
		AddChild(std::make_unique<BStringView>(gfx::Rect{10, 195, 290, 199}, "count", "0"));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kOk) {
			fCount++;
			static_cast<BStringView*>(FindView("count"))->SetText(std::to_string(fCount));
			return;
		}
		if (message->what == kRemoveName) {
			if (BView* name = FindView("name"))
				(void)name->RemoveSelf();
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	int fCount = 0;
};


// A looper that keeps itself busy for ever.
class BusyLooper : public BLooper {
public:
	BusyLooper()
		:
		BLooper("busy")
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == FourCharCode("SPIN")) {
			(void)PostMessage(FourCharCode("SPIN"));
			return;
		}
		BLooper::MessageReceived(message);
	}
};


// Opens the form on the application's platform.
BMessenger
OpenForm()
{
	auto window = std::make_unique<FormWindow>();
	window->Show();
	return *BLooper::Run(std::move(window));
}


TEST(DriverTest, InputTakesTheWayOfRealInput)
{
	// What is tested: The driver over an application with a form: the
	// window by title; queries by name, label, path and type; a click on
	// OK, on the check box and on a disabled button; typing into the text
	// field; Tab.
	// Success criteria: Find, FindByLabel and FindByPath find the OK button,
	// at its frame in window coordinates (box at 10, 10 plus button at 10,
	// 10); FindAll finds both buttons. The click on OK makes the label count
	// 1 (the button's message reached the window); the check box is on; the
	// disabled button changes nothing. Typed "Grüße" is the text field's
	// text, and it has the focus; Tab moves the focus away from it.
	// Why it matters: Applications test their windows with the input of
	// users (specification 20.4, rules T2 and T3).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-driver-test");
	Driver driver(app);
	ASSERT_TRUE(driver.InitCheck().has_value());
	const BMessenger form = OpenForm();

	// When
	WindowRef window = driver.Window("Form");
	const ViewRef ok = window.Find<BButton>("ok");
	const ViewRef byLabel = window.FindByLabel("OK");
	const ViewRef byPath = window.FindByPath("box/ok");
	const std::vector<ViewRef> buttons = window.FindAll<BButton>();
	const std::optional<gfx::Rect> frame = ok.Frame();
	ASSERT_TRUE(driver.Click(ok).has_value());
	ASSERT_TRUE(driver.Click(window.Find<BCheckBox>("autosave")).has_value());
	ASSERT_TRUE(driver.Click(window.Find("off")).has_value());
	const ViewRef name = window.Find<BTextControl>("name");
	ASSERT_TRUE(driver.Type(name, "Grüße").has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::string typed = name.Text();
	const bool nameFocus = window.FindIf([](BView& view) { return view.IsFocus(); }).IsValid();
	ASSERT_TRUE(driver.Key(keys::kTab).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const std::string count = window.Find("count").Text();
	const std::optional<std::int32_t> autosave = window.Find("autosave").Value();
	const ViewRef focusAfterTab = window.FindIf([](BView& view) { return view.IsFocus(); });
	(void)form.SendMessage(kQuitRequested);
	(void)form.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(ok.IsValid());
	EXPECT_TRUE(byLabel.IsValid());
	EXPECT_TRUE(byPath.IsValid());
	EXPECT_EQ(buttons.size(), 2u);
	ASSERT_TRUE(frame.has_value());
	EXPECT_EQ(*frame, (gfx::Rect{20, 20, 100, 45}));
	EXPECT_EQ(count, "1");
	EXPECT_EQ(autosave, 1);
	EXPECT_EQ(typed, "Grüße");
	EXPECT_TRUE(nameFocus);
	EXPECT_NE(focusAfterTab.Name(), "name");
}


TEST(DriverTest, IdleWaitsForTimersAndReportsWhatIsBusy)
{
	// What is tested: WaitForIdle() with a looper that keeps posting to
	// itself, and with a timer of the test clock 100 ms ahead that sends
	// the form a click message; Advance() by 100 ms.
	// Success criteria: With the busy looper WaitForIdle() times out, and
	// the report names the looper "busy" and its message SPIN. The timer
	// ahead does not keep the application busy, and its message has not
	// arrived; after Advance() it has (the label counts 1).
	// Why it matters: Tests wait for the application instead of sleeping,
	// and time moves only when the test says (rules T1 and T3).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-driver-idle-test");
	Driver driver(app);
	const BMessenger form = OpenForm();
	const BMessenger busy = *BLooper::Run(std::make_unique<BusyLooper>());

	// When
	ASSERT_TRUE(busy.SendMessage(FourCharCode("SPIN")).has_value());
	const auto busyIdle = driver.WaitForIdle(50ms);
	(void)busy.SendMessage(kQuitRequested);
	(void)busy.WaitForLooperQuit(5s);
	(void)environment.Clock().Schedule(environment.Clock().Now() + 100ms,
		[form] { (void)form.SendMessage(kOk); });
	const auto aheadIdle = driver.WaitForIdle();
	const std::string before = driver.Window("Form").Find("count").Text();
	const auto advanced = driver.Advance(100ms);
	const std::string after = driver.Window("Form").Find("count").Text();
	(void)form.SendMessage(kQuitRequested);
	(void)form.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_FALSE(busyIdle.has_value());
	EXPECT_EQ(busyIdle.error().code, ErrorCode::TimedOut);
	EXPECT_NE(busyIdle.error().message.find("\"busy\""), std::string::npos)
		<< busyIdle.error().message;
	EXPECT_NE(busyIdle.error().message.find("SPIN"), std::string::npos);
	EXPECT_TRUE(aheadIdle.has_value());
	EXPECT_EQ(before, "0");
	EXPECT_TRUE(advanced.has_value());
	EXPECT_EQ(after, "1");
}


TEST(DriverTest, GoneViewsAndWindowsAnswerWithNothing)
{
	// What is tested: A view found, then removed from its window; the
	// window, then closed; a driver for an application that has another
	// platform.
	// Success criteria: The removed view no longer exists: no value, no
	// frame, no text, and a click on it is NotFound. The closed window is
	// invalid and has no title. The driver of the other application says
	// InvalidArgument.
	// Why it matters: A test must not reach into views that are gone; the
	// queries run on the window's thread and check first.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	std::optional<Error> otherError;
	{
		BApplication other("application/x-vnd.ngui-driver-other");
		ASSERT_TRUE(other.UsePlatform(std::make_unique<FakePlatform>()).has_value());
		Driver otherDriver(other);
		if (auto check = otherDriver.InitCheck(); !check.has_value())
			otherError = check.error();
	}
	BApplication app("application/x-vnd.ngui-driver-gone-test");
	Driver driver(app);
	const BMessenger form = OpenForm();
	WindowRef window = driver.Window("Form");
	const ViewRef name = window.Find("name");
	const bool existed = name.Exists();

	// When
	ASSERT_TRUE(form.SendMessage(kRemoveName).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const bool exists = name.Exists();
	const auto click = driver.Click(name);
	ASSERT_TRUE(form.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(form.WaitForLooperQuit(5s).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_TRUE(existed);
	EXPECT_FALSE(exists);
	EXPECT_FALSE(name.Value().has_value());
	EXPECT_FALSE(name.Frame().has_value());
	EXPECT_EQ(name.Text(), "");
	ASSERT_FALSE(click.has_value());
	EXPECT_EQ(click.error().code, ErrorCode::NotFound);
	EXPECT_FALSE(window.IsValid());
	EXPECT_EQ(window.Title(), "");
	ASSERT_TRUE(otherError.has_value());
	EXPECT_EQ(otherError->code, ErrorCode::InvalidArgument);
}


} // namespace
