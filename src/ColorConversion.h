/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <span>

#include <ngui/Bitmap.h>
#include <ngui/Error.h>


namespace ngui::detail {


// Bytes per row of \a width pixels in \a colorSpace, without padding; 0 for
// a color space bitmaps do not support (Haiku's get_raw_bytes_per_row()).
std::int32_t					RawBytesPerRow(BColorSpace colorSpace, std::int32_t width);

// Bytes per row padded to four bytes (Haiku's get_bytes_per_row()).
std::int32_t					BytesPerRow(BColorSpace colorSpace, std::int32_t width);

// Converts \a width × \a height pixels of \a source (rows of \a sourceBpr
// bytes in \a sourceSpace) from (\a fromX, \a fromY) into \a dest (rows of
// \a destBpr bytes in \a destSpace) at (\a toX, \a toY); Haiku's
// BPrivate::ConvertBits(). Clipped to both buffers; never reads or writes
// outside them. InvalidArgument for negative sizes or an unsupported color
// space.
std::expected<void, Error>		ConvertBits(std::span<const std::uint8_t> source,
									std::span<std::uint8_t> dest, std::int32_t sourceBpr,
									std::int32_t destBpr, BColorSpace sourceSpace,
									BColorSpace destSpace, std::int32_t fromX,
									std::int32_t fromY, std::int32_t toX, std::int32_t toY,
									std::int32_t width, std::int32_t height);


} // namespace ngui::detail
