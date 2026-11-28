/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/Dbus.h"

#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>

#include <dlfcn.h>

#include "Environment.h"


namespace ngui::platform::sdl3::dbus {


namespace {


bool
Bind(Library& dbus)
{
	dbus.library = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
	if (dbus.library == nullptr)
		return false;
	bool complete = true;
	const auto bind = [&](auto& function, const char* name) {
		// POSIX: a function's address passes through dlsym()'s void*.
		void* symbol = dlsym(dbus.library, name);
		complete = complete && symbol != nullptr;
		function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(symbol);
	};
	bind(dbus.ErrorInit, "dbus_error_init");
	bind(dbus.ErrorFree, "dbus_error_free");
	bind(dbus.BusGetPrivate, "dbus_bus_get_private");
	bind(dbus.BusAddMatch, "dbus_bus_add_match");
	bind(dbus.ConnectionClose, "dbus_connection_close");
	bind(dbus.ConnectionUnref, "dbus_connection_unref");
	bind(dbus.SetExitOnDisconnect, "dbus_connection_set_exit_on_disconnect");
	bind(dbus.ReadWrite, "dbus_connection_read_write");
	bind(dbus.PopMessage, "dbus_connection_pop_message");
	bind(dbus.SendWithReplyAndBlock, "dbus_connection_send_with_reply_and_block");
	bind(dbus.NewMethodCall, "dbus_message_new_method_call");
	bind(dbus.MessageUnref, "dbus_message_unref");
	bind(dbus.IsSignal, "dbus_message_is_signal");
	bind(dbus.GetArgs, "dbus_message_get_args");
	bind(dbus.IterInitAppend, "dbus_message_iter_init_append");
	bind(dbus.AppendBasic, "dbus_message_iter_append_basic");
	bind(dbus.AppendFixedArray, "dbus_message_iter_append_fixed_array");
	bind(dbus.OpenContainer, "dbus_message_iter_open_container");
	bind(dbus.CloseContainer, "dbus_message_iter_close_container");
	bind(dbus.IterInit, "dbus_message_iter_init");
	bind(dbus.IterArgType, "dbus_message_iter_get_arg_type");
	bind(dbus.IterRecurse, "dbus_message_iter_recurse");
	bind(dbus.IterGetBasic, "dbus_message_iter_get_basic");
	bind(dbus.IterNext, "dbus_message_iter_next");
	return complete;
}


bool
HasSessionBus()
{
	if (detail::EnvironmentVariable("DBUS_SESSION_BUS_ADDRESS").has_value())
		return true;
	const std::optional<std::string> runtime = detail::EnvironmentVariable("XDG_RUNTIME_DIR");
	std::error_code error;
	return runtime.has_value() && std::filesystem::exists(*runtime + "/bus", error);
}


} // namespace


const Library*
Load()
{
	static const Library* const library = []() -> const Library* {
		static Library functions;
		return Bind(functions) ? &functions : nullptr;
	}();
	return library;
}


std::expected<DBusConnection*, Error>
ConnectSession(const Library& dbus)
{
	if (!HasSessionBus())
		return Failure(ErrorCode::NotSupported, "no session bus");
	DBusError error;
	dbus.ErrorInit(&error);
	DBusConnection* connection = dbus.BusGetPrivate(DBUS_BUS_SESSION, &error);
	if (connection == nullptr) {
		const std::string message = error.message != nullptr ? error.message : "no session bus";
		dbus.ErrorFree(&error);
		return Failure(ErrorCode::NotSupported, message);
	}
	dbus.ErrorFree(&error);
	dbus.SetExitOnDisconnect(connection, FALSE);
	return connection;
}


} // namespace ngui::platform::sdl3::dbus
