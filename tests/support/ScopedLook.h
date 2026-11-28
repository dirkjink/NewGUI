/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>
#include <string_view>

#include <ngui/LookRegistry.h>


namespace ngui::test {


// The current look of LookRegistry::Default() set for the duration of a
// test, the previous one current again afterwards. The text system's role
// fonts follow it (LookInfo::fonts): the BeOS look draws in Liberation,
// the Haiku look in the bundled Noto.
class ScopedLook {
public:
	explicit ScopedLook(std::string_view id)
		:
		fPrevious(std::string(LookRegistry::Default().Current()->Info().id))
	{
		(void)LookRegistry::Default().SetCurrent(id);
	}

	~ScopedLook()
	{
		(void)LookRegistry::Default().SetCurrent(fPrevious);
	}

	ScopedLook(const ScopedLook&) = delete;
	ScopedLook& operator=(const ScopedLook&) = delete;

private:
	std::string fPrevious;
};


} // namespace ngui::test
