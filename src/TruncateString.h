/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/InterfaceDefs.cpp (commit
 * b1d497bb69), truncate_string(): the truncation of BFont::TruncateString()
 * (module X5). Internal; text systems use it with their own escapements.
 */
#pragma once


#include <span>
#include <string>
#include <string_view>

#include <ngui/TextSystem.h>


namespace ngui {


// \a string shortened with "…" so that it fits \a width. \a escapements has
// the advance of each code point of \a string, in units of \a fontSize, as
// Haiku's BFont::GetEscapements(); \a ellipsisWidth is the width of "…".
// Too few escapements leave \a string as it is.
std::string						TruncateString(std::string_view string, TruncationMode mode,
									float width, std::span<const float> escapements,
									float fontSize, float ellipsisWidth);


} // namespace ngui
