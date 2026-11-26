/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Accessibility.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/ListView.h>
#include <ngui/Looper.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/PopUpMenu.h>
#include <ngui/PropertyInfo.h>
#include <ngui/RadioButton.h>
#include <ngui/Slider.h>
#include <ngui/StatusBar.h>
#include <ngui/StringView.h>
#include <ngui/TextControl.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Lint.h>
#include <ngui/platform/HeadlessPlatform.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kSaved = FourCharCode("SAVD");


// A settings window with one of each kind of view: a box of options, a
// slider, text fields, a status bar, a menu field, a list, a static text
// view, a hidden button and an unnamed one.
class SettingsWindow : public BWindow {
public:
	SettingsWindow()
		:
		BWindow({100, 100, 500, 500}, "Settings", BWindowLook::Titled)
	{
		auto box = std::make_unique<BBox>(gfx::Rect{10, 10, 390, 110}, "options");
		box->SetLabel("Options");
		auto save = std::make_unique<BCheckBox>(gfx::Rect{10, 20, 200, 40}, "save",
			"Save automatically", std::make_unique<BMessage>(kSaved));
		save->SetValue(kControlOn);
		box->AddChild(std::move(save));
		// a plain view (as layout containers are) is left out, its children
		// are not
		auto group = std::make_unique<BView>(gfx::Rect{10, 50, 310, 90}, "group",
			kFollowLeftTop, BViewFlags());
		group->AddChild(std::make_unique<BRadioButton>(gfx::Rect{0, 0, 140, 20}, "small",
			"Small", nullptr));
		auto large = std::make_unique<BRadioButton>(gfx::Rect{150, 0, 290, 20}, "large",
			"Large", nullptr);
		large->SetValue(kControlOn);
		group->AddChild(std::move(large));
		box->AddChild(std::move(group));
		AddChild(std::move(box));

		auto volume = std::make_unique<BSlider>(gfx::Rect{10, 120, 390, 160}, "volume",
			"Volume", nullptr, 0, 100);
		volume->SetValue(30);
		volume->SetKeyIncrementValue(5);
		AddChild(std::move(volume));
		AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 170, 390, 190}, "name",
			"Name:", "Ada", nullptr));
		auto password = std::make_unique<BTextControl>(gfx::Rect{10, 200, 390, 220},
			"password", "Password:", "", nullptr);
		password->TextView()->HideTyping(true);
		password->SetText("secret");
		AddChild(std::move(password));
		auto progress = std::make_unique<BStatusBar>(gfx::Rect{10, 230, 390, 270},
			"progress", "Copying");
		progress->SetTo(40, "4 of 10");
		AddChild(std::move(progress));
		auto sizes = std::make_unique<BPopUpMenu>("sizes");
		for (const char* size : {"Small", "Medium"})
			sizes->AddItem(std::make_unique<BMenuItem>(size, nullptr));
		sizes->ItemAt(1)->SetMarked(true);
		AddChild(std::make_unique<BMenuField>(gfx::Rect{10, 280, 210, 304}, "size", "Size:",
			std::move(sizes), true));
		auto fruits = std::make_unique<BListView>(gfx::Rect{10, 310, 190, 360}, "fruits");
		fruits->SetAccessibleName("Fruits");
		AddChild(std::move(fruits));
		auto note = std::make_unique<BTextView>(gfx::Rect{200, 310, 390, 330}, "note",
			gfx::Rect{0, 0, 190, 20});
		note->SetText("Changes apply at once.");
		note->MakeEditable(false);
		note->MakeSelectable(false);
		AddChild(std::move(note));
		auto hidden = std::make_unique<BButton>(gfx::Rect{10, 365, 90, 390}, "hidden",
			"Hidden", nullptr);
		hidden->Hide();
		AddChild(std::move(hidden));
		auto ok = std::make_unique<BButton>(gfx::Rect{300, 365, 390, 390}, "ok", "OK",
			std::make_unique<BMessage>(kSaved));
		ok->SetToolTip("Closes the window");
		BButton* okButton = static_cast<BButton*>(AddChild(std::move(ok)));
		SetDefaultButton(okButton);
		auto off = std::make_unique<BButton>(gfx::Rect{200, 365, 290, 390}, "off", "",
			nullptr);
		off->SetEnabled(false);
		AddChild(std::move(off));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kSaved) {
			fSaved++;
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	int fSaved = 0;
};


// The application with the settings window, headless.
class SettingsApp {
public:
	SettingsApp()
		:
		fApp("application/x-vnd.ngui-accessibility-test"),
		fDriver(fApp)
	{
		TextSystem::SetDefault(&fText);
		auto window = std::make_unique<SettingsWindow>();
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver.WaitForIdle();
	}

	~SettingsApp()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		TextSystem::SetDefault(nullptr);
	}

	Driver& Drive()
	{
		return fDriver;
	}

	WindowRef Window()
	{
		return fDriver.Window("Settings");
	}

private:
	TestEnvironment fEnvironment;
	FixedTextSystem fText;
	BApplication fApp;
	Driver fDriver;
	BMessenger fWindow;
};


std::uint64_t
IdOf(const AccessibleTree& tree, std::string_view name)
{
	const AccessibleNode* node = tree.FindByName(name);
	return node != nullptr ? node->id : 0;
}


TEST(AccessibilityTest, TheTreeSaysWhatTheWindowShows)
{
	// What is tested: The accessibility tree of the settings window: a box
	// with a check box that is on and, in a plain view, two radio
	// buttons; a slider at 30 of 0 to 100 by 5; a text field; a password
	// field with "secret"; a status bar at 40; a menu field with "Medium"
	// marked; a list named by SetAccessibleName(); a text view that can be
	// neither edited nor selected; a hidden button; the default button
	// with a tooltip; a disabled button without a label (not navigable,
	// as Haiku's disabled controls).
	// Success criteria: The window is the root, named by its title; the
	// plain view is left out and its radio buttons are in the box; the hidden
	// button is not there; each node has its role, name, value and states
	// as the text shows (the password field without its text, the menu
	// field's bar and the text field's text view not as nodes of their
	// own); the tooltip is the OK button's description; bounds are in
	// window coordinates.
	// Why it matters: A screen reader reads the window as the user sees it
	// (toolkit scope 11.3), and never a password.
	//
	// Given
	SettingsApp app;

	// When
	const AccessibleTree tree = app.Window().AccessibleTree();

	// Then
	EXPECT_EQ(tree.Text(),
		"window \"Settings\"\n"
		"  group \"Options\"\n"
		"    checkbox \"Save automatically\" [focusable, checked] {click, focus}\n"
		"    radiobutton \"Small\" [focusable] {click, focus}\n"
		"    radiobutton \"Large\" [focusable, checked] {click, focus}\n"
		"  slider \"Volume\" = 30 (0..100 by 5) [focusable] {focus, increment, decrement, "
		"setvalue}\n"
		"  textinput \"Name:\" = \"Ada\" [focusable] {click, focus, setvalue}\n"
		"  passwordinput \"Password:\" [focusable] {click, focus, setvalue}\n"
		"  progressindicator \"Copying\" = \"4 of 10\" = 40 (0..100)\n"
		"  combobox \"Size:\" = \"Medium\" [focusable] {focus}\n"
		"  list \"Fruits\" [focusable] {focus}\n"
		"  label \"Changes apply at once.\"\n"
		"  button \"OK\" \"Closes the window\" [focusable, default] {click, focus}\n"
		"  button [disabled]\n");
	ASSERT_FALSE(tree.nodes.empty());
	EXPECT_EQ(tree.focus, tree.nodes.front().id);
	const AccessibleNode* large = tree.FindByName("Large");
	ASSERT_NE(large, nullptr);
	EXPECT_EQ(large->bounds, (gfx::Rect{170, 60, 310, 80}));
	const AccessibleNode* password = tree.FindByName("Password:");
	ASSERT_NE(password, nullptr);
	EXPECT_TRUE(password->info.value.empty());
	EXPECT_EQ(tree.FindByName("Hidden"), nullptr);
}


TEST(AccessibilityTest, ActionsComeToTheViews)
{
	// What is tested: Actions of assistive technology through the driver
	// (kAccessibleAction): Click on the check box; Increment and SetValue
	// 80 on the slider; SetValue "Grace" and Focus on the text field (with
	// the window active); SetValue on the password field; Click on the
	// disabled button, Increment on the OK button, SetValue on the slider
	// without a number, and a node that does not exist.
	// Success criteria: The check box is off and its window got its
	// message; the slider is at 35, then 80; the text field has "Grace"
	// and the focus (Focused, the tree's focus; its text view is not a
	// node); the password field has the new text, which the tree still
	// does not show. The disabled button and the Increment are
	// NotSupported, the slider without a number InvalidArgument, the
	// missing node NotFound.
	// Why it matters: Screen readers work controls as keyboards do; a
	// view says no to what it cannot do.
	//
	// Given
	SettingsApp app;
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	const AccessibleTree tree = window.AccessibleTree();
	const std::uint64_t save = IdOf(tree, "Save automatically");
	const std::uint64_t volume = IdOf(tree, "Volume");
	const std::uint64_t name = IdOf(tree, "Name:");
	const std::uint64_t password = IdOf(tree, "Password:");
	const std::uint64_t ok = IdOf(tree, "OK");
	BMessage eighty;
	(void)eighty.AddDouble("number", 80);
	BMessage grace;
	(void)grace.AddString("value", "Grace");
	BMessage hunter;
	(void)hunter.AddString("value", "hunter2");
	std::uint64_t off = 0;
	for (const AccessibleNode& node : tree.nodes) {
		if (node.info.states.Has(AccessibleState::Disabled))
			off = node.id;
	}

	// When
	const auto clicked = driver.PerformAccessibleAction(window, save, AccessibleAction::Click);
	const auto stepped = driver.PerformAccessibleAction(window, volume,
		AccessibleAction::Increment);
	const std::optional<std::int32_t> afterStep = window.Find("volume").Value();
	const auto set = driver.PerformAccessibleAction(window, volume, AccessibleAction::SetValue,
		eighty);
	(void)driver.ClickAt(window, {395, 395});
	const auto named = driver.PerformAccessibleAction(window, name,
		AccessibleAction::SetValue, grace);
	const auto focused = driver.PerformAccessibleAction(window, name, AccessibleAction::Focus);
	const auto secret = driver.PerformAccessibleAction(window, password,
		AccessibleAction::SetValue, hunter);
	const auto disabled = driver.PerformAccessibleAction(window, off, AccessibleAction::Click);
	const auto unsupported = driver.PerformAccessibleAction(window, ok,
		AccessibleAction::Increment);
	const auto noNumber = driver.PerformAccessibleAction(window, volume,
		AccessibleAction::SetValue);
	const auto missing = driver.PerformAccessibleAction(window, 999999,
		AccessibleAction::Click);
	const AccessibleTree after = window.AccessibleTree();

	// Then
	EXPECT_TRUE(clicked.has_value()) << clicked.error().message;
	EXPECT_EQ(window.Find("save").Value(), 0);
	EXPECT_TRUE(stepped.has_value());
	EXPECT_EQ(afterStep, 35);
	EXPECT_TRUE(set.has_value());
	EXPECT_EQ(window.Find("volume").Value(), 80);
	EXPECT_TRUE(named.has_value());
	EXPECT_EQ(window.Find("name").Text(), "Grace");
	EXPECT_TRUE(focused.has_value());
	EXPECT_EQ(after.focus, name);
	const AccessibleNode* nameNode = after.Find(name);
	ASSERT_NE(nameNode, nullptr);
	EXPECT_TRUE(nameNode->info.states.Has(AccessibleState::Focused));
	EXPECT_EQ(nameNode->info.value, "Grace");
	EXPECT_TRUE(secret.has_value());
	EXPECT_EQ(window.Find("password").Text(), "hunter2");
	EXPECT_EQ(after.Text().find("hunter2"), std::string::npos);
	EXPECT_EQ(disabled.error().code, ErrorCode::NotSupported);
	EXPECT_EQ(unsupported.error().code, ErrorCode::NotSupported);
	EXPECT_EQ(noNumber.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(missing.error().code, ErrorCode::NotFound);
}


TEST(AccessibilityTest, TheLintWarnsOfControlsWithoutAName)
{
	// What is tested: The layout lint of the settings window, whose
	// disabled button has no label; then with that button named by
	// SetAccessibleName(); and with the check turned off.
	// Success criteria: One Unnamed warning, at the button, and no error
	// for it; none when it is named or the check is off.
	// Why it matters: A control without a name is a button a screen reader
	// can only call "button" (toolkit scope 11.3).
	//
	// Given
	SettingsApp app;
	WindowRef window = app.Window();
	const auto unnamed = [](const LintReport& report) {
		std::vector<LintFinding> found;
		for (const LintFinding& finding : report.findings) {
			if (finding.check == LintCheck::Unnamed)
				found.push_back(finding);
		}
		return found;
	};

	// When
	const std::vector<LintFinding> before = unnamed(Lint(window));
	LintOptions withoutCheck;
	withoutCheck.accessibleNames = false;
	const std::vector<LintFinding> off = unnamed(Lint(window, withoutCheck));
	(void)window.Find("off").Run([](BView& view) { view.SetAccessibleName("Reset"); });
	const std::vector<LintFinding> after = unnamed(Lint(window));

	// Then
	ASSERT_EQ(before.size(), 1u);
	EXPECT_EQ(before[0].path, "off");
	EXPECT_EQ(before[0].severity, LintSeverity::Warning);
	EXPECT_NE(before[0].message.find("button"), std::string::npos);
	EXPECT_TRUE(off.empty());
	EXPECT_TRUE(after.empty());
}


TEST(AccessibilityTest, TheWindowPassesItsTreeOnWhileSomeoneListens)
{
	// What is tested: The settings window on the headless platform: before
	// assistive technology listens; after it begins (SetAccessibilityActive,
	// kAccessibilityRequested); after a click on the check box; after an
	// update that changes nothing; after it stops and the check box is
	// clicked again.
	// Success criteria: Nothing is passed on before it listens; then the
	// whole tree, equal to the window's; after the click a tree in which
	// the check box is off; an update without a change passes nothing on;
	// after it stops, nothing more.
	// Why it matters: Screen readers learn of each change, and a window
	// nobody listens to builds no trees (toolkit scope 11.3).
	//
	// Given
	SettingsApp app;
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	platform::HeadlessWindow* platformWindow = window.PlatformWindow();
	ASSERT_NE(platformWindow, nullptr);

	// When
	const std::size_t before = platformWindow->CountAccessibleUpdates();
	platformWindow->SetAccessibilityActive(true);
	(void)driver.WaitForIdle();
	const std::size_t first = platformWindow->CountAccessibleUpdates();
	const std::optional<AccessibleTree> initial = platformWindow->LastAccessibleTree();
	const AccessibleTree current = window.AccessibleTree();
	(void)driver.Click(window.Find("save"));
	(void)driver.WaitForIdle();
	const std::size_t clicked = platformWindow->CountAccessibleUpdates();
	const std::optional<AccessibleTree> afterClick = platformWindow->LastAccessibleTree();
	(void)window.Find("note").Run([](BView& view) { view.Invalidate(); });
	(void)driver.WaitForIdle();
	const std::size_t unchanged = platformWindow->CountAccessibleUpdates();
	platformWindow->SetAccessibilityActive(false);
	(void)driver.Click(window.Find("save"));
	(void)driver.WaitForIdle();
	const std::size_t stopped = platformWindow->CountAccessibleUpdates();

	// Then
	EXPECT_EQ(before, 0u);
	EXPECT_EQ(first, 1u);
	ASSERT_TRUE(initial.has_value());
	EXPECT_EQ(initial->nodes, current.nodes);
	EXPECT_GT(clicked, first);
	ASSERT_TRUE(afterClick.has_value());
	const AccessibleNode* save = afterClick->FindByName("Save automatically");
	ASSERT_NE(save, nullptr);
	EXPECT_FALSE(save->info.states.Has(AccessibleState::Checked));
	EXPECT_EQ(unchanged, clicked);
	EXPECT_EQ(stopped, unchanged);
}

TEST(AccessibilityTest, TheTreeMatchesItsSnapshot)
{
	// What is tested: EXPECT_ACCESSIBILITY_SNAPSHOT of the settings window.
	// Success criteria: It matches its reference text (backend "a11y").
	// Why it matters: Applications pin what screen readers get as they pin
	// layouts.
	//
	// Given
	SettingsApp app;

	// When / Then
	EXPECT_ACCESSIBILITY_SNAPSHOT(app.Window(), "AccessibilitySettings");
}


TEST(AccessibilityTest, EveryRoleHasAName)
{
	// What is tested: AccessibleRoleName() of every role, and Text() and
	// Find() of an empty tree and of a tree without its children.
	// Success criteria: Each role but None has its own name; an empty tree
	// is no text and finds nothing; a child id without a node is skipped.
	// Why it matters: The snapshot names each role; a broken tree must not
	// crash it.
	//
	// Given
	std::vector<std::string> names;
	for (int role = 0; role <= static_cast<int>(AccessibleRole::Generic); role++)
		names.emplace_back(AccessibleRoleName(static_cast<AccessibleRole>(role)));
	AccessibleTree broken;
	broken.nodes.push_back({1, {AccessibleRole::Window, "W", {}, {}, {}, {}, {}, {}, {}, {}},
		{0, 0, 10, 10}, {2}});

	// When
	const std::string text = broken.Text();

	// Then
	EXPECT_EQ(names.front(), "none");
	for (std::size_t i = 1; i < names.size(); i++) {
		EXPECT_NE(names[i], "none") << i;
		for (std::size_t j = 0; j < i; j++)
			EXPECT_NE(names[i], names[j]);
	}
	EXPECT_EQ(AccessibleTree().Text(), "");
	EXPECT_EQ(AccessibleTree().Find(1), nullptr);
	EXPECT_EQ(text, "window \"W\"\n");
}


} // namespace
