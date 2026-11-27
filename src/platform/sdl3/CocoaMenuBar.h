/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The application's menu bar on macOS (module P3): NewGUI's application
// menu, then the menus of the window in front. Objective-C++ inside
// (CocoaMenuBar.mm), plain C++ outside; main thread only, Apple only.


#include <vector>

#include <ngui/Messenger.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


// Makes NSApp's main menu the application menu followed by \a menus. Choosing
// an entry sends kSystemMenuInvoked with its id to \a window, opening and
// closing one of \a menus kSystemMenusBeginning and kSystemMenusEnded.
void							InstallMenuBar(const std::vector<SystemMenuItem>& menus,
									BMessenger window);


} // namespace ngui::platform::sdl3
