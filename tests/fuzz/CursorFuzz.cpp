/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <ngui/Bitmap.h>
#include <ngui/Cursor.h>


NGUI_FUZZ_TARGET(Cursor)
{
	// What is tested: Arbitrary bytes as R5 cursor data; then, from the
	// same bytes, a bitmap of 1 to 32 × 1 to 32 pixels in any color space
	// with those bytes as pixels, made a cursor with a hot spot of any float
	// value (NaN and infinity included).
	// Success criteria: No crash or sanitizer report. R5 data are accepted
	// exactly when they are 68 bytes of size 16, depth 1 and a hot spot in
	// the 16 × 16 pixels, and give a 16 × 16 image. A cursor of a valid
	// bitmap is valid, its image as large as the bitmap with all its bytes,
	// the hot spot inside it.
	// Why it matters: Cursor data come from applications and resources;
	// the conversion reads bitmaps of any color space.
	//
	using namespace ngui;
	const BCursor r5(data);
	const bool wellFormed = data.size() == 68 && data[0] == 16 && data[1] == 1 && data[2] < 16
		&& data[3] < 16;
	NGUI_FUZZ_CHECK(r5.InitCheck().has_value() == wellFormed);
	if (wellFormed) {
		NGUI_FUZZ_CHECK(r5.Shape().image != nullptr && r5.Shape().image->width == 16
			&& r5.Shape().image->rgba.size() == 16 * 16 * 4);
	}

	if (data.size() < 11)
		return;
	const float width = static_cast<float>(1 + data[0] % 32);
	const float height = static_cast<float>(1 + data[1] % 32);
	const auto space = static_cast<BColorSpace>(data[2] % 15);
	float hotspotX = 0;
	float hotspotY = 0;
	std::memcpy(&hotspotX, data.data() + 3, 4);
	std::memcpy(&hotspotY, data.data() + 7, 4);
	BBitmap bitmap({0, 0, width, height}, space);
	if (bitmap.IsValid()) {
		const std::span<const std::uint8_t> pixels = data.subspan(11);
		(void)bitmap.ImportBits(pixels, bitmap.BytesPerRow(), 0, space);
	}
	const BCursor cursor(bitmap, {hotspotX, hotspotY});
	NGUI_FUZZ_CHECK(cursor.InitCheck().has_value() == bitmap.IsValid());
	if (bitmap.IsValid()) {
		const platform::CursorImage& image = *cursor.Shape().image;
		NGUI_FUZZ_CHECK(image.width == static_cast<std::int32_t>(width)
			&& image.height == static_cast<std::int32_t>(height));
		NGUI_FUZZ_CHECK(image.rgba.size() == static_cast<std::size_t>(image.width)
			* static_cast<std::size_t>(image.height) * 4);
		NGUI_FUZZ_CHECK(image.hotspot.x >= 0 && image.hotspot.x < width
			&& image.hotspot.y >= 0 && image.hotspot.y < height);
	}
}
