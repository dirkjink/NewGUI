/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <span>

#include <ngui/gfx/Backend.h>
#include <ngui/gfx/IntRect.h>


namespace ngui::platform {


/*!	Writable pixels of a window, e.g. the window surface of SDL. */
struct PixelTarget {
			std::int32_t		width = 0;		/*!< Width in device pixels. */
			std::int32_t		height = 0;		/*!< Height in device pixels. */
			std::size_t			stride = 0;		/*!< Bytes per row. */
			gfx::PixelFormat	format = gfx::PixelFormat::Bgra8Premul;
												/*!< Byte order; the alpha byte is
													written as it is. */
			std::span<std::byte> bytes;			/*!< The pixels. */
};


/*!	The core of the CPU presenter (specification 12): copies the \a regions
	of \a source, device pixels clipped to both images, to \a target,
	swapping red and blue if the byte orders differ. Returns the number of
	pixels copied; 0 if a buffer is smaller than its size and stride say. */
std::int64_t					CopyPixels(const gfx::CpuPixels& source,
									const PixelTarget& target,
									std::span<const gfx::IntRect> regions);


} // namespace ngui::platform
