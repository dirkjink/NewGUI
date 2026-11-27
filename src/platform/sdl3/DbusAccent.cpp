/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The accent color on Linux: the desktop's portal says it as three doubles
// in sRGB, each within 0 and 1, any outside meaning none (XDG Desktop Portal,
// org.freedesktop.portal.Settings, org.freedesktop.appearance accent-color),
// and signals its change (SettingChanged). A connection of its own to the
// session bus, with a thread that hears the signal.


#include "platform/sdl3/SystemAccent.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

#include "platform/sdl3/Dbus.h"


namespace ngui::platform::sdl3 {


namespace {


constexpr const char* kPortal = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettings = "org.freedesktop.portal.Settings";
constexpr const char* kNamespace = "org.freedesktop.appearance";
constexpr const char* kKey = "accent-color";
// how long the portal may take to answer; it may be started for the call
constexpr int kReadTimeoutMs = 1000;


// The color of a value (ddd), inside as many variants as it comes in (Read()
// wraps it twice); std::nullopt for another value or a component outside 0
// to 1.
std::optional<gfx::Color>
ColorOf(const dbus::Library& dbus, DBusMessageIter* value)
{
	DBusMessageIter iter = *value;
	while (dbus.IterArgType(&iter) == DBUS_TYPE_VARIANT) {
		DBusMessageIter inner;
		dbus.IterRecurse(&iter, &inner);
		iter = inner;
	}
	if (dbus.IterArgType(&iter) != DBUS_TYPE_STRUCT)
		return std::nullopt;
	DBusMessageIter fields;
	dbus.IterRecurse(&iter, &fields);
	double rgb[3] = {};
	for (int i = 0; i < 3; i++) {
		if (dbus.IterArgType(&fields) != DBUS_TYPE_DOUBLE)
			return std::nullopt;
		dbus.IterGetBasic(&fields, &rgb[i]);
		if (!(rgb[i] >= 0.0 && rgb[i] <= 1.0))
			return std::nullopt;
		(void)dbus.IterNext(&fields);
	}
	const auto byte = [](double component) {
		return static_cast<std::uint8_t>(std::lround(component * 255.0));
	};
	return gfx::Color::FromRgba8(byte(rgb[0]), byte(rgb[1]), byte(rgb[2]), 255);
}


class DbusAccent final : public SystemAccent {
public:
	DbusAccent(const dbus::Library& dbus, DBusConnection* connection, Changed changed)
		:
		fDbus(dbus),
		fConnection(connection),
		fChanged(std::move(changed))
	{
		fColor = _Read();
		fThread = std::thread([this] { _Run(); });
	}

	~DbusAccent() override
	{
		fStop = true;
		fThread.join();
		fDbus.ConnectionClose(fConnection);
		fDbus.ConnectionUnref(fConnection);
	}

	std::optional<gfx::Color> Color() const override
	{
		std::scoped_lock lock(fLock);
		return fColor;
	}

private:
	// Read(namespace, key) of the portal, answered while starting.
	std::optional<gfx::Color> _Read()
	{
		DBusMessage* call = fDbus.NewMethodCall(kPortal, kPortalPath, kSettings, "Read");
		if (call == nullptr)
			return std::nullopt;
		DBusMessageIter args;
		fDbus.IterInitAppend(call, &args);
		(void)fDbus.AppendBasic(&args, DBUS_TYPE_STRING, &kNamespace);
		(void)fDbus.AppendBasic(&args, DBUS_TYPE_STRING, &kKey);
		DBusError error;
		fDbus.ErrorInit(&error);
		DBusMessage* reply
			= fDbus.SendWithReplyAndBlock(fConnection, call, kReadTimeoutMs, &error);
		fDbus.MessageUnref(call);
		fDbus.ErrorFree(&error);
		if (reply == nullptr)
			return std::nullopt;
		std::optional<gfx::Color> color;
		DBusMessageIter value;
		if (fDbus.IterInit(reply, &value))
			color = ColorOf(fDbus, &value);
		fDbus.MessageUnref(reply);
		return color;
	}

	void _Run()
	{
		while (!fStop) {
			if (!fDbus.ReadWrite(fConnection, 50))
				break;
			while (DBusMessage* message = fDbus.PopMessage(fConnection)) {
				_Received(message);
				fDbus.MessageUnref(message);
			}
		}
	}

	// SettingChanged(namespace, key, value) of the accent color.
	void _Received(DBusMessage* message)
	{
		if (!fDbus.IsSignal(message, kSettings, "SettingChanged"))
			return;
		DBusMessageIter args;
		if (!fDbus.IterInit(message, &args))
			return;
		const char* strings[2] = {};
		for (const char*& text : strings) {
			if (fDbus.IterArgType(&args) != DBUS_TYPE_STRING)
				return;
			fDbus.IterGetBasic(&args, &text);
			(void)fDbus.IterNext(&args);
		}
		if (std::strcmp(strings[0], kNamespace) != 0 || std::strcmp(strings[1], kKey) != 0)
			return;
		const std::optional<gfx::Color> color = ColorOf(fDbus, &args);
		{
			std::scoped_lock lock(fLock);
			if (fColor == color)
				return;
			fColor = color;
		}
		fChanged();
	}

	const dbus::Library&	fDbus;
	DBusConnection*			fConnection;
	Changed					fChanged;
	mutable std::mutex		fLock;
	std::optional<gfx::Color> fColor;
	std::atomic<bool>		fStop = false;
	std::thread				fThread;
};


} // namespace


std::unique_ptr<SystemAccent>
CreateSystemAccent(SystemAccent::Changed changed)
{
	const dbus::Library* dbus = dbus::Load();
	if (dbus == nullptr)
		return nullptr;
	auto connected = dbus::ConnectSession(*dbus);
	if (!connected.has_value())
		return nullptr;
	DBusError error;
	dbus->ErrorInit(&error);
	dbus->BusAddMatch(*connected,
		"type='signal',interface='org.freedesktop.portal.Settings',member='SettingChanged',"
		"arg0='org.freedesktop.appearance',arg1='accent-color'",
		&error);
	dbus->ErrorFree(&error);
	return std::make_unique<DbusAccent>(*dbus, *connected, std::move(changed));
}


} // namespace ngui::platform::sdl3
