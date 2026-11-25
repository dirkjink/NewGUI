/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/RemoteControl.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
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
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "remote/LocalSocket.h"
#include "support/FixedTextSystem.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;

// Short, so that the socket paths stay within the limit of AF_UNIX.
constexpr const char* kSignature = "application/x-vnd.nr";


std::int64_t
ThisProcess()
{
#ifdef _WIN32
	return _getpid();
#else
	return getpid();
#endif
}


// A new empty directory under the temporary directory, removed at the end.
class TemporaryDirectory {
public:
	TemporaryDirectory()
		:
		fPath(std::filesystem::temp_directory_path()
			/ ("nr" + std::to_string(ThisProcess()) + "-" + std::to_string(sCount++)))
	{
		std::error_code ignored;
		std::filesystem::remove_all(fPath, ignored);
		std::filesystem::create_directories(fPath);
#ifndef _WIN32
		std::filesystem::permissions(fPath, std::filesystem::perms::owner_all);
#endif
	}

	~TemporaryDirectory()
	{
		std::error_code ignored;
		std::filesystem::remove_all(fPath, ignored);
	}

	const std::filesystem::path& Path() const
	{
		return fPath;
	}

private:
	static inline int	sCount = 0;
	std::filesystem::path fPath;
};


// The window "Settings" with a check box "check".
BMessenger
OpenSettings()
{
	auto window = std::make_unique<BWindow>(gfx::Rect{0, 0, 200, 100}, "Settings",
		BWindowLook::Titled);
	window->AddChild(std::make_unique<BCheckBox>(gfx::Rect{10, 10, 190, 30}, "check",
		"Save", nullptr));
	window->Show();
	return *BLooper::Run(std::move(window));
}


TEST(RemoteControlTest, ItIsOffUntilAsked)
{
	// What is tested: An application made without NGUI_REMOTE_CONTROL and
	// without EnableRemoteControl(); then enabled and disabled again.
	// Success criteria: Without being asked it has no socket and none is
	// listed; enabled, its socket is listed with its signature and process;
	// disabled, the socket is gone and connecting to it is NotFound.
	// Why it matters: Rule T4: the transport is off unless the user or the
	// program turns it on, and leaves nothing behind when it is off again.
	//
	// Given
	TestEnvironment environment;
	const TemporaryDirectory directory;
	BApplication app(kSignature);

	// When
	const std::filesystem::path before = app.RemoteControlPath();
	const std::vector<remote::Endpoint> none = remote::ListApplications(directory.Path());
	const auto path = app.EnableRemoteControl(directory.Path());
	const std::vector<remote::Endpoint> listed = remote::ListApplications(directory.Path());
	app.DisableRemoteControl();
	const bool removed = path.has_value() && !std::filesystem::exists(*path);
	const auto connected = path.has_value() ? remote::Client::Connect(*path)
		: std::expected<remote::Client, Error>(std::unexpected(Error{ErrorCode::NotFound, ""}));

	// Then
	EXPECT_TRUE(before.empty());
	EXPECT_TRUE(none.empty());
	ASSERT_TRUE(path.has_value()) << path.error().message;
	EXPECT_EQ(*path, remote::EndpointPath(directory.Path(), kSignature, ThisProcess()));
	ASSERT_EQ(listed.size(), 1u);
	EXPECT_EQ(listed[0].signature, kSignature);
	EXPECT_EQ(listed[0].process, ThisProcess());
	EXPECT_TRUE(removed);
	EXPECT_TRUE(app.RemoteControlPath().empty());
	ASSERT_FALSE(connected.has_value());
	EXPECT_EQ(connected.error().code, ErrorCode::NotFound);
}


TEST(RemoteControlTest, AClientScriptsTheApplication)
{
	// What is tested: A client connected to the socket of a running
	// application with a window: get the title of Window 0, set and get the
	// value of the check box, count the windows, all on one connection; a
	// message that is no scripting message (kQuitRequested); a frame that
	// is no message.
	// Success criteria: "Settings"; the check box is 1 after setting it;
	// 1 window. kQuitRequested is refused (NotSupported) and the
	// application keeps running (the next request is answered); the bad
	// frame is answered BadData.
	// Why it matters: Remote control is scripting from another process
	// (specification 20.7); it must not be a way to send the application
	// anything else, nor crash on what arrives.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	const TemporaryDirectory directory;
	BApplication app(kSignature);
	Driver driver(app);
	const BMessenger window = OpenSettings();
	(void)driver.WaitForIdle();
	const auto path = app.EnableRemoteControl(directory.Path());
	ASSERT_TRUE(path.has_value()) << path.error().message;
	auto client = remote::Client::Connect(*path);
	ASSERT_TRUE(client.has_value()) << client.error().message;
	BMessage title(kGetProperty);
	(void)title.AddSpecifier("Title");
	(void)title.AddSpecifier("Window", 0);
	BMessage set(kSetProperty);
	(void)set.AddSpecifier("Value");
	(void)set.AddSpecifier("View", "check");
	(void)set.AddSpecifier("Window", "Settings");
	(void)set.AddInt32("data", 1);
	BMessage value(kGetProperty);
	(void)value.AddSpecifier("Value");
	(void)value.AddSpecifier("View", "check");
	(void)value.AddSpecifier("Window", "Settings");
	BMessage count(kCountProperties);
	(void)count.AddSpecifier("Window");

	// When
	const auto titleReply = client->Send(title);
	const auto setReply = client->Send(set);
	const auto valueReply = client->Send(value);
	const auto quitReply = client->Send(BMessage(kQuitRequested));
	const auto countReply = client->Send(count);
	auto raw = remote::detail::Socket::Connect(*path);
	ASSERT_TRUE(raw.has_value());
	const std::vector<std::byte> garbage = {std::byte{3}, std::byte{0}, std::byte{0},
		std::byte{0}, std::byte{'b'}, std::byte{'a'}, std::byte{'d'}};
	client = std::unexpected(Error{ErrorCode::NotFound, ""});
	const auto wrote = raw->WriteAll(garbage);
	const auto badReply = remote::detail::ReadFrame(*raw, 5s);
	app.DisableRemoteControl();
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(titleReply.has_value()) << titleReply.error().message;
	EXPECT_EQ(titleReply->GetString("result", ""), "Settings");
	ASSERT_TRUE(setReply.has_value());
	EXPECT_TRUE(ScriptingResult(*setReply).has_value());
	ASSERT_TRUE(valueReply.has_value());
	EXPECT_EQ(valueReply->GetInt32("result", -1), 1);
	ASSERT_TRUE(quitReply.has_value());
	EXPECT_EQ(ScriptingResult(*quitReply).error().code, ErrorCode::NotSupported);
	ASSERT_TRUE(countReply.has_value());
	EXPECT_EQ(countReply->GetInt32("result", 0), 1);
	EXPECT_TRUE(wrote.has_value());
	ASSERT_TRUE(badReply.has_value()) << badReply.error().message;
	EXPECT_EQ(ScriptingResult(*badReply).error().code, ErrorCode::BadData);
}


TEST(RemoteControlTest, TheEnvironmentTurnsItOn)
{
	// What is tested: An application started with NGUI_REMOTE_CONTROL=1 and
	// the user's runtime directory (XDG_RUNTIME_DIR, on Windows LOCALAPPDATA)
	// in a temporary directory; on POSIX the modes of directory and socket;
	// on POSIX a runtime directory whose "ngui" others may enter.
	// Success criteria: It listens in the default directory under the
	// runtime directory; the directory has mode 0700, the socket 0600. A
	// directory with mode 0755 is refused: no socket, and
	// DefaultDirectory() is InvalidArgument.
	// Why it matters: Rule T4: switched on explicitly at start, and the
	// socket only for the user the application runs as.
	//
	// Given
	TestEnvironment environment;
	const TemporaryDirectory runtime;
#ifdef _WIN32
	const ScopedEnvironment place("LOCALAPPDATA", runtime.Path().string().c_str());
#else
	const ScopedEnvironment place("XDG_RUNTIME_DIR", runtime.Path().string().c_str());
#endif
	const ScopedEnvironment on("NGUI_REMOTE_CONTROL", "1");

	// When
	std::filesystem::path path;
	{
		BApplication app(kSignature);
		path = app.RemoteControlPath();
	}
	const auto directory = remote::DefaultDirectory();

	// Then
	ASSERT_TRUE(directory.has_value()) << directory.error().message;
	EXPECT_EQ(path, remote::EndpointPath(*directory, kSignature, ThisProcess()));
	EXPECT_FALSE(std::filesystem::exists(path));
#ifndef _WIN32
	struct stat status{};
	ASSERT_EQ(stat(directory->c_str(), &status), 0);
	EXPECT_EQ(status.st_mode & 0777, 0700u);
	{
		BApplication app(kSignature);
		ASSERT_EQ(stat(app.RemoteControlPath().c_str(), &status), 0);
		EXPECT_EQ(status.st_mode & 0777, 0600u);
	}
	std::filesystem::permissions(*directory, std::filesystem::perms::owner_all
		| std::filesystem::perms::group_read | std::filesystem::perms::group_exec
		| std::filesystem::perms::others_read | std::filesystem::perms::others_exec);
	{
		BApplication app(kSignature);
		EXPECT_TRUE(app.RemoteControlPath().empty());
	}
	EXPECT_EQ(remote::DefaultDirectory().error().code, ErrorCode::InvalidArgument);
#endif
}


TEST(RemoteControlTest, FramesAreChecked)
{
	// What is tested: EncodeFrame() and DecodeFrame() of a scripting
	// message; frames whose length does not match, that are too large, too
	// short, or whose payload is no message.
	// Success criteria: The message comes back equal; every bad frame is
	// BadData.
	// Why it matters: The frames come from another process; a length must
	// not make the application read or allocate what the sender likes.
	//
	// Given
	BMessage message(kGetProperty);
	(void)message.AddSpecifier("Title");
	(void)message.AddSpecifier("Window", 0);
	const auto frame = remote::detail::EncodeFrame(message);
	ASSERT_TRUE(frame.has_value());
	std::vector<std::byte> truncated(frame->begin(), frame->end() - 1);
	std::vector<std::byte> huge = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
		std::byte{0xff}};
	const std::vector<std::byte> shortFrame = {std::byte{1}};
	std::vector<std::byte> noMessage = {std::byte{2}, std::byte{0}, std::byte{0},
		std::byte{0}, std::byte{'x'}, std::byte{'y'}};

	// When
	const auto decoded = remote::detail::DecodeFrame(*frame);

	// Then
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(*decoded, message);
	EXPECT_EQ(remote::detail::DecodeFrame(truncated).error().code, ErrorCode::BadData);
	EXPECT_EQ(remote::detail::DecodeFrame(huge).error().code, ErrorCode::BadData);
	EXPECT_EQ(remote::detail::DecodeFrame(shortFrame).error().code, ErrorCode::BadData);
	EXPECT_FALSE(remote::detail::DecodeFrame(noMessage).has_value());
}


} // namespace
