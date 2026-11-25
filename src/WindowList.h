/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <string>
#include <vector>

#include <ngui/Messenger.h>


namespace ngui::detail {


// Posts a message with \a what to every window of the process (in
// Window.cpp, which keeps the list).
void PostToWindows(std::uint32_t what);

// A window of the process and its title, kept with the list, so that other
// threads (the application's scripting, specification 20.7) find a window
// by its title without touching it.
struct WindowEntry {
	BMessenger	messenger;
	std::string	title;
};

// The windows of the process with their titles, in the order they were
// made.
std::vector<WindowEntry> WindowEntries();


} // namespace ngui::detail
