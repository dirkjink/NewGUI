/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Recording.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/CheckBox.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Looper.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/PopUpMenu.h>
#include <ngui/TextControl.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>
#include <ngui/test/Driver.h>

#include "record/Recorder.h"
#include "support/FixedTextSystem.h"
#include "support/ScopedEnvironment.h"

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A login form: a name, a password, a card number marked private, a check
// box and a menu field of sizes.
class LoginWindow : public BWindow {
public:
	LoginWindow()
		:
		BWindow({100, 100, 400, 300}, "Login", BWindowLook::Titled)
	{
		AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 10, 290, 30}, "name", "Name:",
			"", nullptr));
		auto password = std::make_unique<BTextControl>(gfx::Rect{10, 40, 290, 60}, "password",
			"Password:", "", nullptr);
		password->TextView()->HideTyping(true);
		AddChild(std::move(password));
		auto card = std::make_unique<BTextControl>(gfx::Rect{10, 70, 290, 90}, "card",
			"Card:", "", nullptr);
		card->SetInputPrivate(true);
		AddChild(std::move(card));
		AddChild(std::make_unique<BCheckBox>(gfx::Rect{10, 100, 200, 120}, "remember",
			"Remember me", nullptr));
		auto sizes = std::make_unique<BPopUpMenu>("size");
		for (const char* size : {"Small", "Medium", "Large"})
			sizes->AddItem(std::make_unique<BMenuItem>(size, nullptr));
		sizes->ItemAt(0)->SetMarked(true);
		AddChild(std::make_unique<BMenuField>(gfx::Rect{10, 130, 210, 154}, "size", "Size:",
			std::move(sizes), true));
	}
};


BMessenger
OpenLogin()
{
	auto window = std::make_unique<LoginWindow>();
	window->Show();
	return *BLooper::Run(std::move(window));
}


void
Close(const BMessenger& window)
{
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
}


// What the login form shows.
struct Form {
	std::string	name;
	std::string	password;
	std::string	card;
	std::int32_t remember = -1;
	std::string	size;
};


Form
Read(Driver& driver)
{
	WindowRef window = driver.Window("Login");
	Form form;
	form.name = window.Find("name").Text();
	form.password = window.Find("password").Text();
	form.card = window.Find("card").Text();
	form.remember = window.Find("remember").Value().value_or(-1);
	form.size = window.Find("size").With<BMenuField>([](BMenuField& field) {
		const BMenuItem* marked = field.Menu()->FindMarked();
		return marked != nullptr ? marked->Label() : std::string();
	}).value_or("");
	return form;
}


// Fills in the form with the mouse and the keyboard, choosing "Large" in the
// size menu, a second between each step.
void
FillIn(Driver& driver)
{
	WindowRef window = driver.Window("Login");
	ASSERT_TRUE(driver.Type(window.Find("name"), "Ada").has_value());
	ASSERT_TRUE(driver.Advance(1s).has_value());
	ASSERT_TRUE(driver.Type(window.Find("password"), "s3cr\xC3\xABt").has_value());
	ASSERT_TRUE(driver.Key(keys::kBackspace).has_value());
	ASSERT_TRUE(driver.Advance(1s).has_value());
	ASSERT_TRUE(driver.Type(window.Find("card"), "4242").has_value());
	ASSERT_TRUE(driver.Click(window.Find("remember")).has_value());
	const std::vector<ViewRef> bars = window.FindAll<BMenuBar>();
	ASSERT_EQ(bars.size(), 1u);
	ASSERT_TRUE(driver.Click(bars.front()).has_value());
	WindowRef popup;
	for (WindowRef& candidate : driver.Windows()) {
		if (candidate.PlatformWindow() != nullptr && candidate.PlatformWindow()->IsPopup())
			popup = candidate;
	}
	ASSERT_TRUE(popup.IsValid());
	const gfx::Rect large = window.Find("size").With<BMenuField>([](BMenuField& field) {
		return field.Menu()->ItemAt(2)->Frame();
	}).value_or(gfx::Rect{});
	ASSERT_TRUE(driver.ClickAt(popup, {large.x0 + 5, (large.y0 + large.y1) / 2}).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
}


std::vector<std::byte>
Bytes(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	std::vector<char> chars((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	return std::vector<std::byte>(reinterpret_cast<const std::byte*>(chars.data()),
		reinterpret_cast<const std::byte*>(chars.data()) + chars.size());
}


bool
Contains(const std::vector<std::byte>& bytes, std::string_view text)
{
	const auto* data = reinterpret_cast<const char*>(bytes.data());
	return std::string_view(data, bytes.size()).find(text) != std::string_view::npos;
}


std::filesystem::path
TemporaryFile(const char* name)
{
#ifdef _WIN32
	const auto process = _getpid();
#else
	const auto process = getpid();
#endif
	return std::filesystem::temp_directory_path()
		/ (std::string(name) + "-" + std::to_string(process) + ".ngrec");
}


TEST(RecordingTest, AFormFilledInIsReplayedHeadless)
{
	// What is tested: An application with the login form records into a
	// file (StartRecording()) while the driver types "Ada" as the name, a
	// password with "ë" and Backspace, "4242" as the card number (a view
	// marked private), clicks the check box and chooses "Large" in the
	// size menu (a popup), with a second of the clock between steps. Then
	// the form is opened anew and the recording loaded and replayed.
	// Success criteria: The file begins with one window "Login" and the look
	// "be"; its first events are the window's frame and scale; nothing in
	// it is the password or the card number, their keys are masked; the
	// name's keys are in it; the events carry the clock's seconds; a popup
	// event goes to popup 0; on POSIX only the user may read the file.
	// Replayed, the new form has "Ada", a password and a card number of
	// bullets (as many characters as typed), the check box on and "Large";
	// the replay took the recorded time on the virtual clock.
	// Why it matters: A bug met in use becomes a regression test
	// (specification 20.8), and no password or private number is in the
	// recording (rule T5).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-recording-test");
	Driver driver(app);
	ASSERT_TRUE(driver.InitCheck().has_value());
	const std::filesystem::path file = TemporaryFile("ngui-recording");
	BMessenger login = OpenLogin();
	ASSERT_TRUE(driver.WaitForIdle().has_value());

	// When
	ASSERT_TRUE(app.StartRecording(file).has_value());
	const bool recording = app.IsRecording();
	const auto again = app.StartRecording(file);
	FillIn(driver);
	const Form filled = Read(driver);
	app.StopRecording();
	const std::vector<std::byte> bytes = Bytes(file);
	const auto loaded = Recording::Load(file);
	std::error_code ignored;
#ifndef _WIN32
	const auto permissions = std::filesystem::status(file).permissions();
#endif
	std::filesystem::remove(file, ignored);
	Close(login);
	login = OpenLogin();
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const Clock::Time before = environment.Clock().Now();
	ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
	const auto replayed = driver.Replay(*loaded);
	const Clock::Time took = environment.Clock().Now() - before;
	const Form form = Read(driver);
	Close(login);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_TRUE(recording);
	EXPECT_FALSE(app.IsRecording());
	EXPECT_EQ(again.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(filled.size, "Large");
	ASSERT_EQ(loaded->start.windows.size(), 1u);
	EXPECT_EQ(loaded->start.windows[0].title, "Login");
	EXPECT_EQ(loaded->start.look, "be");
	EXPECT_EQ(loaded->start.signature, "application/x-vnd.ngui-recording-test");
	ASSERT_GE(loaded->events.size(), 2u);
	EXPECT_EQ(loaded->events[0].message.what, kWindowMoved);
	EXPECT_EQ(loaded->events[0].window, 0);
	EXPECT_EQ(loaded->events[1].message.what, kWindowResized);
	EXPECT_EQ(loaded->events[1].message.GetFloat("width", 0), 300.0f);
	EXPECT_FALSE(Contains(bytes, "s3cr"));
	EXPECT_FALSE(Contains(bytes, "\xC3\xAB"));
	EXPECT_FALSE(Contains(bytes, "4242"));
	EXPECT_TRUE(Contains(bytes, "•"));
	const auto masked = std::ranges::count_if(loaded->events,
		[](const RecordedEvent& event) { return event.masked; });
	EXPECT_EQ(masked, 2 * (6 + 1 + 4));
	const auto popupEvents = std::ranges::count_if(loaded->events,
		[](const RecordedEvent& event) { return event.popup == 0; });
	EXPECT_GE(popupEvents, 1);
	EXPECT_EQ(loaded->events.back().time, Clock::Time(2s));
#ifndef _WIN32
	EXPECT_EQ(permissions & std::filesystem::perms::all, std::filesystem::perms::owner_read
		| std::filesystem::perms::owner_write);
#endif
	ASSERT_TRUE(replayed.has_value()) << replayed.error().message;
	EXPECT_EQ(took, Clock::Time(2s));
	EXPECT_EQ(form.name, "Ada");
	EXPECT_EQ(form.password, "•••••");
	EXPECT_EQ(form.card, "••••");
	EXPECT_EQ(form.remember, 1);
	EXPECT_EQ(form.size, "Large");
}


TEST(RecordingTest, ReplayNamesWhatIsMissing)
{
	// What is tested: Replaying a recording that began with two windows into
	// one, one whose event goes to a window that is never made, and one
	// whose event goes to a popup that is not open; and replaying without
	// a ManualClock.
	// Success criteria: NotFound for the windows of the start, for event 0
	// of the missing window and for the popup; InvalidArgument without the
	// virtual clock.
	// Why it matters: When the application goes another way than when it
	// was recorded, the replay says where instead of sending events
	// anywhere.
	//
	// Given
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	std::expected<void, Error> withoutClock;
	{
		BApplication app("application/x-vnd.ngui-replay-test");
		Driver driver(app);
		withoutClock = driver.Replay(Recording{});
	}
	TestEnvironment environment;
	BApplication app("application/x-vnd.ngui-replay-test");
	Driver driver(app);
	const BMessenger login = OpenLogin();
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	Recording two;
	two.start.windows = {{"Login"}, {"Other"}};
	Recording later;
	later.start.windows = {{"Login"}};
	later.events.push_back({1s, 1, -1, false, BMessage(kMouseMoved)});
	Recording popup;
	popup.start.windows = {{"Login"}};
	popup.events.push_back({0s, 0, 0, false, BMessage(kMouseMoved)});

	// When
	const auto startMissing = driver.Replay(two);
	const auto windowMissing = driver.Replay(later);
	const auto popupMissing = driver.Replay(popup);
	Close(login);
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_EQ(withoutClock.error().code, ErrorCode::InvalidArgument);
	EXPECT_EQ(startMissing.error().code, ErrorCode::NotFound);
	EXPECT_EQ(windowMissing.error().code, ErrorCode::NotFound);
	EXPECT_NE(windowMissing.error().message.find("event 0"), std::string::npos);
	EXPECT_EQ(popupMissing.error().code, ErrorCode::NotFound);
}


TEST(RecordingTest, TheFileIsFramesThatMayBeCutOff)
{
	// What is tested: A recording with a start and three events (one
	// masked, one of a popup, one of the application) written and read
	// back; saved and loaded; read with its last frame cut off, with the
	// first byte of an event's message damaged, without its start, empty,
	// and from a file that is not there.
	// Success criteria: Read back, start and events are as written (time,
	// window, popup, mask, message); cut off, the last event is missing and
	// the rest is there; damaged or without a start it is BadData; a file
	// that cannot be read is IoError.
	// Why it matters: A recording of a crash ends in the middle of a frame;
	// a damaged file must not replay something else.
	//
	// Given
	Recording recording;
	recording.start = {"application/x-vnd.test", "haiku", {"de", "en"}, {{"Main"}}};
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "•");
	recording.events.push_back({250ms, 0, -1, true, key});
	recording.events.push_back({1s, 0, 0, false, BMessage(kMouseDown)});
	recording.events.push_back({2s, -1, -1, false, BMessage(kQuitRequested)});
	const std::filesystem::path file = TemporaryFile("ngui-frames");

	// When
	const auto bytes = recording.Write();
	ASSERT_TRUE(bytes.has_value());
	const auto read = Recording::Read(*bytes);
	const auto saved = recording.Save(file);
	const auto loaded = Recording::Load(file);
	std::error_code ignored;
	std::filesystem::remove(file, ignored);
	const auto cut = Recording::Read(std::span(*bytes).first(bytes->size() - 3));
	// the first byte of the first event's message (its format)
	const std::size_t startSize = Recording{recording.start, {}}.Write()->size();
	std::vector<std::byte> damaged = *bytes;
	damaged[startSize + 4] = std::byte{0xff};
	const auto broken = Recording::Read(damaged);
	const auto withoutStart = Recording::Read(std::span(*bytes).subspan(startSize));
	const auto empty = Recording::Read({});
	const auto missing = Recording::Load(file);

	// Then
	ASSERT_TRUE(read.has_value()) << read.error().message;
	EXPECT_EQ(read->start.signature, "application/x-vnd.test");
	EXPECT_EQ(read->start.look, "haiku");
	EXPECT_EQ(read->start.languages, (std::vector<std::string>{"de", "en"}));
	ASSERT_EQ(read->start.windows.size(), 1u);
	EXPECT_EQ(read->start.windows[0].title, "Main");
	ASSERT_EQ(read->events.size(), 3u);
	EXPECT_EQ(read->events[0].time, Clock::Time(250ms));
	EXPECT_TRUE(read->events[0].masked);
	EXPECT_EQ(read->events[0].message.GetString("bytes", ""), "•");
	EXPECT_EQ(read->events[1].popup, 0);
	EXPECT_EQ(read->events[1].message.what, kMouseDown);
	EXPECT_EQ(read->events[2].window, -1);
	EXPECT_TRUE(saved.has_value());
	ASSERT_TRUE(loaded.has_value());
	EXPECT_EQ(loaded->events.size(), 3u);
	ASSERT_TRUE(cut.has_value());
	EXPECT_EQ(cut->events.size(), 2u);
	EXPECT_EQ(broken.error().code, ErrorCode::BadData);
	EXPECT_EQ(withoutStart.error().code, ErrorCode::BadData);
	EXPECT_EQ(empty.error().code, ErrorCode::BadData);
	EXPECT_EQ(missing.error().code, ErrorCode::IoError);
}


TEST(RecordingTest, TypingIsMaskedButNotItsKeys)
{
	// What is tested: MaskTyping() of a key typing "a", one typing "é" (two
	// bytes), Backspace, Command+A, a space, and an input method composing
	// "aé" with "é" selected (bytes 1 to 3).
	// Success criteria: "a", "é" and the space become one bullet each and
	// the key code 0; Backspace and Command+A stay as they are (shortcuts
	// and editing keys tell nothing typed); the composition becomes two
	// bullets with the selection on the second (bytes 3 to 6).
	// Why it matters: A masked recording still replays the editing, only
	// not the characters (rule T5).
	//
	// Given
	const auto keyOf = [](std::string_view bytes, BModifierFlags modifiers = {}) {
		BMessage key(kKeyDown);
		(void)key.AddInt32("key", 38);
		(void)key.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		(void)key.AddString("bytes", bytes);
		return key;
	};
	BMessage compose(kInputMethodEvent);
	(void)compose.AddInt32("be:opcode", kInputMethodChanged);
	(void)compose.AddString("be:string", "a\xC3\xA9");
	(void)compose.AddInt32("be:selection", 1);
	(void)compose.AddInt32("be:selection", 3);

	// When
	const BMessage a = detail::MaskTyping(keyOf("a"));
	const BMessage accented = detail::MaskTyping(keyOf("\xC3\xA9"));
	const BMessage backspace = detail::MaskTyping(keyOf("\x08"));
	const BMessage selectAll = detail::MaskTyping(keyOf("a", BModifier::Command));
	const BMessage space = detail::MaskTyping(keyOf(" "));
	const BMessage composed = detail::MaskTyping(compose);

	// Then
	EXPECT_EQ(a.GetString("bytes", ""), "•");
	EXPECT_EQ(a.GetInt32("key", -1), 0);
	EXPECT_EQ(accented.GetString("bytes", ""), "•");
	EXPECT_EQ(backspace.GetString("bytes", ""), "\x08");
	EXPECT_EQ(selectAll.GetString("bytes", ""), "a");
	EXPECT_EQ(space.GetString("bytes", ""), "•");
	EXPECT_EQ(composed.GetString("be:string", ""), "••");
	EXPECT_EQ(composed.FindInt32("be:selection", 0).value_or(-1), 3);
	EXPECT_EQ(composed.FindInt32("be:selection", 1).value_or(-1), 6);
	EXPECT_TRUE(detail::IsTyping(kKeyUp));
	EXPECT_FALSE(detail::IsTyping(kMouseDown));
}



TEST(RecordingTest, TheEnvironmentIsTheUsersConsent)
{
	// What is tested: An application started with NGUI_RECORD naming a file,
	// and one started without it.
	// Success criteria: The first records from its start: the file is a
	// recording of its signature without windows; the second does not
	// record.
	// Why it matters: Recording begins only when asked (rule T5): by the
	// application after the user agreed, or by whoever starts it so.
	//
	// Given
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	TestEnvironment environment;
	const std::filesystem::path file = TemporaryFile("ngui-environment");
	bool recording = false;
	bool without = true;

	// When
	{
		const ScopedEnvironment record("NGUI_RECORD", file.string().c_str());
		BApplication app("application/x-vnd.ngui-environment-test");
		recording = app.IsRecording();
	}
	const auto loaded = Recording::Load(file);
	std::error_code ignored;
	std::filesystem::remove(file, ignored);
	{
		BApplication app("application/x-vnd.ngui-environment-test");
		without = app.IsRecording();
	}
	TextSystem::SetDefault(nullptr);

	// Then
	EXPECT_TRUE(recording);
	ASSERT_TRUE(loaded.has_value()) << loaded.error().message;
	EXPECT_EQ(loaded->start.signature, "application/x-vnd.ngui-environment-test");
	EXPECT_TRUE(loaded->start.windows.empty());
	EXPECT_FALSE(without);
}


#ifdef NGUI_TEST_REPLAY
// What ngui-replay prints for \a arguments, and its exit status.
struct ToolRun {
	std::string	output;
	int			status;
};


ToolRun
RunReplay(const std::string& arguments)
{
	const std::string command = std::string("\"") + NGUI_TEST_REPLAY + "\" " + arguments;
#ifdef _WIN32
	FILE* pipe = _popen(command.c_str(), "r");
#else
	FILE* pipe = popen(command.c_str(), "r");
#endif
	ToolRun run{{}, -1};
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


TEST(RecordingTest, TheToolShowsWhatWasRecorded)
{
	// What is tested: The built ngui-replay with --dump of a saved
	// recording (one window, a masked key of the window, a click into its
	// popup), of a file that is not there, and without arguments.
	// Success criteria: It prints the signature, the look, the languages,
	// the window, each event with its time, place and name, the masked one
	// marked, and the items of each; status 0. A missing file is status
	// 1, no arguments status 2.
	// Why it matters: One can see what a recording holds before passing it
	// on (rule T5).
	//
	// Given
	Recording recording;
	recording.start = {"application/x-vnd.test", "be", {"de"}, {{"Main"}}};
	BMessage key(kKeyDown);
	(void)key.AddString("bytes", "\u2022");
	recording.events.push_back({1500ms, 0, -1, true, key});
	recording.events.push_back({2s, 0, 0, false, BMessage(kMouseDown)});
	const std::filesystem::path file = TemporaryFile("ngui-dump");
	ASSERT_TRUE(recording.Save(file).has_value());

	// When
	const ToolRun dump = RunReplay("--dump \"" + file.string() + "\"");
	std::error_code ignored;
	std::filesystem::remove(file, ignored);
	const ToolRun missing = RunReplay("--dump \"" + file.string() + "\" 2>&1");
	const ToolRun usage = RunReplay("2>&1");

	// Then
	EXPECT_EQ(dump.status, 0);
	EXPECT_NE(dump.output.find("Recording of application/x-vnd.test, look \"be\", "
		"languages de"), std::string::npos) << dump.output;
	EXPECT_NE(dump.output.find("window 0: \"Main\""), std::string::npos);
	EXPECT_NE(dump.output.find("1.500 s  window 0"), std::string::npos);
	EXPECT_NE(dump.output.find("B_KEY_DOWN  (masked)"), std::string::npos);
	EXPECT_NE(dump.output.find("\"bytes\" (B_STRING_TYPE) : \"\u2022\""), std::string::npos);
	EXPECT_NE(dump.output.find("window 0, popup 0"), std::string::npos);
	EXPECT_NE(dump.output.find("B_MOUSE_DOWN"), std::string::npos);
	EXPECT_EQ(missing.status, 1);
	EXPECT_EQ(usage.status, 2);
	EXPECT_NE(usage.output.find("usage: ngui-replay"), std::string::npos);
}
#endif


} // namespace
