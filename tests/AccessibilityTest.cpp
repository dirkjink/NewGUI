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
#include <ngui/TabView.h>
#include <ngui/StringItem.h>
#include <ngui/SeparatorItem.h>
#include <ngui/OutlineListView.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
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


// The application with a window of \a TestWindow (titled \a title),
// headless.
template<typename TestWindow>
class WindowApp {
public:
	explicit WindowApp(std::string title)
		:
		fApp("application/x-vnd.ngui-accessibility-test"),
		fDriver(fApp),
		fTitle(std::move(title))
	{
		TextSystem::SetDefault(&fText);
		auto window = std::make_unique<TestWindow>();
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver.WaitForIdle();
	}

	~WindowApp()
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
		return fDriver.Window(fTitle);
	}

	BMessenger Messenger() const
	{
		return fWindow;
	}

private:
	TestEnvironment fEnvironment;
	FixedTextSystem fText;
	BApplication fApp;
	Driver fDriver;
	std::string fTitle;
	BMessenger fWindow;
};


// The settings window's application.
class SettingsApp : public WindowApp<SettingsWindow> {
public:
	SettingsApp()
		:
		WindowApp("Settings")
	{
	}
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
		"  combobox \"Size:\" = \"Medium\" [focusable] {click, focus}\n"
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
	// without a number, and a node that does not exist; then Click on the
	// menu field.
	// Success criteria: The check box is off and its window got its
	// message; the slider is at 35, then 80; the text field has "Grace"
	// and the focus (Focused, the tree's focus; its text view is not a
	// node); the password field has the new text, which the tree still
	// does not show. The disabled button and the Increment are
	// NotSupported, the slider without a number InvalidArgument, the
	// missing node NotFound. The menu field's menu opens, called as its
	// marked item, which is checked.
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
	const auto sizes = driver.PerformAccessibleAction(window, IdOf(tree, "Size:"),
		AccessibleAction::Click);
	const std::string menu = window.AccessibleTree().Text();

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
	EXPECT_TRUE(sizes.has_value()) << sizes.error().message;
	EXPECT_NE(menu.find("  menu \"Medium\"\n"
		"    menuitem \"Small\" {click}\n"
		"    menuitem \"Medium\" [checked] {click}\n"), std::string::npos) << menu;
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

constexpr std::uint32_t kQuit = FourCharCode("QUIT");


// A window with a menu bar (File: Open, a separator, Quit; Edit: Copy), a
// list of two fruits with Banana selected, an outline of Fruit (with
// Apple under it) and Vegetables, and a tab view with General and
// Advanced. It counts kQuit.
class ItemsWindow : public BWindow {
public:
	ItemsWindow()
		:
		BWindow({100, 100, 500, 400}, "Items", BWindowLook::Titled)
	{
		auto bar = std::make_unique<BMenuBar>(gfx::Rect{0, 0, 400, 20}, "bar");
		auto file = std::make_unique<BMenu>("File");
		(void)file->AddItem(std::make_unique<BMenuItem>("Open", nullptr));
		(void)file->AddItem(std::make_unique<BSeparatorItem>());
		(void)file->AddItem(std::make_unique<BMenuItem>("Quit",
			std::make_unique<BMessage>(kQuit)));
		(void)bar->AddItem(std::make_unique<BMenuItem>(std::move(file)));
		auto edit = std::make_unique<BMenu>("Edit");
		(void)edit->AddItem(std::make_unique<BMenuItem>("Copy", nullptr));
		(void)bar->AddItem(std::make_unique<BMenuItem>(std::move(edit)));
		AddChild(std::move(bar));

		auto fruits = std::make_unique<BListView>(gfx::Rect{10, 30, 190, 80}, "fruits");
		fruits->SetAccessibleName("Fruits");
		(void)fruits->AddItem(std::make_unique<BStringItem>("Apple"));
		(void)fruits->AddItem(std::make_unique<BStringItem>("Banana"));
		fruits->Select(1);
		AddChild(std::move(fruits));
		auto outline = std::make_unique<BOutlineListView>(gfx::Rect{200, 30, 390, 100},
			"outline");
		outline->SetAccessibleName("Food");
		(void)outline->AddItem(std::make_unique<BStringItem>("Fruit"));
		(void)outline->AddItem(std::make_unique<BStringItem>("Apple", 1));
		(void)outline->AddItem(std::make_unique<BStringItem>("Vegetables"));
		AddChild(std::move(outline));
		auto tabs = std::make_unique<BTabView>(gfx::Rect{10, 110, 390, 290}, "tabs");
		tabs->AddTab(std::make_unique<BView>(gfx::Rect{0, 0, 100, 100}, "General",
			kFollowAll, BViewFlags()));
		tabs->AddTab(std::make_unique<BView>(gfx::Rect{0, 0, 100, 100}, "Advanced",
			kFollowAll, BViewFlags()));
		AddChild(std::move(tabs));
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kQuit) {
			fQuits++;
			return;
		}
		BWindow::MessageReceived(message);
	}

	int fQuits = 0;
};


TEST(AccessibilityTest, ItemsAreNodesOfTheirViews)
{
	// What is tested: The tree of the items window, then actions on items:
	// Click on Apple in the list; Collapse and Expand of Fruit; Click on
	// the Advanced tab; Click on the File menu of the bar, the tree with
	// the open menu; Click on Quit in it.
	// Success criteria: The items are nodes below their views, with ids of
	// the view's token times 2^32 plus index + 1: the bar's menus as menu
	// items (collapsed), the list's items with Banana selected, the outline's with
	// their levels and Fruit expanded, the tabs with General selected.
	// Clicking Apple selects it alone; Collapse hides Apple under Fruit,
	// Expand shows it again; Click selects the tab. Clicking File opens its
	// menu: the tree has it below the window, called File, its items with
	// the separator as splitter; the focus is on File, expanded (as Haiku
	// opens a menu on a click, no item selected). Clicking Quit invokes it
	// (the window counts it) and closes the menu.
	// Why it matters: Screen readers read and work lists, outlines, tabs
	// and menus item by item (toolkit scope 11.3).
	//
	// Given
	WindowApp<ItemsWindow> app("Items");
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	(void)driver.ClickAt(window, {395, 395});
	const AccessibleTree tree = window.AccessibleTree();
	const AccessibleNode* fruits = tree.FindByName("Fruits");
	const AccessibleNode* food = tree.FindByName("Food");
	const AccessibleNode* bar = nullptr;
	const AccessibleNode* tabs = nullptr;
	for (const AccessibleNode& node : tree.nodes) {
		if (node.info.role == AccessibleRole::MenuBar)
			bar = &node;
		if (node.info.role == AccessibleRole::TabList)
			tabs = &node;
	}
	ASSERT_TRUE(fruits != nullptr && food != nullptr && bar != nullptr && tabs != nullptr);
	const auto child = [&](const AccessibleNode* node, std::size_t index) {
		return index < node->children.size() ? node->children[index] : 0;
	};

	// When
	const auto apple = driver.PerformAccessibleAction(window, child(fruits, 0),
		AccessibleAction::Click);
	const auto collapsed = driver.PerformAccessibleAction(window, child(food, 0),
		AccessibleAction::Collapse);
	const std::size_t whileCollapsed = window.AccessibleTree().Find(food->id)->children.size();
	const auto expanded = driver.PerformAccessibleAction(window, child(food, 0),
		AccessibleAction::Expand);
	const auto advanced = driver.PerformAccessibleAction(window, child(tabs, 1),
		AccessibleAction::Click);
	const auto opened = driver.PerformAccessibleAction(window, child(bar, 0),
		AccessibleAction::Click);
	const AccessibleTree open = window.AccessibleTree();
	const AccessibleNode* menu = nullptr;
	for (const AccessibleNode& node : open.nodes) {
		if (node.info.role == AccessibleRole::Menu)
			menu = &node;
	}
	ASSERT_NE(menu, nullptr);
	const std::uint64_t quit = child(menu, 2);
	const auto chosen = driver.PerformAccessibleAction(window, quit, AccessibleAction::Click);
	const AccessibleTree after = window.AccessibleTree();
	int quits = -1;
	(void)window.Find("bar").Run([&](BView& view) {
		quits = static_cast<ItemsWindow*>(view.Window())->fQuits;
	});

	// Then
	const std::uint64_t token = fruits->id;
	EXPECT_EQ(fruits->children, (std::vector<std::uint64_t>{(token << 32) | 1,
		(token << 32) | 2}));
	EXPECT_EQ(tree.Text().substr(tree.Text().find("  menubar")),
		"  menubar\n"
		"    menuitem \"File\" [collapsed] {click}\n"
		"    menuitem \"Edit\" [collapsed] {click}\n"
		"  list \"Fruits\" [focusable] {focus}\n"
		"    listitem \"Apple\" {click}\n"
		"    listitem \"Banana\" [selected] {click}\n"
		"  tree \"Food\" [focusable] {focus}\n"
		"    treeitem \"Fruit\" level 1 [expanded] {click, collapse}\n"
		"    treeitem \"Apple\" level 2 {click}\n"
		"    treeitem \"Vegetables\" level 1 {click}\n"
		"  tablist [focusable] {focus}\n"
		"    tab \"General\" [selected] {click}\n"
		"    tab \"Advanced\" {click}\n") << tree.Text();
	EXPECT_TRUE(apple.has_value()) << apple.error().message;
	EXPECT_TRUE(collapsed.has_value());
	EXPECT_EQ(whileCollapsed, 2u);
	EXPECT_TRUE(expanded.has_value());
	EXPECT_TRUE(advanced.has_value());
	EXPECT_TRUE(opened.has_value()) << opened.error().message;
	EXPECT_NE(open.Text().find("  menu \"File\"\n"
		"    menuitem \"Open\" {click}\n"
		"    splitter\n"
		"    menuitem \"Quit\" {click}\n"), std::string::npos) << open.Text();
	EXPECT_EQ(open.focus, child(bar, 0));
	EXPECT_NE(open.Text().find("menuitem \"File\" [focused, selected, expanded]"),
		std::string::npos);
	EXPECT_TRUE(chosen.has_value()) << chosen.error().message;
	EXPECT_EQ(quits, 1);
	EXPECT_EQ(after.Text().find("  menu \"File\""), std::string::npos);
	const AccessibleTree final = after;
	const AccessibleNode* finalFruits = final.FindByName("Fruits");
	ASSERT_NE(finalFruits, nullptr);
	EXPECT_NE(final.Text().find("    listitem \"Apple\" [selected] {click}\n"
		"    listitem \"Banana\" {click}\n"), std::string::npos);
	EXPECT_NE(final.Text().find("    tab \"General\" {click}\n"
		"    tab \"Advanced\" [selected] {click}\n"), std::string::npos);
	EXPECT_NE(final.Text().find("treeitem \"Fruit\" level 1 [expanded]"), std::string::npos);
}

TEST(AccessibilityTest, AnnouncementsAreNodesOfTheirOwn)
{
	// What is tested: The settings window with assistive technology
	// listening (headless): the static note made a polite live region;
	// BWindow::Announce("Saved") on the window's thread; kAnnounce "Saved"
	// again from the test's thread; an assertive "Disk full"; an empty
	// announcement.
	// Success criteria: The note is "live polite". After the first
	// announcement the window's last child is a label "Saved", live polite,
	// with the id window token times 2^32 plus 1; the second has the same
	// text but a new id (plus 2), so that it is announced again; the third
	// is "live assertive" and the only announcement node; after the empty
	// one there is none. Each announcement is passed on to the platform.
	// Why it matters: Screen readers announce what a program says without
	// a view of its own, and changes of status lines (toolkit scope 11.3).
	//
	// Given
	SettingsApp app;
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	platform::HeadlessWindow* platformWindow = window.PlatformWindow();
	ASSERT_NE(platformWindow, nullptr);
	platformWindow->SetAccessibilityActive(true);
	(void)window.Find("note").Run([](BView& view) {
		view.SetLiveRegion(AccessibleLive::Polite);
		view.Invalidate();
	});
	(void)driver.WaitForIdle();
	const std::size_t before = platformWindow->CountAccessibleUpdates();
	const auto announce = [&](const char* text, AccessibleLive live) {
		BMessage message(kAnnounce);
		(void)message.AddString("text", text);
		(void)message.AddInt32("live", static_cast<std::int32_t>(live));
		(void)app.Messenger().SendMessage(message);
		(void)driver.WaitForIdle();
		return window.AccessibleTree();
	};

	// When
	(void)window.Find("note").Run([](BView& view) { view.Window()->Announce("Saved"); });
	(void)driver.WaitForIdle();
	const AccessibleTree first = window.AccessibleTree();
	const std::size_t afterFirst = platformWindow->CountAccessibleUpdates();
	const AccessibleTree second = announce("Saved", AccessibleLive::Polite);
	const std::size_t afterSecond = platformWindow->CountAccessibleUpdates();
	const AccessibleTree third = announce("Disk full", AccessibleLive::Assertive);
	const AccessibleTree cleared = announce("", AccessibleLive::Polite);

	// Then
	const std::uint64_t windowId = first.nodes.front().id;
	const auto last = [](const AccessibleTree& tree) {
		return tree.Find(tree.nodes.front().children.back());
	};
	EXPECT_NE(first.Text().find("label \"Changes apply at once.\" live polite"),
		std::string::npos) << first.Text();
	ASSERT_NE(last(first), nullptr);
	EXPECT_EQ(last(first)->id, (windowId << 32) | 1);
	EXPECT_EQ(last(first)->info.name, "Saved");
	EXPECT_EQ(last(first)->info.live, AccessibleLive::Polite);
	EXPECT_GT(afterFirst, before);
	ASSERT_NE(last(second), nullptr);
	EXPECT_EQ(last(second)->id, (windowId << 32) | 2);
	EXPECT_EQ(last(second)->info.name, "Saved");
	EXPECT_GT(afterSecond, afterFirst);
	EXPECT_NE(third.Text().find("  label \"Disk full\" live assertive\n"), std::string::npos);
	EXPECT_EQ(third.Text().find("\"Saved\""), std::string::npos);
	EXPECT_EQ(cleared.Text().find("Disk full"), std::string::npos);
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
