/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/CpuPresenter.h>

#include <algorithm>
#include <cstring>


namespace ngui::platform {


namespace {


// Whether \a bytes hold \a height rows of \a stride bytes, the last one at
// least \a width pixels long.
bool
Holds(std::size_t bytes, std::int32_t width, std::int32_t height, std::size_t stride)
{
	if (width <= 0 || height <= 0)
		return true;
	const auto row = static_cast<std::size_t>(width) * 4;
	return stride >= row && bytes >= stride * static_cast<std::size_t>(height - 1) + row;
}


} // namespace


std::int64_t
CopyPixels(const gfx::CpuPixels& source, const PixelTarget& target,
	std::span<const gfx::IntRect> regions)
{
	if (!Holds(source.bytes.size(), source.width, source.height, source.stride)
		|| !Holds(target.bytes.size(), target.width, target.height, target.stride)) {
		return 0;
	}
	const bool swap = source.format != target.format;
	const std::int32_t width = std::min(source.width, target.width);
	const std::int32_t height = std::min(source.height, target.height);
	std::int64_t copied = 0;
	for (const gfx::IntRect& region : regions) {
		const std::int32_t x0 = std::clamp(region.x0, 0, std::max(width, 0));
		const std::int32_t x1 = std::clamp(region.x1, x0, std::max(width, 0));
		const std::int32_t y0 = std::clamp(region.y0, 0, std::max(height, 0));
		const std::int32_t y1 = std::clamp(region.y1, y0, std::max(height, 0));
		const auto rowBytes = static_cast<std::size_t>(x1 - x0) * 4;
		for (std::int32_t y = y0; y < y1; y++) {
			const std::byte* from = source.bytes.data()
				+ static_cast<std::size_t>(y) * source.stride + static_cast<std::size_t>(x0) * 4;
			std::byte* to = target.bytes.data()
				+ static_cast<std::size_t>(y) * target.stride + static_cast<std::size_t>(x0) * 4;
			if (!swap) {
				std::memcpy(to, from, rowBytes);
				continue;
			}
			for (std::size_t i = 0; i < rowBytes; i += 4) {
				to[i] = from[i + 2];
				to[i + 1] = from[i + 1];
				to[i + 2] = from[i];
				to[i + 3] = from[i + 3];
			}
		}
		copied += static_cast<std::int64_t>(x1 - x0) * (y1 - y0);
	}
	return copied;
}


} // namespace ngui::platform
