/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The accent color the user chose for the system (module G6), which SDL
// does not report: on Linux the desktop's portal (org.freedesktop.portal.
// Settings, org.freedesktop.appearance accent-color; DbusAccent.cpp), on
// Windows the registry of the desktop window manager (WinAccent.cpp), on
// macOS NSColor's controlAccentColor (CocoaAccent.mm). Each hears the user
// change it.


#include <functional>
#include <memory>
#include <optional>

#include <ngui/gfx/Color.h>


namespace ngui::platform::sdl3 {


/*!	The system's accent color and its changes. */
class SystemAccent {
public:
	using Changed = std::function<void()>;

	virtual						~SystemAccent() = default;
									/*!< Ends hearing the changes. */

	virtual	std::optional<gfx::Color> Color() const = 0;
									/*!< The accent color as last heard, in 8-bit
										components; std::nullopt when the user chose
										none. From any thread. */
};


/*!	The system's accent color, \a changed called (on a thread of its own or
	the main thread) whenever the user changes it; nullptr where it cannot be
	asked (no session bus, say). */
std::unique_ptr<SystemAccent>	CreateSystemAccent(SystemAccent::Changed changed);


} // namespace ngui::platform::sdl3
