/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/DbusNotifications.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include <dlfcn.h>

#include <dbus/dbus.h>

#include "Environment.h"


namespace ngui::platform::sdl3 {


namespace {


constexpr const char* kService = "org.freedesktop.Notifications";
constexpr const char* kPath = "/org/freedesktop/Notifications";
constexpr const char* kInterface = "org.freedesktop.Notifications";


// The functions of libdbus the notifications need, from the system's
// library (libdbus-1.so.3), which SDL loads too.
struct Dbus {
	void*			library = nullptr;
	void			(*ErrorInit)(DBusError*) = nullptr;
	void			(*ErrorFree)(DBusError*) = nullptr;
	DBusConnection*	(*BusGetPrivate)(DBusBusType, DBusError*) = nullptr;
	void			(*BusAddMatch)(DBusConnection*, const char*, DBusError*) = nullptr;
	void			(*ConnectionClose)(DBusConnection*) = nullptr;
	void			(*ConnectionUnref)(DBusConnection*) = nullptr;
	void			(*SetExitOnDisconnect)(DBusConnection*, dbus_bool_t) = nullptr;
	dbus_bool_t		(*ReadWrite)(DBusConnection*, int) = nullptr;
	DBusMessage*	(*PopMessage)(DBusConnection*) = nullptr;
	DBusMessage*	(*SendWithReplyAndBlock)(DBusConnection*, DBusMessage*, int, DBusError*)
						= nullptr;
	DBusMessage*	(*NewMethodCall)(const char*, const char*, const char*, const char*)
						= nullptr;
	void			(*MessageUnref)(DBusMessage*) = nullptr;
	dbus_bool_t		(*IsSignal)(DBusMessage*, const char*, const char*) = nullptr;
	dbus_bool_t		(*GetArgs)(DBusMessage*, DBusError*, int, ...) = nullptr;
	void			(*IterInitAppend)(DBusMessage*, DBusMessageIter*) = nullptr;
	dbus_bool_t		(*AppendBasic)(DBusMessageIter*, int, const void*) = nullptr;
	dbus_bool_t		(*AppendFixedArray)(DBusMessageIter*, int, const void*, int) = nullptr;
	dbus_bool_t		(*OpenContainer)(DBusMessageIter*, int, const char*, DBusMessageIter*)
						= nullptr;
	dbus_bool_t		(*CloseContainer)(DBusMessageIter*, DBusMessageIter*) = nullptr;

	bool Load()
	{
		library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
		if (library == nullptr)
			return false;
		bool complete = true;
		const auto bind = [&](auto& function, const char* name) {
			// POSIX: a function's address passes through dlsym()'s void*.
			void* symbol = dlsym(library, name);
			complete = complete && symbol != nullptr;
			function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol);
		};
		bind(ErrorInit, "dbus_error_init");
		bind(ErrorFree, "dbus_error_free");
		bind(BusGetPrivate, "dbus_bus_get_private");
		bind(BusAddMatch, "dbus_bus_add_match");
		bind(ConnectionClose, "dbus_connection_close");
		bind(ConnectionUnref, "dbus_connection_unref");
		bind(SetExitOnDisconnect, "dbus_connection_set_exit_on_disconnect");
		bind(ReadWrite, "dbus_connection_read_write");
		bind(PopMessage, "dbus_connection_pop_message");
		bind(SendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block");
		bind(NewMethodCall, "dbus_message_new_method_call");
		bind(MessageUnref, "dbus_message_unref");
		bind(IsSignal, "dbus_message_is_signal");
		bind(GetArgs, "dbus_message_get_args");
		bind(IterInitAppend, "dbus_message_iter_init_append");
		bind(AppendBasic, "dbus_message_iter_append_basic");
		bind(AppendFixedArray, "dbus_message_iter_append_fixed_array");
		bind(OpenContainer, "dbus_message_iter_open_container");
		bind(CloseContainer, "dbus_message_iter_close_container");
		return complete;
	}
};


const Dbus*
Library()
{
	static const Dbus* const library = []() -> const Dbus* {
		static Dbus functions;
		return functions.Load() ? &functions : nullptr;
	}();
	return library;
}


// Whether there is a session bus to connect to without starting one
// (libdbus would otherwise try to launch a bus through X11).
bool
HasSessionBus()
{
	if (detail::EnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").has_value())
		return true;
	const std::optional<std::string> runtime = detail::EnvironmentVariable("XDG_RUNTIME_DIR");
	std::error_code error;
	return runtime.has_value() && std::filesystem::exists(*runtime + "/bus", error);
}


// The urgency of the specification: 0 low, 1 normal, 2 critical.
std::uint8_t
UrgencyOf(NotificationSpec::Kind kind)
{
	switch (kind) {
		case NotificationSpec::Kind::Important:
		case NotificationSpec::Kind::Error:
			return 2;
		case NotificationSpec::Kind::Information:
		case NotificationSpec::Kind::Progress:
			break;
	}
	return 1;
}


} // namespace


struct DbusNotifications::Impl {
	const Dbus&				dbus;
	DBusConnection*			connection;
	std::thread				thread;
	std::mutex				lock;
	std::deque<NotificationSpec> queue;
	std::atomic<bool>		stop = false;
	// on the thread only: the server's ids of the notifications by their
	// ids, and what a click on one sends where
	std::map<std::string, dbus_uint32_t> ids;
	std::map<dbus_uint32_t, std::pair<BMessenger, BMessage>> clicks;

	Impl(const Dbus& functions, DBusConnection* bus)
		:
		dbus(functions),
		connection(bus)
	{
	}

	void Run()
	{
		while (!stop) {
			std::deque<NotificationSpec> sending;
			{
				std::scoped_lock guard(lock);
				sending.swap(queue);
			}
			for (const NotificationSpec& spec : sending)
				Notify(spec);
			// waits a little for the server's signals (and Show() meanwhile)
			if (!dbus.ReadWrite(connection, 50))
				break;
			while (DBusMessage* message = dbus.PopMessage(connection)) {
				Received(message);
				dbus.MessageUnref(message);
			}
		}
	}

	void AppendString(DBusMessageIter* iter, const std::string& text)
	{
		const char* value = text.c_str();
		(void)dbus.AppendBasic(iter, DBUS_TYPE_STRING, &value);
	}

	// One entry of the hints, a{sv}, with a value of \a signature.
	template<typename Fill>
	void AppendHint(DBusMessageIter* hints, const char* name, const char* signature, Fill fill)
	{
		DBusMessageIter entry;
		DBusMessageIter variant;
		(void)dbus.OpenContainer(hints, DBUS_TYPE_DICT_ENTRY, nullptr, &entry);
		(void)dbus.AppendBasic(&entry, DBUS_TYPE_STRING, &name);
		(void)dbus.OpenContainer(&entry, DBUS_TYPE_VARIANT, signature, &variant);
		fill(&variant);
		(void)dbus.CloseContainer(&entry, &variant);
		(void)dbus.CloseContainer(hints, &entry);
	}

	void Notify(const NotificationSpec& spec)
	{
		DBusMessage* call = dbus.NewMethodCall(kService, kPath, kInterface, "Notify");
		if (call == nullptr)
			return;
		DBusMessageIter args;
		dbus.IterInitAppend(call, &args);
		// app_name, replaces_id, app_icon, summary, body
		AppendString(&args, spec.group.empty() ? spec.application : spec.group);
		dbus_uint32_t replaces = 0;
		if (const auto found = ids.find(spec.id); !spec.id.empty() && found != ids.end())
			replaces = found->second;
		(void)dbus.AppendBasic(&args, DBUS_TYPE_UINT32, &replaces);
		AppendString(&args, "");
		AppendString(&args, spec.title);
		AppendString(&args, spec.content);
		// actions: the default one (a click on it) with a click message
		DBusMessageIter actions;
		(void)dbus.OpenContainer(&args, DBUS_TYPE_ARRAY, "s", &actions);
		if (spec.onClick.has_value()) {
			AppendString(&actions, "default");
			AppendString(&actions, "Open");
		}
		(void)dbus.CloseContainer(&args, &actions);
		// hints
		DBusMessageIter hints;
		(void)dbus.OpenContainer(&args, DBUS_TYPE_ARRAY, "{sv}", &hints);
		AppendHint(&hints, "urgency", "y", [&](DBusMessageIter* value) {
			const unsigned char urgency = UrgencyOf(spec.kind);
			(void)dbus.AppendBasic(value, DBUS_TYPE_BYTE, &urgency);
		});
		if (spec.kind == NotificationSpec::Kind::Progress) {
			AppendHint(&hints, "value", "i", [&](DBusMessageIter* value) {
				const auto percent = static_cast<dbus_int32_t>(std::lround(spec.progress * 100));
				(void)dbus.AppendBasic(value, DBUS_TYPE_INT32, &percent);
			});
		}
		const auto pixels = static_cast<std::size_t>(spec.iconWidth)
			* static_cast<std::size_t>(spec.iconHeight);
		if (spec.iconWidth > 0 && spec.iconHeight > 0 && spec.icon.size() == pixels * 4) {
			// width, height, rowstride, has_alpha, bits_per_sample, channels,
			// data: RGBA, not premultiplied
			AppendHint(&hints, "image-data", "(iiibiiay)", [&](DBusMessageIter* value) {
				DBusMessageIter image;
				(void)dbus.OpenContainer(value, DBUS_TYPE_STRUCT, nullptr, &image);
				const dbus_int32_t width = spec.iconWidth;
				const dbus_int32_t height = spec.iconHeight;
				const dbus_int32_t stride = spec.iconWidth * 4;
				const dbus_bool_t alpha = TRUE;
				const dbus_int32_t bits = 8;
				const dbus_int32_t channels = 4;
				(void)dbus.AppendBasic(&image, DBUS_TYPE_INT32, &width);
				(void)dbus.AppendBasic(&image, DBUS_TYPE_INT32, &height);
				(void)dbus.AppendBasic(&image, DBUS_TYPE_INT32, &stride);
				(void)dbus.AppendBasic(&image, DBUS_TYPE_BOOLEAN, &alpha);
				(void)dbus.AppendBasic(&image, DBUS_TYPE_INT32, &bits);
				(void)dbus.AppendBasic(&image, DBUS_TYPE_INT32, &channels);
				DBusMessageIter bytes;
				(void)dbus.OpenContainer(&image, DBUS_TYPE_ARRAY, "y", &bytes);
				const unsigned char* data = spec.icon.data();
				(void)dbus.AppendFixedArray(&bytes, DBUS_TYPE_BYTE, &data,
					static_cast<int>(spec.icon.size()));
				(void)dbus.CloseContainer(&image, &bytes);
				(void)dbus.CloseContainer(value, &image);
			});
		}
		(void)dbus.CloseContainer(&args, &hints);
		// expire_timeout: -1 for the server's choice
		const auto timeout = static_cast<dbus_int32_t>(spec.timeout.count() < 0 ? -1
			: std::min<std::int64_t>(spec.timeout.count(), INT32_MAX));
		(void)dbus.AppendBasic(&args, DBUS_TYPE_INT32, &timeout);

		DBusError error;
		dbus.ErrorInit(&error);
		DBusMessage* reply = dbus.SendWithReplyAndBlock(connection, call, 5000, &error);
		dbus.MessageUnref(call);
		dbus_uint32_t id = 0;
		if (reply != nullptr) {
			(void)dbus.GetArgs(reply, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_INVALID);
			dbus.MessageUnref(reply);
		}
		dbus.ErrorFree(&error);
		if (id == 0)
			return;
		if (!spec.id.empty())
			ids[spec.id] = id;
		if (spec.onClick.has_value())
			clicks.insert_or_assign(id, std::make_pair(spec.target, *spec.onClick));
		else
			clicks.erase(id);
	}

	void Received(DBusMessage* message)
	{
		DBusError error;
		dbus.ErrorInit(&error);
		dbus_uint32_t id = 0;
		if (dbus.IsSignal(message, kInterface, "ActionInvoked")) {
			const char* action = nullptr;
			if (dbus.GetArgs(message, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_STRING, &action,
					DBUS_TYPE_INVALID)
				&& action != nullptr && std::string_view(action) == "default") {
				if (const auto found = clicks.find(id); found != clicks.end())
					(void)found->second.first.SendMessage(found->second.second);
			}
		} else if (dbus.IsSignal(message, kInterface, "NotificationClosed")) {
			dbus_uint32_t reason = 0;
			if (dbus.GetArgs(message, &error, DBUS_TYPE_UINT32, &id, DBUS_TYPE_UINT32, &reason,
					DBUS_TYPE_INVALID)) {
				clicks.erase(id);
				std::erase_if(ids, [id](const auto& entry) { return entry.second == id; });
			}
		}
		dbus.ErrorFree(&error);
	}
};


DbusNotifications::DbusNotifications(std::unique_ptr<Impl> impl)
	:
	fImpl(std::move(impl))
{
}


std::expected<std::unique_ptr<DbusNotifications>, Error>
DbusNotifications::Create()
{
	const Dbus* dbus = Library();
	if (dbus == nullptr)
		return Failure(ErrorCode::NotSupported, "no libdbus");
	if (!HasSessionBus())
		return Failure(ErrorCode::NotSupported, "no session bus");
	DBusError error;
	dbus->ErrorInit(&error);
	DBusConnection* connection = dbus->BusGetPrivate(DBUS_BUS_SESSION, &error);
	if (connection == nullptr) {
		const std::string message = error.message != nullptr ? error.message : "no session bus";
		dbus->ErrorFree(&error);
		return Failure(ErrorCode::NotSupported, message);
	}
	dbus->SetExitOnDisconnect(connection, FALSE);
	// the server's signals of clicks and closing
	dbus->BusAddMatch(connection,
		"type='signal',interface='org.freedesktop.Notifications'", &error);
	dbus->ErrorFree(&error);
	auto impl = std::make_unique<Impl>(*dbus, connection);
	Impl* running = impl.get();
	impl->thread = std::thread([running] { running->Run(); });
	return std::unique_ptr<DbusNotifications>(new DbusNotifications(std::move(impl)));
}


DbusNotifications::~DbusNotifications()
{
	fImpl->stop = true;
	fImpl->thread.join();
	fImpl->dbus.ConnectionClose(fImpl->connection);
	fImpl->dbus.ConnectionUnref(fImpl->connection);
}


void
DbusNotifications::Show(const NotificationSpec& spec)
{
	std::scoped_lock guard(fImpl->lock);
	fImpl->queue.push_back(spec);
}


} // namespace ngui::platform::sdl3
