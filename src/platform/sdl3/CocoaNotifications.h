/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The system's notifications on macOS (module P5): UNUserNotificationCenter,
// which only an application bundle may use (it throws for a program
// without one). Objective-C++ inside (CocoaNotifications.mm), plain C++
// outside; from any thread.


#include <expected>
#include <memory>
#include <string>

#include <ngui/Error.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


/*!	What a notification becomes for UNUserNotificationCenter: the request's
	identifier (the notification's id, so a new one with it takes the place
	of the one delivered; else a new UUID), title, body (a progress ends it
	with "75 %"), the thread it is grouped in (the group or the
	application) and whether it is time sensitive (Important, Error). */
struct CocoaNotificationContent {
			std::string			identifier;
			std::string			title;
			std::string			body;
			std::string			thread;
			bool				timeSensitive = false;
};


/*!	\a spec as content of a request. */
CocoaNotificationContent		ContentOf(const NotificationSpec& spec);


/*!	The application's notifications: asks for permission once, then adds a
	request per notification; a click on one (its default action) sends its
	onClick to its target. macOS keeps a notification until the user removes
	it, so the timeout is not used. */
class CocoaNotifications {
public:
	static	std::expected<std::unique_ptr<CocoaNotifications>, Error> Create();
									/*!< NotSupported outside an application bundle (no
										bundle identifier). */
								~CocoaNotifications();
									/*!< No more clicks. */

			void				Show(const NotificationSpec& spec);
									/*!< Hands \a spec to the notification center; from any
										thread. */

	// For tests
	static	void				Clicked(const std::string& identifier);
									/*!< Acts as the user clicking the notification
										\a identifier: its onClick goes to its target. */
	static	void				Remember(const std::string& identifier,
									const NotificationSpec& spec);
									/*!< Keeps what a click on \a identifier sends, as
										Show() does. */

private:
								CocoaNotifications() = default;
};


} // namespace ngui::platform::sdl3
