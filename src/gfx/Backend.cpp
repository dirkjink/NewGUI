/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/Backend.h>


namespace ngui::gfx {


bool
PixelData::IsValid() const
{
	if (width <= 0 || height <= 0)
		return false;
	const std::size_t row = static_cast<std::size_t>(width) * 4;
	if (stride < row || bytes.size() < row)
		return false;
	// All rows but the last need the full stride, the last only its pixels;
	// divided instead of multiplied, so that a huge stride cannot overflow.
	const std::size_t rowsBeforeLast = static_cast<std::size_t>(height) - 1;
	return rowsBeforeLast == 0 || (bytes.size() - row) / stride >= rowsBeforeLast;
}


Size
Surface::LogicalSize() const
{
	const Size device = DeviceSize();
	const float scale = Scale();
	return {device.w / scale, device.h / scale};
}


} // namespace ngui::gfx
