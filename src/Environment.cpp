/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "Environment.h"

#include <cstdlib>


namespace ngui::detail {


std::optional<std::string>
EnvironmentVariable(const char* name)
{
#ifdef _MSC_VER
	char* value = nullptr;
	std::size_t length = 0;
	if (_dupenv_s(&value, &length, name) != 0 || value == nullptr)
		return std::nullopt;
	std::string copy(value);
	std::free(value);
	return copy;
#else
	const char* value = std::getenv(name);
	if (value == nullptr)
		return std::nullopt;
	return std::string(value);
#endif
}


} // namespace ngui::detail
