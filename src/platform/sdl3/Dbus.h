/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The system's libdbus on Linux, loaded with dlopen() as SDL loads it;
// NewGUI builds against its headers only. For the desktop's notifications
// (module P5) and its accent color (module G6).


#include <expected>

#include <dbus/dbus.h>

#include <ngui/Error.h>


namespace ngui::platform::sdl3::dbus {


/*!	The functions of libdbus NewGUI needs, from libdbus-1.so.3. */
struct Library {
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
	dbus_bool_t		(*IterInit)(DBusMessage*, DBusMessageIter*) = nullptr;
	int				(*IterArgType)(DBusMessageIter*) = nullptr;
	void			(*IterRecurse)(DBusMessageIter*, DBusMessageIter*) = nullptr;
	void			(*IterGetBasic)(DBusMessageIter*, void*) = nullptr;
	dbus_bool_t		(*IterNext)(DBusMessageIter*) = nullptr;
};


/*!	libdbus, loaded on first use; nullptr without it (or with one that
	lacks a function). */
const Library*			Load();

/*!	A private connection to the session bus that does not end the process
	when it closes; NotSupported without libdbus or without a session bus to
	connect to (DBUS_SESSION_BUS_ADDRESS or $XDG_RUNTIME_DIR/bus; libdbus would
	otherwise try to launch one through X11). */
std::expected<DBusConnection*, Error> ConnectSession(const Library& dbus);


} // namespace ngui::platform::sdl3::dbus
