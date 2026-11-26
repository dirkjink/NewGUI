/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Accessibility.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/ColumnListView.h>
#include <ngui/ColumnTypes.h>
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
		"  textinput \"Name:\" = \"Ada\" selection 0:0..0:0 [focusable] {click, focus, "
		"setvalue, settextselection, replaceselectedtext}\n"
		"    textrun = \"Ada\" (3 characters, words at 0)\n"
		"  passwordinput \"Password:\" selection 0:0..0:0 [focusable] {click, focus, "
		"setvalue, settextselection, replaceselectedtext}\n"
		"    textrun = \"\u2022\u2022\u2022\u2022\u2022\u2022\" (6 characters)\n"
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

// A window with a column list "Files" (Name and Size, sorted by Name):
// Documents (collapsed, with Notes.md of 300 bytes) and README of 12 bytes;
// the list has the focus.
class TableWindow : public BWindow {
public:
	TableWindow()
		:
		BWindow({100, 100, 500, 400}, "Table", BWindowLook::Titled)
	{
		auto list = std::make_unique<BColumnListView>(gfx::Rect{10, 10, 390, 200}, "files",
			kFollowLeftTop, BViewFlag::Navigable);
		list->SetAccessibleName("Files");
		auto name = std::make_unique<BStringColumn>("Name", 150.0f, 20.0f, 300.0f);
		BColumn* nameColumn = name.get();
		list->AddColumn(std::move(name), 0);
		list->AddColumn(std::make_unique<BSizeColumn>("Size", 80.0f, 20.0f, 300.0f), 1);
		list->SetSortColumn(nameColumn, false, true);
		auto documents = std::make_unique<BRow>();
		documents->SetField(std::make_unique<BStringField>("Documents"), 0);
		BRow* folder = documents.get();
		list->AddRow(std::move(documents));
		auto notes = std::make_unique<BRow>();
		notes->SetField(std::make_unique<BStringField>("Notes.md"), 0);
		notes->SetField(std::make_unique<BSizeField>(300), 1);
		list->AddRow(std::move(notes), folder);
		auto readme = std::make_unique<BRow>();
		readme->SetField(std::make_unique<BStringField>("README"), 0);
		readme->SetField(std::make_unique<BSizeField>(12), 1);
		list->AddRow(std::move(readme));
		AddChild(std::move(list))->MakeFocus(true);
	}
};


TEST(AccessibilityTest, TablesAreRowsOfCells)
{
	// What is tested: The tree of a window with a column list of Name and
	// Size, with Documents (collapsed, with Notes.md) and README; then actions
	// on its items: Expand on Documents; Click on the size cell of README;
	// Focus on Notes.md; Click on the column header Size; Collapse on
	// Documents; the layout lint with the list named and unnamed.
	// Success criteria: The list is a tree table with its rows (the titles'
	// too) and columns; its first item is the titles' row of column
	// headers, then a row for each shown row, with its level, Expanded or
	// Collapsed, and a cell for each column, named by the field's text
	// (an empty cell for a missing field). Expand shows Notes.md at level 2;
	// a click on a cell selects its row; Focus makes a row the focus row,
	// and the window's focus goes to it; the header sorts by Size (the row
	// without a size last); Collapse hides Notes.md again. The lint warns
	// of the table only once its name is taken away.
	// Why it matters: Screen readers read tables row by row and cell by
	// cell, with the column titles (toolkit scope 11.3).
	//
	// Given
	WindowApp<TableWindow> app("Table");
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	(void)driver.ClickAt(window, {395, 395});
	const AccessibleTree tree = window.AccessibleTree();
	const AccessibleNode* files = tree.FindByName("Files");
	ASSERT_NE(files, nullptr);
	// the rows (and their cells) by the name of their first cell
	const auto row = [](const AccessibleTree& in, std::string_view name) -> std::uint64_t {
		for (const AccessibleNode& node : in.nodes) {
			if (node.info.role != AccessibleRole::Row || node.children.empty())
				continue;
			const AccessibleNode* first = in.Find(node.children.front());
			if (first != nullptr && first->info.name == name)
				return node.id;
		}
		return 0;
	};
	const auto table = [](const AccessibleTree& in) {
		const std::string text = in.Text();
		const std::size_t from = text.find("  treetable \"Files\"");
		return from == std::string::npos ? text : text.substr(from, text.find("    scrollbar",
			from) - from);
	};

	// When
	const auto expanded = driver.PerformAccessibleAction(window, row(tree, "Documents"),
		AccessibleAction::Expand);
	const AccessibleTree open = window.AccessibleTree();
	const AccessibleNode* readme = open.Find(row(open, "README"));
	ASSERT_NE(readme, nullptr);
	const auto clicked = driver.PerformAccessibleAction(window, readme->children.back(),
		AccessibleAction::Click);
	const auto focused = driver.PerformAccessibleAction(window, row(open, "Notes.md"),
		AccessibleAction::Focus);
	const AccessibleTree focus = window.AccessibleTree();
	const AccessibleNode* titles = open.Find(open.Find(files->id)->children.front());
	ASSERT_NE(titles, nullptr);
	const auto sorted = driver.PerformAccessibleAction(window, titles->children.back(),
		AccessibleAction::Click);
	const AccessibleTree bySize = window.AccessibleTree();
	// items are counted by their place: Documents moved
	const auto collapsed = driver.PerformAccessibleAction(window, row(bySize, "Documents"),
		AccessibleAction::Collapse);
	const AccessibleTree after = window.AccessibleTree();
	const auto unnamed = [&window] {
		std::vector<std::string> found;
		for (const LintFinding& finding : Lint(window).findings) {
			if (finding.check == LintCheck::Unnamed)
				found.push_back(finding.path + ": " + finding.message);
		}
		return found;
	};
	const std::vector<std::string> named = unnamed();
	(void)window.Find("files").Run([](BView& view) { view.SetAccessibleName(""); });
	const std::vector<std::string> nameless = unnamed();

	// Then
	EXPECT_EQ(table(tree),
		"  treetable \"Files\" rows 3 columns 2 [focusable, focused] {focus}\n"
		"    row row 0\n"
		"      columnheader \"Name\" row 0 column 0 {click}\n"
		"      columnheader \"Size\" row 0 column 1 {click}\n"
		"    row level 1 row 1 [collapsed] {click, focus, expand}\n"
		"      cell \"Documents\" row 1 column 0\n"
		"      cell row 1 column 1\n"
		"    row level 1 row 2 {click, focus}\n"
		"      cell \"README\" row 2 column 0\n"
		"      cell \"12 bytes\" row 2 column 1\n");
	EXPECT_EQ(files->children.front(), (files->id << 32) | 1);
	EXPECT_TRUE(expanded.has_value());
	EXPECT_NE(table(open).find("    row level 2 row 2 {click, focus}\n"
		"      cell \"Notes.md\" row 2 column 0\n"
		"      cell \"300 bytes\" row 2 column 1\n"), std::string::npos) << table(open);
	EXPECT_NE(table(open).find("rows 4 columns 2"), std::string::npos);
	EXPECT_TRUE(clicked.has_value());
	EXPECT_TRUE(focused.has_value());
	EXPECT_EQ(focus.focus, row(focus, "Notes.md"));
	EXPECT_NE(table(focus).find("    row level 2 row 2 [focused] {click, focus}\n"),
		std::string::npos) << table(focus);
	EXPECT_NE(table(focus).find("    row level 1 row 3 [selected] {click, focus}\n"
		"      cell \"README\""), std::string::npos) << table(focus);
	EXPECT_TRUE(sorted.has_value());
	EXPECT_NE(table(bySize).find("    row level 1 row 1 [selected] {click, focus}\n"
		"      cell \"README\""), std::string::npos) << table(bySize);
	EXPECT_TRUE(collapsed.has_value());
	EXPECT_NE(table(after).find("    row level 1 row 2 [focused, collapsed] {click, focus, "
		"expand}\n      cell \"Documents\""), std::string::npos) << table(after);
	EXPECT_EQ(table(after).find("Notes.md"), std::string::npos);
	EXPECT_TRUE(named.empty());
	ASSERT_EQ(nameless.size(), 1u);
	EXPECT_EQ(nameless[0].substr(0, 18), "files: a treetable");
}


TEST(AccessibilityTest, AnnouncementsAreNodesOfTheirOwn)
{
	// What is tested: The settings window with assistive technology
	// listening (headless): the static note made a polite live region;
	// BWindow::Announce("Saved") on the window's thread; kAnnounce "Saved"
	// again from the test's thread; an assertive "Disk full"; an empty
	// announcement; the caret of the name field moved by SetTextSelection.
	// Success criteria: The note is "live polite". After the first
	// announcement the window's last child is a label "Saved", live polite,
	// with the id window token times 2^32 plus 1; the second has the same
	// text but a new id (plus 2), so that it is announced again; the third
	// is "live assertive" and the only announcement node; after the empty
	// one there is none. Each announcement is passed on to the platform.
	// Moving the caret of the unfocused name field (SetTextSelection), which
	// draws nothing anew, passes the tree with the new caret on as well.
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
	// an action that draws nothing anew still passes the tree on
	const std::size_t beforeSelect = platformWindow->CountAccessibleUpdates();
	BMessage caret;
	(void)caret.AddInt32("anchorItem", 0);
	(void)caret.AddInt32("anchorCharacter", 2);
	(void)caret.AddInt32("focusItem", 0);
	(void)caret.AddInt32("focusCharacter", 2);
	const auto moved = driver.PerformAccessibleAction(window, IdOf(cleared, "Name:"),
		AccessibleAction::SetTextSelection, caret);
	const std::optional<AccessibleTree> pushed = platformWindow->LastAccessibleTree();

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
	EXPECT_TRUE(moved.has_value()) << moved.error().message;
	EXPECT_GT(platformWindow->CountAccessibleUpdates(), beforeSelect);
	ASSERT_TRUE(pushed.has_value());
	const AccessibleNode* name = pushed->FindByName("Name:");
	ASSERT_NE(name, nullptr);
	EXPECT_EQ(name->info.textSelection, (AccessibleTextSelection{{0, 2}, {0, 2}}));
}

// A window with an editable text view "editor" (no wrapping) and a read-only
// one "notes".
class TextWindow : public BWindow {
public:
	TextWindow()
		:
		BWindow({100, 100, 500, 400}, "Text", BWindowLook::Titled)
	{
		auto editor = std::make_unique<BTextView>(gfx::Rect{10, 10, 390, 200}, "editor",
			gfx::Rect{0, 0, 380, 190});
		editor->SetAccessibleName("Editor");
		editor->SetWordWrap(false);
		AddChild(std::move(editor));
		auto notes = std::make_unique<BTextView>(gfx::Rect{10, 210, 390, 290}, "notes",
			gfx::Rect{0, 0, 380, 80});
		notes->SetAccessibleName("Notes");
		notes->SetText("Fixed");
		notes->MakeEditable(false);
		AddChild(std::move(notes));
	}
};


TEST(AccessibilityTest, TextHasRunsAndASelection)
{
	// What is tested: The editor with "Hello w\xC3\xB6rld\nSecond line" ("ö" two
	// bytes), "\xC3\xB6r" selected with the caret at its end; with 300 times "a";
	// with "end\n" and the caret at the end; then SetTextSelection of
	// "Second", ReplaceSelectedText by "First", and both on the read-only
	// notes or with a place that is not there.
	// Success criteria: A run per line, the line break at the end of the
	// first: 12 characters (the "ö" two bytes long), words at 0 and 6;
	// positions at 7 pixels a byte from the run's left edge (FixedTextSystem),
	// the "ö" 14 wide, the line break 0; the selection 0:7..0:9. 300
	// characters are two runs of 255 and 45. "end\n" has an empty last run,
	// and the caret is at its start. SetTextSelection selects bytes 13 to
	// 19; ReplaceSelectedText makes "Hello w\xC3\xB6rld\nFirst line", the caret
	// after "First" (byte 18). The
	// read-only notes do not replace (NotSupported), a place past a run
	// is InvalidArgument.
	// Why it matters: Screen readers read text by character, word and line
	// and follow the caret (toolkit scope 11.3); AccessKit needs runs of at
	// most 255 characters.
	//
	// Given
	WindowApp<TextWindow> app("Text");
	Driver& driver = app.Drive();
	WindowRef window = app.Window();
	const auto set = [&](const char* text, int32 from, int32 to) {
		(void)window.Find("editor").Run([&](BView& view) {
			auto& editor = static_cast<BTextView&>(view);
			editor.SetText(text);
			editor.Select(from, to);
		});
		(void)driver.WaitForIdle();
		return window.AccessibleTree();
	};
	const auto runsOf = [](const AccessibleTree& tree, std::string_view name) {
		std::vector<const AccessibleNode*> runs;
		const AccessibleNode* node = tree.FindByName(name);
		if (node != nullptr) {
			for (const std::uint64_t child : node->children)
				runs.push_back(tree.Find(child));
		}
		return runs;
	};

	// When
	const AccessibleTree lines = set("Hello w\xC3\xB6rld\nSecond line", 7, 10);
	const AccessibleTree longLine = set(std::string(300, 'a').c_str(), 0, 0);
	const AccessibleTree ended = set("end\n", 4, 4);
	(void)set("Hello w\xC3\xB6rld\nSecond line", 0, 0);
	const std::uint64_t editor = window.AccessibleTree().FindByName("Editor")->id;
	BMessage second;
	(void)second.AddInt32("anchorItem", 1);
	(void)second.AddInt32("anchorCharacter", 0);
	(void)second.AddInt32("focusItem", 1);
	(void)second.AddInt32("focusCharacter", 6);
	const auto selected = driver.PerformAccessibleAction(window, editor,
		AccessibleAction::SetTextSelection, second);
	int32 start = -1;
	int32 end = -1;
	(void)window.Find("editor").Run([&](BView& view) {
		static_cast<BTextView&>(view).GetSelection(&start, &end);
	});
	BMessage first;
	(void)first.AddString("value", "First");
	const auto replaced = driver.PerformAccessibleAction(window, editor,
		AccessibleAction::ReplaceSelectedText, first);
	const std::string text = window.Find("editor").Text();
	int32 caret = -1;
	int32 caretEnd = -1;
	(void)window.Find("editor").Run([&](BView& view) {
		static_cast<BTextView&>(view).GetSelection(&caret, &caretEnd);
	});
	const auto readOnly = driver.PerformAccessibleAction(window,
		window.AccessibleTree().FindByName("Notes")->id, AccessibleAction::ReplaceSelectedText,
		first);
	BMessage past;
	(void)past.AddInt32("anchorItem", 0);
	(void)past.AddInt32("anchorCharacter", 40);
	(void)past.AddInt32("focusItem", 0);
	(void)past.AddInt32("focusCharacter", 40);
	const auto nowhere = driver.PerformAccessibleAction(window, editor,
		AccessibleAction::SetTextSelection, past);

	// Then
	const AccessibleNode* node = lines.FindByName("Editor");
	ASSERT_NE(node, nullptr);
	EXPECT_EQ(node->info.role, AccessibleRole::MultilineTextInput);
	EXPECT_EQ(node->info.textSelection, (AccessibleTextSelection{{0, 7}, {0, 9}}));
	const std::vector<const AccessibleNode*> runs = runsOf(lines, "Editor");
	ASSERT_EQ(runs.size(), 2u);
	ASSERT_TRUE(runs[0] != nullptr && runs[1] != nullptr);
	EXPECT_EQ(runs[0]->info.role, AccessibleRole::TextRun);
	EXPECT_EQ(runs[0]->info.value, "Hello w\xC3\xB6rld\n");
	EXPECT_EQ(runs[0]->info.characterLengths,
		(std::vector<std::uint8_t>{1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1, 1}));
	EXPECT_EQ(runs[0]->info.wordStarts, (std::vector<std::uint8_t>{0, 6}));
	ASSERT_EQ(runs[0]->info.characterPositions.size(), 12u);
	EXPECT_EQ(runs[0]->info.characterPositions[1], 7.0f);
	EXPECT_EQ(runs[0]->info.characterPositions[7], 49.0f);
	EXPECT_EQ(runs[0]->info.characterWidths[7], 14.0f);
	EXPECT_EQ(runs[0]->info.characterPositions[8], 63.0f);
	EXPECT_EQ(runs[0]->info.characterWidths[11], 0.0f);
	EXPECT_EQ(runs[1]->info.value, "Second line");
	EXPECT_EQ(runs[1]->info.wordStarts, (std::vector<std::uint8_t>{0, 7}));
	EXPECT_GT(runs[1]->bounds.y0, runs[0]->bounds.y0);
	const std::vector<const AccessibleNode*> longRuns = runsOf(longLine, "Editor");
	ASSERT_EQ(longRuns.size(), 2u);
	EXPECT_EQ(longRuns[0]->info.characterLengths.size(), 255u);
	EXPECT_EQ(longRuns[1]->info.characterLengths.size(), 45u);
	const std::vector<const AccessibleNode*> endedRuns = runsOf(ended, "Editor");
	ASSERT_EQ(endedRuns.size(), 2u);
	EXPECT_EQ(endedRuns[1]->info.value, "");
	EXPECT_EQ(ended.FindByName("Editor")->info.textSelection,
		(AccessibleTextSelection{{1, 0}, {1, 0}}));
	EXPECT_TRUE(selected.has_value()) << selected.error().message;
	EXPECT_EQ(start, 13);
	EXPECT_EQ(end, 19);
	EXPECT_TRUE(replaced.has_value()) << replaced.error().message;
	EXPECT_EQ(text, "Hello w\xC3\xB6rld\nFirst line");
	EXPECT_EQ(caret, 18);
	EXPECT_EQ(caretEnd, 18);
	EXPECT_EQ(readOnly.error().code, ErrorCode::NotSupported);
	EXPECT_EQ(nowhere.error().code, ErrorCode::InvalidArgument);
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
	for (int role = 0; role <= static_cast<int>(AccessibleRole::TextRun); role++)
		names.emplace_back(AccessibleRoleName(static_cast<AccessibleRole>(role)));
	AccessibleTree broken;
	AccessibleNode window;
	window.id = 1;
	window.info.role = AccessibleRole::Window;
	window.info.name = "W";
	window.bounds = {0, 0, 10, 10};
	// not `= {2}`: GCC warns wrongly about the bounds of the initializer
	// list (-Warray-bounds) in optimized builds, as in Window.cpp
	window.children.push_back(2);
	broken.nodes.push_back(window);

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
