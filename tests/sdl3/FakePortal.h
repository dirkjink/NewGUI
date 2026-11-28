/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The desktop's portal on Linux, as far as NewGUI and SDL ask it (module
// G6): org.freedesktop.portal.Settings on org.freedesktop.portal.Desktop,
// Read() of org.freedesktop.appearance's color-scheme (1 dark, 2 light) and
// accent-color (three doubles), and SettingChanged when the user changes
// one. On a session bus of the test's own; answers on a thread of its own,
// since SDL and NewGUI ask while the application starts.


#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include <dbus/dbus.h>


namespace ngui::test {


class FakePortal {
public:
	using Rgb = std::array<double, 3>;

	FakePortal(std::uint32_t scheme, std::optional<Rgb> accent = std::nullopt)
		:
		fScheme(scheme),
		fAccent(accent)
	{
		DBusError error;
		dbus_error_init(&error);
		fConnection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
		dbus_error_free(&error);
		if (fConnection == nullptr)
			return;
		dbus_connection_set_exit_on_disconnect(fConnection, FALSE);
		fOwner = dbus_bus_request_name(fConnection, "org.freedesktop.portal.Desktop",
			DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
		dbus_error_free(&error);
		if (fOwner)
			fThread = std::thread([this] { _Serve(); });
	}

	~FakePortal()
	{
		fStop = true;
		if (fThread.joinable())
			fThread.join();
		if (fConnection != nullptr) {
			dbus_connection_close(fConnection);
			dbus_connection_unref(fConnection);
		}
	}

	FakePortal(const FakePortal&) = delete;
	FakePortal& operator=(const FakePortal&) = delete;

	// Whether the test's portal owns the name (no real one runs).
	bool IsOwner() const
	{
		return fOwner;
	}

	// The user switches the system to \a scheme: SettingChanged.
	void SwitchScheme(std::uint32_t scheme)
	{
		std::scoped_lock lock(fLock);
		fScheme = scheme;
		fPending.push_back(Change::Scheme);
	}

	// The user chooses \a accent, or none (-1 for each component, outside 0
	// to 1 as the portal says it): SettingChanged.
	void SetAccent(std::optional<Rgb> accent)
	{
		std::scoped_lock lock(fLock);
		fAccent = accent;
		fPending.push_back(Change::Accent);
	}

private:
	static constexpr const char* kSettings = "org.freedesktop.portal.Settings";

	enum class Change { Scheme, Accent };

	void _Serve()
	{
		while (!fStop) {
			(void)dbus_connection_read_write(fConnection, 20);
			while (DBusMessage* message = dbus_connection_pop_message(fConnection)) {
				if (dbus_message_is_method_call(message, kSettings, "Read"))
					_Answer(message);
				dbus_message_unref(message);
			}
			std::deque<Change> pending;
			{
				std::scoped_lock lock(fLock);
				pending.swap(fPending);
			}
			for (const Change change : pending)
				_Changed(change);
		}
	}

	// A value of \a change in \a iter: u for the scheme, (ddd) for the accent.
	void _AppendValue(DBusMessageIter* iter, Change change)
	{
		std::scoped_lock lock(fLock);
		DBusMessageIter value;
		if (change == Change::Scheme) {
			const dbus_uint32_t scheme = fScheme;
			(void)dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "u", &value);
			(void)dbus_message_iter_append_basic(&value, DBUS_TYPE_UINT32, &scheme);
		} else {
			const Rgb rgb = fAccent.value_or(Rgb{-1, -1, -1});
			DBusMessageIter fields;
			(void)dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "(ddd)", &value);
			(void)dbus_message_iter_open_container(&value, DBUS_TYPE_STRUCT, nullptr, &fields);
			for (const double component : rgb)
				(void)dbus_message_iter_append_basic(&fields, DBUS_TYPE_DOUBLE, &component);
			(void)dbus_message_iter_close_container(&value, &fields);
		}
		(void)dbus_message_iter_close_container(iter, &value);
	}

	// Read(namespace, key) answers the value in a variant in a variant; an
	// accent color nobody chose is not found, as a desktop without one says.
	void _Answer(DBusMessage* call)
	{
		const char* name = nullptr;
		const char* key = nullptr;
		DBusError error;
		dbus_error_init(&error);
		const bool read = dbus_message_get_args(call, &error, DBUS_TYPE_STRING, &name,
			DBUS_TYPE_STRING, &key, DBUS_TYPE_INVALID);
		dbus_error_free(&error);
		const bool scheme = read && std::strcmp(key, "color-scheme") == 0;
		const bool accent = read && std::strcmp(key, "accent-color") == 0;
		bool chosen = false;
		{
			std::scoped_lock lock(fLock);
			chosen = fAccent.has_value();
		}
		DBusMessage* reply = nullptr;
		if (read && std::strcmp(name, "org.freedesktop.appearance") == 0
				&& (scheme || (accent && chosen))) {
			reply = dbus_message_new_method_return(call);
			DBusMessageIter args;
			DBusMessageIter outer;
			dbus_message_iter_init_append(reply, &args);
			(void)dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "v", &outer);
			_AppendValue(&outer, scheme ? Change::Scheme : Change::Accent);
			(void)dbus_message_iter_close_container(&args, &outer);
		} else {
			reply = dbus_message_new_error(call, "org.freedesktop.portal.Error.NotFound",
				"Requested setting not found");
		}
		(void)dbus_connection_send(fConnection, reply, nullptr);
		dbus_message_unref(reply);
		dbus_connection_flush(fConnection);
	}

	void _Changed(Change change)
	{
		DBusMessage* signal = dbus_message_new_signal("/org/freedesktop/portal/desktop",
			kSettings, "SettingChanged");
		DBusMessageIter args;
		const char* name = "org.freedesktop.appearance";
		const char* key = change == Change::Scheme ? "color-scheme" : "accent-color";
		dbus_message_iter_init_append(signal, &args);
		(void)dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &name);
		(void)dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &key);
		_AppendValue(&args, change);
		(void)dbus_connection_send(fConnection, signal, nullptr);
		dbus_message_unref(signal);
		dbus_connection_flush(fConnection);
	}

	DBusConnection* fConnection = nullptr;
	bool fOwner = false;
	std::mutex fLock;
	std::uint32_t fScheme;
	std::optional<Rgb> fAccent;
	std::deque<Change> fPending;
	std::atomic<bool> fStop = false;
	std::thread fThread;
};


} // namespace ngui::test
