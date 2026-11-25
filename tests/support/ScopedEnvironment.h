/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdlib>
#include <string>


namespace ngui::test {


// Sets an environment variable for the life of the object.
class ScopedEnvironment {
public:
	ScopedEnvironment(const char* name, const char* value)
		:
		fName(name)
	{
#ifdef _WIN32
		(void)_putenv_s(name, value);
#else
		(void)setenv(name, value, 1);
#endif
	}

	~ScopedEnvironment()
	{
#ifdef _WIN32
		(void)_putenv_s(fName.c_str(), "");
#else
		(void)unsetenv(fName.c_str());
#endif
	}

	ScopedEnvironment(const ScopedEnvironment&) = delete;
	ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
	std::string fName;
};


} // namespace ngui::test
