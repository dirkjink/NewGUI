/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BString as far as the ported looks use it: a std::string with
// String(); BFont::TruncateString() takes it as std::string*.


#include <string>


namespace ngui::compat {


class BString : public std::string {
public:
	using std::string::string;

			const char*			String() const
								{
									return c_str();
								}
};


} // namespace ngui::compat
