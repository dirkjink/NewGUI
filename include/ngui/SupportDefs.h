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

using int8 = std::int8_t;		/*!< Signed 8-bit integer, port phase only. */
using uint8 = std::uint8_t;		/*!< Unsigned 8-bit integer, port phase only. */
using int16 = std::int16_t;		/*!< Signed 16-bit integer, port phase only. */
using uint16 = std::uint16_t;	/*!< Unsigned 16-bit integer, port phase only. */
using int32 = std::int32_t;		/*!< Signed 32-bit integer, port phase only. */
using uint32 = std::uint32_t;	/*!< Unsigned 32-bit integer, port phase only. */
using int64 = std::int64_t;		/*!< Signed 64-bit integer, port phase only. */
using uint64 = std::uint64_t;	/*!< Unsigned 64-bit integer, port phase only. */


} // namespace ngui
