/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The system's notifications on Windows (module P5): a balloon of an icon in
// the notification area (Shell_NotifyIcon()), which Windows 10 and 11 show
// as a toast. Needs no registration of the program (no AUMID, no shortcut
// in the start menu). Plain C++ outside; windows.h inside
// (WinNotifications.cpp).


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <ngui/Error.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


/*!	What a notification becomes as a balloon: the icon's tip (the group or
	the application), the title and the text in UTF-16 within the
	shell's limits (127, 63 and 255 characters, no surrogate pair cut), and
	the NIIF_ flags (NIIF_INFO, NIIF_WARNING for Important, NIIF_ERROR,
	NIIF_USER with an icon). A progress ends the text with "75 %". */
struct Balloon {
			std::wstring		tip;
			std::wstring		title;
			std::wstring		text;
			std::uint32_t		flags = 0;
};


/*!	\a spec as a balloon. */
Balloon							BalloonOf(const NotificationSpec& spec);


/*!	The window message of the icon (WM_APP + 1): Windows sends its events
	with it, NIN_BALLOONUSERCLICK in the low word of lParam for a click. */
inline constexpr unsigned int	kNotificationIconMessage = 0x8001;


/*!	The icon in the notification area, with a hidden window on a thread of
	its own: Show() shows a balloon, which takes the place of the one
	before; a click on it sends its onClick to its target. */
class WinNotifications {
public:
	static	std::expected<std::unique_ptr<WinNotifications>, Error> Create();
									/*!< Adds the icon; NotSupported if the shell takes
										none (no taskbar). */
								~WinNotifications();
									/*!< Takes the icon away and ends the thread. */

			void				Show(const NotificationSpec& spec);
									/*!< Shows \a spec on the thread, at once; from any
										thread. */

			void*				Window() const;
									/*!< The hidden window (HWND) the icon's events go
										to, for tests. */
			std::size_t			CountShown() const;
									/*!< How many balloons were shown, for tests. */

private:
			struct Impl;

	explicit					WinNotifications(std::unique_ptr<Impl> impl);

			std::unique_ptr<Impl> fImpl;
};


} // namespace ngui::platform::sdl3
