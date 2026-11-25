/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <optional>
#include <string>


namespace ngui::detail {


/*!	The environment variable \a name, nullopt if it is not set. MSVC
	deprecates std::getenv() (C4996) in favour of _dupenv_s(), which copies
	the value; so does this. */
std::optional<std::string> EnvironmentVariable(const char* name);


} // namespace ngui::detail
