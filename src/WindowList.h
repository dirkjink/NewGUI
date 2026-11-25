/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>


namespace ngui::detail {


// Posts a message with \a what to every window of the process (in
// Window.cpp, which keeps the list).
void PostToWindows(std::uint32_t what);


} // namespace ngui::detail
