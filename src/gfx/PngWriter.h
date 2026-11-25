/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <span>
#include <string>
#include <vector>


namespace ngui::gfx::detail {


// A PNG of \a width × \a height RGBA pixels (8 bits, not premultiplied,
// rows of width * 4 bytes), without compression: the image data is a zlib
// stream of stored deflate blocks. Deterministic and without dependencies,
// for the images the SVG export embeds (specification 19.3); not for
// files that should be small.
std::vector<std::uint8_t> EncodeStoredPng(std::uint32_t width, std::uint32_t height,
	std::span<const std::uint8_t> rgba);

// \a bytes in base64 (RFC 4648, with padding), for data: URIs.
std::string Base64(std::span<const std::uint8_t> bytes);


} // namespace ngui::gfx::detail
