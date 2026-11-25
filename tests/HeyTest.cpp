/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "remote/Hey.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/CheckBox.h>
#include <ngui/Looper.h>
#include <ngui/PropertyInfo.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "support/FixedTextSystem.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::remote::detail;
using namespace ngui::test;
using namespace std::chrono_literals;


// The words of \a command, split at spaces (no quoting of spaces).
std::vector<std::string>
Split(const std::string& command)
{
	std::vector<std::string> words;
	std::istringstream stream(command);
	for (std::string word; stream >> word;)
		words.push_back(word);
	return words;
}


std::expected<BMessage, Error>
Parse(const std::string& command)
{
	return ParseHeyCommand(Split(command));
}


// The specifier \a index of \a message.
BMessage
SpecifierAt(const BMessage& message, std::int32_t index)
{
	return message.FindMessage("specifiers", index).value_or(BMessage());
}


TEST(HeyTest, CommandsBecomeScriptingMessages)
{
	// What is tested: ParseHeyCommand() with hey's forms: a direct and an
	// index specifier; a quoted name of digits and a bare name; "the" and
	// a bare reverse index with BRect; a range in brackets; a string, a
	// bool, an integer and a float as values; create with "with … and …";
	// count and getsuites; an rgb_color.
	// Success criteria: The verbs become kGetProperty … kGetSupportedSuites;
	// the specifiers are added in the order they are read, so that the last
	// word ("Window 0") is resolved first; each specifier has its form and
	// fields as hey makes them ("[1 to 3]" is index 1, range 2); the values
	// land in "data" or the named fields with their types.
	// Why it matters: ngui-hey speaks the language of Haiku's hey
	// (specification 20.7); scripts written for hey must mean the same.
	//
	// When
	const auto title = Parse("get Title of Window 0");
	const auto quoted = Parse("get Value of View \"42\" of Window Settings");
	const auto frame = Parse("set Frame of the Window -1 to BRect(10,20,110,220)");
	const auto range = Parse("do Selection [1 to 3] of View list");
	const auto label = Parse("set Label of View check to \"Save\"");
	const auto flag = Parse("set Enabled of View check to false");
	const auto number = Parse("set Value of View slider to 42");
	const auto real = Parse("set Scale of View zoom to 1.5");
	const auto created = Parse("create Item with name=\"Five\" and index=int32(4)");
	const auto count = Parse("count Window");
	const auto suites = Parse("getsuites");
	const auto color = Parse("set Color of View box to rgb_color(255,0,0,255)");

	// Then
	ASSERT_TRUE(title.has_value()) << title.error().message;
	EXPECT_EQ(title->what, kGetProperty);
	EXPECT_EQ(title->CurrentSpecifierIndex(), 1);
	EXPECT_EQ(SpecifierAt(*title, 0).what, kDirectSpecifier);
	EXPECT_EQ(SpecifierAt(*title, 0).GetString("property", ""), "Title");
	EXPECT_EQ(SpecifierAt(*title, 1).what, kIndexSpecifier);
	EXPECT_EQ(SpecifierAt(*title, 1).GetInt32("index", -1), 0);

	ASSERT_TRUE(quoted.has_value());
	EXPECT_EQ(SpecifierAt(*quoted, 1).what, kNameSpecifier);
	EXPECT_EQ(SpecifierAt(*quoted, 1).GetString("name", ""), "42");
	EXPECT_EQ(SpecifierAt(*quoted, 2).GetString("name", ""), "Settings");

	ASSERT_TRUE(frame.has_value()) << frame.error().message;
	EXPECT_EQ(frame->what, kSetProperty);
	EXPECT_EQ(SpecifierAt(*frame, 1).what, kReverseIndexSpecifier);
	EXPECT_EQ(SpecifierAt(*frame, 1).GetInt32("index", 0), 1);
	EXPECT_EQ(frame->GetRect("data", {}), (gfx::Rect{10, 20, 110, 220}));

	ASSERT_TRUE(range.has_value()) << range.error().message;
	EXPECT_EQ(range->what, kExecuteProperty);
	EXPECT_EQ(SpecifierAt(*range, 0).what, kRangeSpecifier);
	EXPECT_EQ(SpecifierAt(*range, 0).GetInt32("index", 0), 1);
	EXPECT_EQ(SpecifierAt(*range, 0).GetInt32("range", 0), 2);

	EXPECT_EQ(label->GetString("data", ""), "Save");
	EXPECT_FALSE(flag->GetBool("data", true));
	EXPECT_EQ(number->GetInt32("data", 0), 42);
	EXPECT_EQ(real->GetFloat("data", 0), 1.5f);

	ASSERT_TRUE(created.has_value()) << created.error().message;
	EXPECT_EQ(created->what, kCreateProperty);
	EXPECT_EQ(SpecifierAt(*created, 0).GetString("property", ""), "Item");
	EXPECT_EQ(created->GetString("name", ""), "Five");
	EXPECT_EQ(created->GetInt32("index", 0), 4);

	EXPECT_EQ(count->what, kCountProperties);
	EXPECT_EQ(SpecifierAt(*count, 0).GetString("property", ""), "Window");
	EXPECT_EQ(suites->what, kGetSupportedSuites);
	EXPECT_FALSE(suites->HasSpecifiers());
	EXPECT_EQ(color->GetColor("data", {}), (gfx::Color{1, 0, 0, 1}));
}


TEST(HeyTest, BadCommandsSayWhy)
{
	// What is tested: An unknown verb, set without a value, "of" at the
	// end, a range without "to", words after the value, no words, and a
	// field given twice with two types.
	// Success criteria: Each is BadScriptSyntax with a message that names
	// the problem.
	// Why it matters: The tool tells the user what is wrong with the
	// command instead of sending something else.
	//
	// When / Then
	const std::vector<std::string> bad = {"jump Title", "set Title of Window 0",
		"get Title of", "do Item [1 x 3]", "set Title to x y", "",
		"create Item with x=int32(1) and x=\"a\""};
	for (const std::string& command : bad) {
		const auto parsed = Parse(command);
		ASSERT_FALSE(parsed.has_value()) << command;
		EXPECT_EQ(parsed.error().code, ErrorCode::BadScriptSyntax) << command;
		EXPECT_FALSE(parsed.error().message.empty()) << command;
	}
}


TEST(HeyTest, RepliesArePrintedAsHeyDoes)
{
	// What is tested: DescribeReply() and ResultText() of a reply with a
	// string, an int32 error, a rectangle and a property table, and of one
	// with two int32 results.
	// Success criteria: "Reply BMessage(B_REPLY):", a line per item with
	// hey's type names (the int32 also in hex), the table with its
	// property, commands, forms and usage; ResultText() is the bare values,
	// one per line.
	// Why it matters: People and scripts read the output; -o gives values a
	// shell can use.
	//
	// Given
	BMessage reply(kReply);
	(void)reply.AddString("result", "Settings");
	(void)reply.AddInt32("error", 0);
	(void)reply.AddRect("frame", {0, 0, 10, 20});
	const std::vector<BPropertyInfoEntry> table = {
		{"Title", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Its title.", 0,
			{BTypeCode::String}}};
	(void)reply.AddMessage("messages", BPropertyInfo(table).ToMessage());
	BMessage selection(kReply);
	(void)selection.AddInt32("result", 1);
	(void)selection.AddInt32("result", 3);

	// When
	const std::string text = DescribeReply(reply);
	const std::string result = ResultText(reply);
	const std::string indices = ResultText(selection);

	// Then
	EXPECT_NE(text.find("Reply BMessage(B_REPLY):"), std::string::npos) << text;
	EXPECT_NE(text.find("\"result\" (B_STRING_TYPE) : \"Settings\""), std::string::npos);
	EXPECT_NE(text.find("\"error\" (B_INT32_TYPE) : 0 (0x00000000)"), std::string::npos);
	EXPECT_NE(text.find("BRect(0.0, 0.0, 10.0, 20.0)"), std::string::npos);
	EXPECT_NE(text.find("Title   B_GET_PROPERTY B_SET_PROPERTY"), std::string::npos) << text;
	EXPECT_NE(text.find("DIRECT"), std::string::npos);
	EXPECT_NE(text.find("Usage: Its title."), std::string::npos);
	EXPECT_EQ(result, "Settings\n");
	EXPECT_EQ(indices, "1\n3\n");
}


#ifdef NGUI_TEST_HEY
// What ngui-hey prints for \a arguments, and its exit status.
struct HeyRun {
	std::string	output;
	int			status;
};


HeyRun
RunHey(const std::string& arguments)
{
	const std::string command = std::string("\"") + NGUI_TEST_HEY + "\" " + arguments;
#ifdef _WIN32
	FILE* pipe = _popen(command.c_str(), "r");
#else
	FILE* pipe = popen(command.c_str(), "r");
#endif
	HeyRun run{{}, -1};
	if (pipe == nullptr)
		return run;
	std::array<char, 256> buffer{};
	while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
		run.output += buffer.data();
#ifdef _WIN32
	run.status = _pclose(pipe);
#else
	const int status = pclose(pipe);
	run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
	return run;
}


TEST(HeyTest, TheToolScriptsARunningApplication)
{
	// What is tested: The built ngui-hey against a running application with
	// remote control in the user's runtime directory (a temporary one):
	// list the applications; get the title of Window 0 with -o; set the
	// check box by name chain and read it back; an application that does
	// not exist; a bad verb.
	// Success criteria: -l lists the process with its signature; the title
	// is "Settings"; after "set … to 1" the value is 1 and the status 0;
	// a missing application is status 1, a bad command status 2.
	// Why it matters: This is the tool of specification 20.7 end to end:
	// another process, the socket, scripting, the reply on stdout.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
#ifdef _WIN32
	const std::int64_t process = _getpid();
#else
	const std::int64_t process = getpid();
#endif
	const std::filesystem::path runtime = std::filesystem::temp_directory_path()
		/ ("nh" + std::to_string(process));
	std::error_code ignored;
	std::filesystem::remove_all(runtime, ignored);
	std::filesystem::create_directories(runtime);
#ifdef _WIN32
	const ScopedEnvironment place("LOCALAPPDATA", runtime.string().c_str());
#else
	std::filesystem::permissions(runtime, std::filesystem::perms::owner_all);
	const ScopedEnvironment place("XDG_RUNTIME_DIR", runtime.string().c_str());
#endif
	BApplication app("application/x-vnd.nh-test");
	Driver driver(app);
	auto window = std::make_unique<BWindow>(gfx::Rect{0, 0, 200, 100}, "Settings",
		BWindowLook::Titled);
	window->AddChild(std::make_unique<BCheckBox>(gfx::Rect{10, 10, 190, 30}, "check",
		"Save", nullptr));
	window->Show();
	const BMessenger windowMessenger = *BLooper::Run(std::move(window));
	(void)driver.WaitForIdle();
	ASSERT_TRUE(app.EnableRemoteControl().has_value());

	// When
	const HeyRun list = RunHey("-l");
	const HeyRun title = RunHey("-o nh-test get Title of Window 0");
	const HeyRun set = RunHey("nh-test set Value of View check of Window Settings to 1");
	const HeyRun value = RunHey("-o nh-test get Value of View check of Window Settings");
	const HeyRun missing = RunHey("-s no-such-app get Title of Window 0");
	const HeyRun bad = RunHey("-s nh-test jump Title");
	app.DisableRemoteControl();
	(void)windowMessenger.SendMessage(kQuitRequested);
	(void)windowMessenger.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);
	std::filesystem::remove_all(runtime, ignored);

	// Then
	EXPECT_NE(list.output.find(std::to_string(process) + "\tapplication/x-vnd.nh-test"),
		std::string::npos) << list.output;
	EXPECT_EQ(title.output, "Settings\n");
	EXPECT_EQ(title.status, 0);
	EXPECT_EQ(set.status, 0) << set.output;
	EXPECT_NE(set.output.find("Reply BMessage(B_REPLY):"), std::string::npos);
	EXPECT_EQ(value.output, "1\n");
	EXPECT_EQ(missing.status, 1);
	EXPECT_EQ(bad.status, 2);
}
#endif


} // namespace
