/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The accent color on Windows (module G6): the desktop window manager keeps
// it in the registry, HKEY_CURRENT_USER\Software\Microsoft\Windows\DWM,
// AccentColor, a DWORD 0xAABBGGRR; a thread of its own hears the key change
// (RegNotifyChangeKeyValue).


#include <memory>
#include <string>

#include "platform/sdl3/SystemAccent.h"


namespace ngui::platform::sdl3 {


/*!	The accent color in the DWORD \a value of the key \a key below
	HKEY_CURRENT_USER, \a changed called on the thread when it changes;
	nullptr without the key. CreateSystemAccent() with the desktop window
	manager's; another for tests. */
std::unique_ptr<SystemAccent>	CreateWinAccent(const std::wstring& key,
									const std::wstring& value,
									SystemAccent::Changed changed);


} // namespace ngui::platform::sdl3
