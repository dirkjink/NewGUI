/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Bitmap.h>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/gfx/ImageRegistry.h>


namespace {


using namespace ngui;


// The bytes of \a bitmap's first row, its first \a count bytes.
std::vector<std::uint8_t>
FirstBytes(const BBitmap& bitmap, std::size_t count)
{
	const std::span<const std::uint8_t> bits = bitmap.Bits();
	return {bits.begin(), bits.begin() + static_cast<std::ptrdiff_t>(count)};
}


// A one pixel B_RGBA32 bitmap holding \a b, \a g, \a r, \a a.
BBitmap
OnePixel(std::uint8_t b, std::uint8_t g, std::uint8_t r, std::uint8_t a)
{
	BBitmap bitmap({0, 0, 1, 1}, BColorSpace::RGBA32);
	const std::span<std::uint8_t> bits = bitmap.Bits();
	bits[0] = b;
	bits[1] = g;
	bits[2] = r;
	bits[3] = a;
	return bitmap;
}


TEST(BitmapTest, RowsArePaddedToFourBytes)
{
	// What is tested: Bitmaps of 5 × 3 in B_RGB32, B_RGB24, B_RGB16 and
	// B_GRAY8, of 9 × 3 in B_GRAY1; one of 10.2 × 3 at (5, 5); one with 100
	// bytes per row; bitmaps that cannot be made: empty bounds, no color
	// space, too few bytes per row, too large.
	// Success criteria: Haiku's rows: the bytes the pixels need (20, 15, 10,
	// 5, 2) padded to four (20, 16, 12, 8, 4), all pixels zero. Bounds are
	// rounded up to whole pixels (11 wide) and keep their origin. A larger
	// row length given is used. The others fail InitCheck() with
	// InvalidArgument and have no bits.
	// Why it matters: Code that fills bitmaps row by row relies on Haiku's
	// row lengths.
	//
	// Given / When
	const BBitmap rgb32({0, 0, 5, 3}, BColorSpace::RGB32);
	const BBitmap rgb24({0, 0, 5, 3}, BColorSpace::RGB24);
	const BBitmap rgb16({0, 0, 5, 3}, BColorSpace::RGB16);
	const BBitmap gray8({0, 0, 5, 3}, BColorSpace::Gray8);
	const BBitmap gray1({0, 0, 9, 3}, BColorSpace::Gray1);
	const BBitmap rounded({5, 5, 15.2f, 8}, BColorSpace::RGBA32);
	const BBitmap wide({0, 0, 5, 3}, BColorSpace::RGB32, 100);
	const BBitmap empty({0, 0, 0, 3}, BColorSpace::RGB32);
	const BBitmap none({0, 0, 5, 3}, BColorSpace::NoColorSpace);
	const BBitmap narrow({0, 0, 5, 3}, BColorSpace::RGB32, 19);
	const BBitmap huge({0, 0, 100000, 100000}, BColorSpace::RGB32);

	// Then
	EXPECT_TRUE(rgb32.IsValid());
	EXPECT_EQ(rgb32.BytesPerRow(), 20);
	EXPECT_EQ(rgb24.BytesPerRow(), 16);
	EXPECT_EQ(rgb16.BytesPerRow(), 12);
	EXPECT_EQ(gray8.BytesPerRow(), 8);
	EXPECT_EQ(gray1.BytesPerRow(), 4);
	EXPECT_EQ(rgb24.BitsLength(), 48u);
	EXPECT_EQ(rgb24.ColorSpace(), BColorSpace::RGB24);
	bool zeros = true;
	for (std::uint8_t byte : rgb24.Bits())
		zeros = zeros && byte == 0;
	EXPECT_TRUE(zeros);
	EXPECT_EQ(rounded.Bounds(), (gfx::Rect{5, 5, 16, 8}));
	EXPECT_EQ(rounded.BytesPerRow(), 44);
	EXPECT_EQ(wide.BytesPerRow(), 100);
	EXPECT_EQ(wide.BitsLength(), 300u);
	for (const BBitmap* bad : {&empty, &none, &narrow, &huge}) {
		EXPECT_FALSE(bad->IsValid());
		ASSERT_FALSE(bad->InitCheck().has_value());
		EXPECT_EQ(bad->InitCheck().error().code, ErrorCode::InvalidArgument);
		EXPECT_EQ(bad->BitsLength(), 0u);
		EXPECT_EQ(bad->Image(), 0u);
	}
}


TEST(BitmapTest, ColorSpacesConvertAsHaikus)
{
	// What is tested: The B_RGBA32 pixel R 0xff, G 0x80, B 0x10, A 0x40
	// imported into a one pixel bitmap of every color space; A 0x80 into
	// B_RGBA15; the results imported back into B_RGBA32.
	// Success criteria: Haiku's layouts, in memory: RGB32 10 80 ff ff (the
	// unused byte opaque), RGBA32_BIG 40 ff 80 10, RGB32_BIG ff ff 80 10,
	// RGB24 10 80 ff, RGB24_BIG ff 80 10, RGB16 0xfc02 (5, 6, 5 top bits),
	// RGB16_BIG swapped, RGB15 and RGBA15 0x7e02 (alpha bit 0 for 0x40, set
	// for 0x80), GRAY8 (308 R + 600 G + 116 B) / 1024 = 153, GRAY1 a clear
	// bit (bright). Back: the components keep their top bits (RGB16 red
	// 0xf8, not 0xfc: NewGUI takes no bits of green), the brightness is
	// gray, GRAY1 white, one alpha bit 0 or 0xff, no alpha opaque.
	// Why it matters: Images come in all of Haiku's formats; ImportBits()
	// must give the same pixels as Haiku.
	//
	// Given
	const BBitmap source = OnePixel(0x10, 0x80, 0xff, 0x40);
	const auto convert = [&](const BBitmap& from, BColorSpace space) {
		BBitmap to({0, 0, 1, 1}, space);
		EXPECT_TRUE(to.ImportBits(from).has_value());
		return to;
	};
	const auto back = [&](const BBitmap& from) {
		return FirstBytes(convert(from, BColorSpace::RGBA32), 4);
	};
	using Bytes = std::vector<std::uint8_t>;

	// When
	const BBitmap rgb32 = convert(source, BColorSpace::RGB32);
	const BBitmap rgba32Big = convert(source, BColorSpace::RGBA32Big);
	const BBitmap rgb32Big = convert(source, BColorSpace::RGB32Big);
	const BBitmap rgb24 = convert(source, BColorSpace::RGB24);
	const BBitmap rgb24Big = convert(source, BColorSpace::RGB24Big);
	const BBitmap rgb16 = convert(source, BColorSpace::RGB16);
	const BBitmap rgb16Big = convert(source, BColorSpace::RGB16Big);
	const BBitmap rgb15 = convert(source, BColorSpace::RGB15);
	const BBitmap rgba15 = convert(source, BColorSpace::RGBA15);
	const BBitmap rgba15Opaque = convert(OnePixel(0x10, 0x80, 0xff, 0x80),
		BColorSpace::RGBA15);
	const BBitmap gray8 = convert(source, BColorSpace::Gray8);
	const BBitmap gray1 = convert(source, BColorSpace::Gray1);
	const BBitmap black = convert(OnePixel(0, 0, 0, 0xff), BColorSpace::Gray1);

	// Then
	EXPECT_EQ(FirstBytes(rgb32, 4), (Bytes{0x10, 0x80, 0xff, 0xff}));
	EXPECT_EQ(FirstBytes(rgba32Big, 4), (Bytes{0x40, 0xff, 0x80, 0x10}));
	EXPECT_EQ(FirstBytes(rgb32Big, 4), (Bytes{0xff, 0xff, 0x80, 0x10}));
	EXPECT_EQ(FirstBytes(rgb24, 3), (Bytes{0x10, 0x80, 0xff}));
	EXPECT_EQ(FirstBytes(rgb24Big, 3), (Bytes{0xff, 0x80, 0x10}));
	EXPECT_EQ(FirstBytes(rgb16, 2), (Bytes{0x02, 0xfc}));
	EXPECT_EQ(FirstBytes(rgb16Big, 2), (Bytes{0xfc, 0x02}));
	EXPECT_EQ(FirstBytes(rgb15, 2), (Bytes{0x02, 0x7e}));
	EXPECT_EQ(FirstBytes(rgba15, 2), (Bytes{0x02, 0x7e}));
	EXPECT_EQ(FirstBytes(rgba15Opaque, 2), (Bytes{0x02, 0xfe}));
	EXPECT_EQ(FirstBytes(gray8, 1), (Bytes{153}));
	EXPECT_EQ(FirstBytes(gray1, 1), (Bytes{0x00}));
	EXPECT_EQ(FirstBytes(black, 1), (Bytes{0x80}));

	EXPECT_EQ(back(rgb32), (Bytes{0x10, 0x80, 0xff, 0xff}));
	EXPECT_EQ(back(rgba32Big), (Bytes{0x10, 0x80, 0xff, 0x40}));
	EXPECT_EQ(back(rgb24Big), (Bytes{0x10, 0x80, 0xff, 0xff}));
	EXPECT_EQ(back(rgb16), (Bytes{0x10, 0x80, 0xf8, 0xff}));
	EXPECT_EQ(back(rgb16Big), (Bytes{0x10, 0x80, 0xf8, 0xff}));
	EXPECT_EQ(back(rgba15), (Bytes{0x10, 0x80, 0xf8, 0x00}));
	EXPECT_EQ(back(rgba15Opaque), (Bytes{0x10, 0x80, 0xf8, 0xff}));
	EXPECT_EQ(back(gray8), (Bytes{153, 153, 153, 0xff}));
	EXPECT_EQ(back(gray1), (Bytes{0xff, 0xff, 0xff, 0xff}));
	EXPECT_EQ(back(black), (Bytes{0x00, 0x00, 0x00, 0xff}));
}


TEST(BitmapTest, PartsAreImportedAndClipped)
{
	// What is tested: Into a 4 × 3 B_GRAY8 bitmap: a 2 × 2 part of a 3 × 3
	// source from (1, 1) to (2, 0); the source at (-1, 0) to (3, 2), partly
	// outside both; raw rows with kAnyBytesPerRow and an offset of 4; data
	// shorter than the bitmap; a bitmap of other bounds; bad arguments.
	// Success criteria: The part lands at (2, 0) .. (3, 1), the rest stays.
	// The parts outside either are clipped, so (−1, 0) starts the source
	// one pixel further in. Raw rows go to the second row; short data fills
	// what it has. Other bounds, a negative size or row length and an
	// offset beyond the bits fail with InvalidArgument.
	// Why it matters: Images are composed of parts; clipping must keep
	// every write inside the bitmap.
	//
	// Given
	BBitmap source({0, 0, 3, 3}, BColorSpace::Gray8);
	for (int32 y = 0; y < 3; y++) {
		for (int32 x = 0; x < 3; x++)
			source.Bits()[static_cast<std::size_t>(y * 4 + x)] = static_cast<std::uint8_t>(
				10 * (y + 1) + x);
	}
	BBitmap target({0, 0, 4, 3}, BColorSpace::Gray8);
	const auto row = [&](int32 y) {
		const std::span<const std::uint8_t> bits = target.Bits();
		return std::vector<std::uint8_t>(bits.begin() + y * 4, bits.begin() + y * 4 + 4);
	};
	using Bytes = std::vector<std::uint8_t>;

	// When / Then
	ASSERT_TRUE(target.ImportBits(source, {1, 1}, {2, 0}, {2, 2}).has_value());
	EXPECT_EQ(row(0), (Bytes{0, 0, 21, 22}));
	EXPECT_EQ(row(1), (Bytes{0, 0, 31, 32}));
	EXPECT_EQ(row(2), (Bytes{0, 0, 0, 0}));
	ASSERT_TRUE(target.ImportBits(source, {-1, 0}, {3, 2}, {3, 3}).has_value());
	EXPECT_EQ(row(2), (Bytes{0, 0, 0, 0}));
	ASSERT_TRUE(target.ImportBits(source, {0, 0}, {-1, 2}, {3, 1}).has_value());
	EXPECT_EQ(row(2), (Bytes{11, 12, 0, 0}));
	const std::vector<std::uint8_t> raw = {1, 2, 3, 4, 5, 6, 7, 8};
	ASSERT_TRUE(target.ImportBits(raw, kAnyBytesPerRow, 4, BColorSpace::Gray8).has_value());
	EXPECT_EQ(row(0), (Bytes{0, 0, 21, 22}));
	EXPECT_EQ(row(1), (Bytes{1, 2, 3, 4}));
	EXPECT_EQ(row(2), (Bytes{5, 6, 7, 8}));
	const std::vector<std::uint8_t> shortData = {9, 9};
	ASSERT_TRUE(target.ImportBits(shortData, 4, 0, BColorSpace::Gray8).has_value());
	EXPECT_EQ(row(0), (Bytes{9, 9, 21, 22}));

	const BBitmap other({0, 0, 3, 3}, BColorSpace::Gray8);
	EXPECT_FALSE(target.ImportBits(other).has_value());
	EXPECT_FALSE(target.ImportBits(raw, -2, 0, BColorSpace::Gray8).has_value());
	EXPECT_FALSE(target.ImportBits(raw, 4, 13, BColorSpace::Gray8).has_value());
	EXPECT_FALSE(target.ImportBits(raw, 4, BColorSpace::Gray8, {0, 0}, {0, 0}, {-1, 1})
		.has_value());
	EXPECT_FALSE(target.ImportBits(raw, 4, 0, BColorSpace::NoColorSpace).has_value());
}


TEST(BitmapTest, TheImageFollowsThePixels)
{
	// What is tested: A 3 × 2 B_RGB32 bitmap: Image() twice; the pixels
	// changed through Bits(); a copy; the bitmap destroyed.
	// Success criteria: Image() registers the pixels once (the same handle,
	// one reference, 3 × 2 pixels). Changed pixels give a new handle, and the
	// old one is released. A copy has the same pixels and an image of its
	// own. A destroyed bitmap releases its image.
	// Why it matters: Views draw bitmaps through the image registry; images
	// must neither leak nor show old pixels.
	//
	// Given
	gfx::ImageRegistry& registry = gfx::ImageRegistry::Default();
	gfx::ImageHandle last = 0;
	gfx::ImageHandle copied = 0;

	// When / Then
	{
		BBitmap bitmap({0, 0, 3, 2}, BColorSpace::RGB32);
		const gfx::ImageHandle first = bitmap.Image();
		ASSERT_NE(first, 0u);
		EXPECT_EQ(bitmap.Image(), first);
		EXPECT_EQ(registry.ReferenceCount(first), 1u);
		EXPECT_EQ(registry.PixelSize(first), (gfx::Size{3, 2}));
		bitmap.Bits()[0] = 0xff;
		EXPECT_EQ(registry.ReferenceCount(first), 0u);
		last = bitmap.Image();
		EXPECT_NE(last, first);
		const BBitmap copy(bitmap);
		EXPECT_EQ(copy.Bits()[0], 0xff);
		copied = copy.Image();
		EXPECT_NE(copied, last);
		EXPECT_EQ(registry.ReferenceCount(copied), 1u);
	}
	EXPECT_EQ(registry.ReferenceCount(last), 0u);
	EXPECT_EQ(registry.ReferenceCount(copied), 0u);
}


} // namespace
