/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/PropertyInfo.h>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Box.h>
#include <ngui/CheckBox.h>
#include <ngui/ListView.h>
#include <ngui/Looper.h>
#include <ngui/StringItem.h>
#include <ngui/TextControl.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>

#include "support/FixedTextSystem.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// What the list sends when it is invoked.
constexpr std::uint32_t kInvoked = FourCharCode("INVK");


// The window "Settings": a box "box" with a check box "check", a text
// control "name" and a list "list" of four items, which sends kInvoked to
// the window when invoked.
class SettingsWindow : public BWindow {
public:
	SettingsWindow()
		:
		BWindow({100, 100, 400, 300}, "Settings", BWindowLook::Titled)
	{
		auto box = std::make_unique<BBox>(gfx::Rect{10, 10, 290, 190}, "box");
		box->AddChild(std::make_unique<BCheckBox>(gfx::Rect{10, 10, 200, 30}, "check",
			"Save automatically", nullptr));
		box->AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 40, 250, 60}, "name",
			"Name:", "Haiku", nullptr));
		auto list = std::make_unique<BListView>(gfx::Rect{10, 70, 250, 170}, "list",
			BListViewType::MultipleSelection);
		for (const char* item : {"One", "Two", "Three", "Four"})
			list->AddItem(std::make_unique<BStringItem>(item));
		list->SetInvocationMessage(std::make_unique<BMessage>(kInvoked));
		box->AddChild(std::move(list));
		AddChild(std::move(box));
	}
};


// A scripting message: \a command with the specifiers as a reader says
// them ("Value of View check of Window Settings" is {Value, View check,
// Window Settings}), added in that order: the last one added, the window,
// is resolved first.
struct Target {
	std::string_view	property;
	std::string_view	name = {};
	std::int32_t		index = -1;
};


BMessage
Script(std::uint32_t command, std::vector<Target> path)
{
	BMessage message(command);
	for (auto target = path.begin(); target != path.end(); ++target) {
		if (!target->name.empty())
			(void)message.AddSpecifier(target->property, target->name);
		else if (target->index >= 0)
			(void)message.AddSpecifier(target->property, target->index);
		else
			(void)message.AddSpecifier(target->property);
	}
	return message;
}


class Session {
public:
	Session()
		:
		fApp("application/x-vnd.ngui-scripting-test"),
		fDriver(fApp),
		fAppMessenger(&fApp)
	{
		TextSystem::SetDefault(&fText);
		auto window = std::make_unique<SettingsWindow>();
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver.WaitForIdle();
	}

	~Session()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		TextSystem::SetDefault(nullptr);
	}

	// \a message to the application, the reply.
	BMessage Ask(const BMessage& message)
	{
		BMessage reply;
		EXPECT_TRUE(fAppMessenger.SendMessage(message, &reply, 5s).has_value());
		return reply;
	}

	BMessage Get(std::vector<Target> path)
	{
		return Ask(Script(kGetProperty, std::move(path)));
	}

	template<typename Add>
	BMessage Set(std::vector<Target> path, Add add)
	{
		BMessage message = Script(kSetProperty, std::move(path));
		add(message);
		return Ask(message);
	}

private:
	TestEnvironment		fEnvironment;
	FixedTextSystem		fText;
	BApplication		fApp;
	Driver				fDriver;
	BMessenger			fAppMessenger;
	BMessenger			fWindow;
};


const Target kSettings{"Window", "Settings"};


TEST(ScriptingPropertiesTest, TheApplicationAndItsWindowsAnswer)
{
	// What is tested: Scripting messages to the application: its "Name"
	// and its count of "Window"; "Title" and "Frame" of Window 0 and of
	// Window "Settings", setting the title and asking by the new one;
	// counting "View" of the window; "Name" of View 0 of the window;
	// "Hidden" of the window; a window that does not exist.
	// Success criteria: The name is the signature, there is 1 window; the
	// title is "Settings" by index and by name; the frame is its content
	// area; after setting "Title" to "Preferences" the window answers to
	// that name; it has 1 top view, named "box"; it is not hidden; Window
	// "Missing" is NotFound, Window 5 OutOfRange.
	// Why it matters: The application forwards a message for a window to
	// that window's looper with the reply still owed to the sender
	// (specification 20.7), the way hey reaches every window of an
	// application.
	//
	// Given
	Session session;

	// When
	const BMessage name = session.Get({{"Name"}});
	const BMessage count = session.Ask(Script(kCountProperties, {{"Window"}}));
	const BMessage byIndex = session.Get({{"Title"}, {"Window", {}, 0}});
	const BMessage frame = session.Get({{"Frame"}, kSettings});
	const BMessage retitled = session.Set({{"Title"}, kSettings},
		[](BMessage& message) { (void)message.AddString("data", "Preferences"); });
	const BMessage byNewName = session.Get({{"Title"}, {"Window", "Preferences"}});
	const BMessage views = session.Ask(Script(kCountProperties, {{"View"},
		{"Window", "Preferences"}}));
	const BMessage topView = session.Get({{"Name"}, {"View", {}, 0},
		{"Window", "Preferences"}});
	const BMessage hidden = session.Get({{"Hidden"}, {"Window", "Preferences"}});
	const BMessage missing = session.Get({{"Title"}, {"Window", "Missing"}});
	const BMessage outOfRange = session.Get({{"Title"}, {"Window", {}, 5}});

	// Then
	EXPECT_EQ(name.GetString("result", ""), "application/x-vnd.ngui-scripting-test");
	EXPECT_EQ(count.GetInt32("result", 0), 1);
	EXPECT_EQ(byIndex.GetString("result", ""), "Settings");
	EXPECT_EQ(frame.GetRect("result", {}), (gfx::Rect{100, 100, 400, 300}));
	EXPECT_TRUE(ScriptingResult(retitled).has_value()) << retitled.ToString();
	EXPECT_EQ(byNewName.GetString("result", ""), "Preferences");
	EXPECT_EQ(views.GetInt32("result", 0), 1);
	EXPECT_EQ(topView.GetString("result", ""), "box");
	EXPECT_FALSE(hidden.GetBool("result", true));
	EXPECT_EQ(ScriptingResult(missing).error().code, ErrorCode::NotFound);
	EXPECT_EQ(ScriptingResult(outOfRange).error().code, ErrorCode::OutOfRange);
}


TEST(ScriptingPropertiesTest, ControlsAnswerTheirProperties)
{
	// What is tested: The check box and the text control of the window,
	// named from the window down (View "check" is a grandchild): get and
	// set "Value", "Label" and "Enabled" of the check box; get and set the
	// text control's text as "Text" and as Haiku's "Value"; a view that
	// does not exist; a property no one has; the suites of the text
	// control.
	// Success criteria: The check box is off, then on after setting Value
	// 1; its label is "Save automatically"; it is disabled after setting
	// Enabled false. The text is "Haiku", then "NewGUI" after setting
	// "Value". View "absent" is NotFound; "Colour" is BadScriptSyntax. The
	// suites name text control, control, view and handler.
	// Why it matters: These are the standard properties of specification
	// 20.7 (Control: Value, Label; TextControl: Text) as Haiku's classes
	// have them; tests and remote control set and read controls through
	// them.
	//
	// Given
	Session session;
	const Target check{"View", "check"};
	const Target name{"View", "name"};

	// When
	const BMessage off = session.Get({{"Value"}, check, kSettings});
	(void)session.Set({{"Value"}, check, kSettings},
		[](BMessage& message) { (void)message.AddInt32("data", 1); });
	const BMessage on = session.Get({{"Value"}, check, kSettings});
	const BMessage label = session.Get({{"Label"}, check, kSettings});
	(void)session.Set({{"Enabled"}, check, kSettings},
		[](BMessage& message) { (void)message.AddBool("data", false); });
	const BMessage enabled = session.Get({{"Enabled"}, check, kSettings});
	const BMessage text = session.Get({{"Text"}, name, kSettings});
	(void)session.Set({{"Value"}, name, kSettings},
		[](BMessage& message) { (void)message.AddString("data", "NewGUI"); });
	const BMessage changed = session.Get({{"Text"}, name, kSettings});
	const BMessage absent = session.Get({{"Value"}, {"View", "absent"}, kSettings});
	const BMessage colour = session.Get({{"Colour"}, check, kSettings});
	const BMessage suites = session.Get({{"Suites"}, name, kSettings});

	// Then
	EXPECT_EQ(off.GetInt32("result", -1), 0);
	EXPECT_EQ(on.GetInt32("result", -1), 1);
	EXPECT_EQ(label.GetString("result", ""), "Save automatically");
	EXPECT_FALSE(enabled.GetBool("result", true));
	EXPECT_EQ(text.GetString("result", ""), "Haiku");
	EXPECT_EQ(changed.GetString("result", ""), "NewGUI");
	EXPECT_EQ(ScriptingResult(absent).error().code, ErrorCode::NotFound);
	EXPECT_EQ(ScriptingResult(colour).error().code, ErrorCode::BadScriptSyntax);
	EXPECT_EQ(suites.GetString("suites", ""), "suite/vnd.Be-text-control");
	EXPECT_EQ(suites.FindString("suites", 1).value_or(""), "suite/vnd.Be-control");
	EXPECT_EQ(suites.FindString("suites", 2).value_or(""), "suite/vnd.Be-view");
	EXPECT_EQ(suites.FindString("suites", 3).value_or(""), "suite/vnd.Be-handler");
}


TEST(ScriptingPropertiesTest, ListsSelectAndInvokeByScript)
{
	// What is tested: The multiple selection list "list" by Haiku's
	// properties: count "Item"; select items 1 and 3 (Selection by index,
	// data true); get and count the "Selection"; deselect item 1; execute
	// Item 2 (select it alone and invoke); select a range; an index
	// beyond the list.
	// Success criteria: 4 items; after selecting 1 and 3 the selection is
	// {1, 3}, 2 items; after deselecting 1 it is {3}; executing Item 2
	// leaves {2}; the range of 2 from 0 extends that to {0, 1, 2}, as
	// Haiku's multiple selection list does; Item 7 is OutOfRange.
	// Why it matters: ListView: Items, Selection (specification 20.7); a
	// test or a remote script picks entries the way a user does.
	//
	// Given
	Session session;
	const Target list{"View", "list"};
	const auto selection = [&] {
		const BMessage reply = session.Get({{"Selection"}, list, kSettings});
		std::vector<std::int32_t> indices;
		for (std::int32_t i = 0; reply.HasInt32("result", i); i++)
			indices.push_back(*reply.FindInt32("result", i));
		return indices;
	};
	const auto select = [](bool on) {
		return [on](BMessage& message) { (void)message.AddBool("data", on); };
	};

	// When
	const BMessage items = session.Ask(Script(kCountProperties, {{"Item"}, list, kSettings}));
	(void)session.Set({{"Selection", {}, 1}, list, kSettings}, select(true));
	(void)session.Set({{"Selection", {}, 3}, list, kSettings}, select(true));
	const std::vector<std::int32_t> both = selection();
	const BMessage count = session.Ask(Script(kCountProperties, {{"Selection"}, list,
		kSettings}));
	(void)session.Set({{"Selection", {}, 1}, list, kSettings}, select(false));
	const std::vector<std::int32_t> third = selection();
	const BMessage executed = session.Ask(Script(kExecuteProperty, {{"Item", {}, 2}, list,
		kSettings}));
	const std::vector<std::int32_t> second = selection();
	BMessage range(kSetProperty);
	(void)range.AddSpecifier("Selection", 0, 2);
	(void)range.AddSpecifier("View", "list");
	(void)range.AddSpecifier("Window", "Settings");
	(void)range.AddBool("data", true);
	const BMessage ranged = session.Ask(range);
	const std::vector<std::int32_t> first = selection();
	const BMessage beyond = session.Ask(Script(kExecuteProperty, {{"Item", {}, 7}, list,
		kSettings}));

	// Then
	EXPECT_EQ(items.GetInt32("result", 0), 4);
	EXPECT_EQ(both, (std::vector<std::int32_t>{1, 3}));
	EXPECT_EQ(count.GetInt32("result", 0), 2);
	EXPECT_EQ(third, (std::vector<std::int32_t>{3}));
	EXPECT_TRUE(ScriptingResult(executed).has_value()) << executed.ToString();
	EXPECT_EQ(second, (std::vector<std::int32_t>{2}));
	EXPECT_TRUE(ScriptingResult(ranged).has_value()) << ranged.ToString();
	EXPECT_EQ(first, (std::vector<std::int32_t>{0, 1, 2}));
	EXPECT_EQ(ScriptingResult(beyond).error().code, ErrorCode::OutOfRange);
}


} // namespace
