/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Checks that Skia from the vcpkg port links with NewGUI's toolchains and
// does what the Skia backend (M9) will rely on; the backend itself follows.


#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"


namespace {


sk_sp<SkSurface>
RedSquare()
{
	sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(16, 16));
	if (!surface)
		return nullptr;
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);
	SkPaint paint;
	paint.setColor(SK_ColorRED);
	paint.setAntiAlias(false);
	canvas->drawRect(SkRect::MakeLTRB(4, 4, 12, 12), paint);
	return surface;
}


TEST(SkiaSmokeTest, RasterSurfaceFillsHalfOpen)
{
	// What is tested: A CPU raster surface (N32, premultiplied, no color
	// space) with a white background and a red rectangle [4, 12) × [4, 12)
	// drawn without antialiasing.
	// Success criteria: Pixels 4 to 11 are red, pixel 12 and pixel 3 are
	// white in both directions.
	// Why it matters: This is the surface of specification 13.3, and Skia's
	// rectangles are half-open like NewGUI's (4.1); the backend can pass
	// rectangles through without converting their edges.
	//
	// When
	const sk_sp<SkSurface> surface = RedSquare();

	// Then
	ASSERT_TRUE(surface);
	SkPixmap pixels;
	ASSERT_TRUE(surface->peekPixels(&pixels));
	EXPECT_EQ(pixels.getColor(4, 4), SK_ColorRED);
	EXPECT_EQ(pixels.getColor(11, 11), SK_ColorRED);
	EXPECT_EQ(pixels.getColor(12, 12), SK_ColorWHITE);
	EXPECT_EQ(pixels.getColor(3, 3), SK_ColorWHITE);
	EXPECT_EQ(pixels.getColor(12, 8), SK_ColorWHITE);
	EXPECT_EQ(pixels.colorSpace(), nullptr);
}


TEST(SkiaSmokeTest, EncodesPng)
{
	// What is tested: SkPngEncoder on the pixels of the raster surface.
	// Success criteria: Encoding succeeds and the data starts with the PNG
	// signature.
	// Why it matters: Screenshots are written as PNG (specification 19.1);
	// the port is built with the feature png for exactly this.
	//
	// Given
	const sk_sp<SkSurface> surface = RedSquare();
	ASSERT_TRUE(surface);
	SkPixmap pixels;
	ASSERT_TRUE(surface->peekPixels(&pixels));

	// When
	SkDynamicMemoryWStream stream;
	const bool encoded = SkPngEncoder::Encode(&stream, pixels, {});
	const sk_sp<SkData> data = stream.detachAsData();

	// Then
	ASSERT_TRUE(encoded);
	ASSERT_GE(data->size(), 8u);
	const auto* bytes = static_cast<const std::uint8_t*>(data->data());
	const std::uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	for (std::size_t i = 0; i < 8; i++)
		EXPECT_EQ(bytes[i], signature[i]) << "byte " << i;
}


} // namespace
