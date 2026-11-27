/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The desktop's notifications on Linux (module P5): the Desktop
// Notifications Specification of freedesktop.org, org.freedesktop.Notifications
// on the session bus. The system's libdbus is loaded with dlopen(), as SDL
// loads it; NewGUI builds against its headers only.


#include <expected>
#include <memory>

#include <ngui/Error.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


/*!	A connection of its own to the session bus, with a thread that sends
	the notifications and hears the clicks: a click ("default" action)
	sends the notification's onClick to its target. */
class DbusNotifications {
public:
	static	std::expected<std::unique_ptr<DbusNotifications>, Error> Create();
									/*!< Connects; NotSupported without libdbus or a
										session bus (DBUS_SESSION_BUS_ADDRESS or
										$XDG_RUNTIME_DIR/bus). */
								~DbusNotifications();
									/*!< Ends the thread and the connection. */

			void				Show(const NotificationSpec& spec);
									/*!< Sends \a spec on the thread, at once; from any
										thread. */

private:
			struct Impl;

	explicit					DbusNotifications(std::unique_ptr<Impl> impl);

			std::unique_ptr<Impl> fImpl;
};


} // namespace ngui::platform::sdl3
