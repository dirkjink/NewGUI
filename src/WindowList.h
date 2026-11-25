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


namespace ngui {
class BHandler;
class BWindow;
}


namespace ngui::detail {


// Posts a message with \a what to every window of the process (in
// Window.cpp, which keeps the list).
void PostToWindows(std::uint32_t what);

// A window of the process and its title, kept with the list, so that other
// threads (the application's scripting, specification 20.7) find a window
// by its title without touching it.
struct WindowEntry {
	BMessenger		messenger;
	std::string		title;
	std::uint64_t	serial = 0;
						// its place in the order the windows were made
};

// The windows of the process with their titles, in the order they were
// made.
std::vector<WindowEntry> WindowEntries();

// The serial the next window will get.
std::uint64_t NextWindowSerial();


} // namespace ngui::detail


namespace ngui {


// What Record & Replay needs of a window, on its thread.
struct WindowAccess {
	static std::uint64_t Serial(const BWindow& window);
	// The index of \a handler among the window's open popups, -1 if it is
	// none of them.
	static std::int32_t PopupIndex(const BWindow& window, const BHandler* handler);
	// Whether what is typed goes into a view kept out of recordings: the
	// focus, or a view it is in, is BView::IsInputPrivate(), or a text view
	// that hides typing (specification 20.8, T5).
	static bool MasksInput(const BWindow& window);
};


} // namespace ngui
