/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>


namespace ngui {


// Integer type aliases for code ported from Haiku (port phase, see
// CONVENTIONS.md, section 4.3). New code uses the <cstdint> types directly.
// The aliases are removed before the first public release (section 7).

using int8 = std::int8_t;
using uint8 = std::uint8_t;
using int16 = std::int16_t;
using uint16 = std::uint16_t;
using int32 = std::int32_t;
using uint32 = std::uint32_t;
using int64 = std::int64_t;
using uint64 = std::uint64_t;


} // namespace ngui
