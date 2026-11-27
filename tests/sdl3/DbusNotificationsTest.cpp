/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The desktop's notifications on Linux (module P5): the test is the
// desktop's notification server, org.freedesktop.Notifications on a session
// bus of its own (dbus-run-session in the CI). Without one, or when a real
// server owns the name, the test is skipped.


#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <dbus/dbus.h>
#include <gtest/gtest.h>

#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/platform/Platform.h>

#include "sdl3/RunningApp.h"
#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr std::uint32_t kOpened = 0x6f70656e;	// 'open'


// One Notify call as the server got it.
struct Call {
	std::string					application;
	std::uint32_t				replaces = 0;
	std::string					summary;
	std::string					body;
	std::vector<std::string>	actions;
	int							urgency = -1;
	int							value = -1;
	std::int32_t				imageWidth = 0;
	std::int32_t				imageHeight = 0;
	std::int32_t				imageStride = 0;
	std::vector<std::uint8_t>	imageBytes;
	std::int32_t				timeout = 0;
};


std::string
StringOf(DBusMessageIter* iter)
{
	const char* value = nullptr;
	dbus_message_iter_get_basic(iter, &value);
	return value != nullptr ? value : "";
}


// The notification server: owns org.freedesktop.Notifications, answers
// Notify with ids from 1 and keeps the calls.
class Server {
public:
	Server()
	{
		DBusError error;
		dbus_error_init(&error);
		fConnection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
		dbus_error_free(&error);
		if (fConnection == nullptr)
			return;
		dbus_connection_set_exit_on_disconnect(fConnection, FALSE);
		fOwner = dbus_bus_request_name(fConnection, "org.freedesktop.Notifications",
			DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
		dbus_error_free(&error);
	}

	~Server()
	{
		if (fConnection != nullptr) {
			dbus_connection_close(fConnection);
			dbus_connection_unref(fConnection);
		}
	}

	bool IsOwner() const
	{
		return fOwner;
	}

	// Answers the calls that came, for up to \a time, until there are
	// \a count of them.
	void Serve(std::size_t count, std::chrono::milliseconds time)
	{
		const auto end = std::chrono::steady_clock::now() + time;
		while (calls.size() < count && std::chrono::steady_clock::now() < end) {
			(void)dbus_connection_read_write(fConnection, 20);
			while (DBusMessage* message = dbus_connection_pop_message(fConnection)) {
				if (dbus_message_is_method_call(message, "org.freedesktop.Notifications",
						"Notify")) {
					calls.push_back(Parse(message));
					const dbus_uint32_t id = calls.back().replaces != 0 ? calls.back().replaces
						: ++fLastId;
					DBusMessage* reply = dbus_message_new_method_return(message);
					(void)dbus_message_append_args(reply, DBUS_TYPE_UINT32, &id,
						DBUS_TYPE_INVALID);
					(void)dbus_connection_send(fConnection, reply, nullptr);
					dbus_message_unref(reply);
					dbus_connection_flush(fConnection);
				}
				dbus_message_unref(message);
			}
		}
	}

	// The user clicks the notification \a id.
	void Click(dbus_uint32_t id)
	{
		DBusMessage* signal = dbus_message_new_signal("/org/freedesktop/Notifications",
			"org.freedesktop.Notifications", "ActionInvoked");
		const char* action = "default";
		(void)dbus_message_append_args(signal, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &action,
			DBUS_TYPE_INVALID);
		(void)dbus_connection_send(fConnection, signal, nullptr);
		dbus_message_unref(signal);
		dbus_connection_flush(fConnection);
	}

	std::vector<Call> calls;

private:
	static void ParseHint(DBusMessageIter* entry, Call& call)
	{
		const std::string name = StringOf(entry);
		(void)dbus_message_iter_next(entry);
		DBusMessageIter variant;
		dbus_message_iter_recurse(entry, &variant);
		if (name == "urgency" && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BYTE) {
			unsigned char urgency = 0;
			dbus_message_iter_get_basic(&variant, &urgency);
			call.urgency = urgency;
		} else if (name == "value"
			&& dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_INT32) {
			dbus_int32_t value = 0;
			dbus_message_iter_get_basic(&variant, &value);
			call.value = value;
		} else if (name == "image-data"
			&& dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRUCT) {
			DBusMessageIter image;
			dbus_message_iter_recurse(&variant, &image);
			dbus_int32_t ints[3] = {};
			for (dbus_int32_t& value : ints) {
				dbus_message_iter_get_basic(&image, &value);
				(void)dbus_message_iter_next(&image);
			}
			call.imageWidth = ints[0];
			call.imageHeight = ints[1];
			call.imageStride = ints[2];
			// has_alpha, bits_per_sample, channels
			for (int skip = 0; skip < 3; skip++)
				(void)dbus_message_iter_next(&image);
			DBusMessageIter bytes;
			dbus_message_iter_recurse(&image, &bytes);
			const unsigned char* data = nullptr;
			int length = 0;
			dbus_message_iter_get_fixed_array(&bytes, &data, &length);
			call.imageBytes.assign(data, data + length);
		}
	}

	static Call Parse(DBusMessage* message)
	{
		Call call;
		DBusMessageIter args;
		(void)dbus_message_iter_init(message, &args);
		call.application = StringOf(&args);
		(void)dbus_message_iter_next(&args);
		dbus_message_iter_get_basic(&args, &call.replaces);
		(void)dbus_message_iter_next(&args);
		(void)dbus_message_iter_next(&args);	// app_icon
		call.summary = StringOf(&args);
		(void)dbus_message_iter_next(&args);
		call.body = StringOf(&args);
		(void)dbus_message_iter_next(&args);
		DBusMessageIter actions;
		dbus_message_iter_recurse(&args, &actions);
		while (dbus_message_iter_get_arg_type(&actions) == DBUS_TYPE_STRING) {
			call.actions.push_back(StringOf(&actions));
			(void)dbus_message_iter_next(&actions);
		}
		(void)dbus_message_iter_next(&args);
		DBusMessageIter hints;
		dbus_message_iter_recurse(&args, &hints);
		while (dbus_message_iter_get_arg_type(&hints) == DBUS_TYPE_DICT_ENTRY) {
			DBusMessageIter entry;
			dbus_message_iter_recurse(&hints, &entry);
			ParseHint(&entry, call);
			(void)dbus_message_iter_next(&hints);
		}
		(void)dbus_message_iter_next(&args);
		dbus_message_iter_get_basic(&args, &call.timeout);
		return call;
	}

	DBusConnection* fConnection = nullptr;
	bool fOwner = false;
	dbus_uint32_t fLastId = 0;
};


TEST(DbusNotificationsTest, NotificationsGoToTheDesktop)
{
	// What is tested: The SDL platform under Linux and the test as the
	// desktop's notification server on a session bus. Three notifications
	// are shown (Platform::ShowNotification()): an Important one from
	// "example-mail" in the group "Mail" with a click message to a looper of
	// the test and a 1 × 1 icon, 5 s; a Progress one with the id "fetch" at
	// 0.25, then again at 0.75. The server answers with ids 1 and 2; then
	// the user clicks notification 1.
	// Success criteria: The server gets three Notify calls: the first with
	// the app name "Mail", the texts, the actions "default" "Open",
	// urgency 2, the icon as image-data (1 × 1, stride 4, the RGBA bytes)
	// and 5000 ms; the second with urgency 1, value 25, no actions and
	// timeout -1; the third replaces id 2 with value 75. After the click the
	// looper gets kOpened. Skipped without a session bus or when another
	// server owns the name.
	// Why it matters: On Linux the desktop shows notifications only through
	// this specification (module P5); a progress must replace itself, and a
	// click must reach the application.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr)
		GTEST_SKIP() << "no session bus";
	Server server;
	if (!server.IsOwner())
		GTEST_SKIP() << "another notification server runs";
	RunningApp app;
	ASSERT_NE(app.Platform(), nullptr);
	auto record = std::make_shared<LooperRecord>();
	const auto started = BLooper::Run(std::make_unique<RecordingLooper>(record));
	ASSERT_TRUE(started.has_value());
	const BMessenger looper = *started;
	NotificationSpec mail;
	mail.kind = NotificationSpec::Kind::Important;
	mail.application = "example-mail";
	mail.group = "Mail";
	mail.title = "New mail";
	mail.content = "From Ada";
	mail.iconWidth = 1;
	mail.iconHeight = 1;
	mail.icon = {255, 0, 0, 128};
	mail.timeout = 5000ms;
	mail.target = looper;
	mail.onClick = BMessage(kOpened);
	NotificationSpec progress;
	progress.kind = NotificationSpec::Kind::Progress;
	progress.application = "example-mail";
	progress.title = "Fetching";
	progress.id = "fetch";
	progress.progress = 0.25f;

	// When
	ASSERT_TRUE(app.Platform()->ShowNotification(mail).has_value());
	ASSERT_TRUE(app.Platform()->ShowNotification(progress).has_value());
	server.Serve(2, 5s);
	progress.progress = 0.75f;
	ASSERT_TRUE(app.Platform()->ShowNotification(progress).has_value());
	server.Serve(3, 5s);
	server.Click(1);
	const bool clicked = record->WaitFor(1);

	// Then
	ASSERT_EQ(server.calls.size(), 3u);
	const Call& first = server.calls[0];
	EXPECT_EQ(first.application, "Mail");
	EXPECT_EQ(first.replaces, 0u);
	EXPECT_EQ(first.summary, "New mail");
	EXPECT_EQ(first.body, "From Ada");
	EXPECT_EQ(first.actions, (std::vector<std::string>{"default", "Open"}));
	EXPECT_EQ(first.urgency, 2);
	EXPECT_EQ(first.imageWidth, 1);
	EXPECT_EQ(first.imageHeight, 1);
	EXPECT_EQ(first.imageStride, 4);
	EXPECT_EQ(first.imageBytes, (std::vector<std::uint8_t>{255, 0, 0, 128}));
	EXPECT_EQ(first.timeout, 5000);
	const Call& second = server.calls[1];
	EXPECT_EQ(second.application, "example-mail");
	EXPECT_EQ(second.urgency, 1);
	EXPECT_EQ(second.value, 25);
	EXPECT_TRUE(second.actions.empty());
	EXPECT_EQ(second.timeout, -1);
	const Call& third = server.calls[2];
	EXPECT_EQ(third.replaces, 2u);
	EXPECT_EQ(third.value, 75);
	ASSERT_TRUE(clicked);
	std::scoped_lock guard(record->lock);
	ASSERT_FALSE(record->received.empty());
	EXPECT_EQ(record->received.front(), "looper:" + std::to_string(kOpened));
	(void)looper.SendMessage(kQuitRequested);
}


} // namespace
