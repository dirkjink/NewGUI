/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Image.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

#include <gtest/gtest.h>


namespace {


using namespace ngui;
using namespace ngui::test;


// The CRC-32 of PNG chunks (ISO 3309).
std::uint32_t
Crc32(std::span<const std::byte> bytes)
{
	std::uint32_t crc = 0xffffffffu;
	for (const std::byte byte : bytes) {
		crc ^= static_cast<std::uint32_t>(byte);
		for (int bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
	}
	return crc ^ 0xffffffffu;
}


TEST(ImageTest, PngRoundTripKeepsEveryByte)
{
	// What is tested: EncodePng() and DecodePng() of a 3 × 2 image with
	// opaque, transparent and half transparent pixels, and WritePng() and
	// ReadPng() through a file.
	// Success criteria: Both round trips give the same image byte for byte;
	// encoding twice gives the same bytes; a semi-transparent pixel keeps
	// its unpremultiplied color.
	// Why it matters: References are PNG files; a lossy or nondeterministic
	// round trip would make snapshots fail or churn.
	//
	// Given
	Image image(3, 2, {255, 255, 255, 255});
	image.Set(0, 0, {255, 0, 0, 255});
	image.Set(1, 0, {0, 0, 0, 0});
	image.Set(2, 1, {10, 200, 30, 128});
	const std::filesystem::path file = std::filesystem::temp_directory_path()
		/ "ngui-image-test.png";

	// When
	const auto encoded = EncodePng(image);
	const auto again = EncodePng(image);
	const auto decoded = DecodePng(*encoded);
	ASSERT_TRUE(WritePng(image, file).has_value());
	const auto read = ReadPng(file);
	std::filesystem::remove(file);

	// Then
	ASSERT_TRUE(encoded.has_value());
	EXPECT_EQ(*encoded, *again);
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(*decoded, image);
	EXPECT_EQ(decoded->At(2, 1), (Rgba8{10, 200, 30, 128}));
	ASSERT_TRUE(read.has_value());
	EXPECT_EQ(*read, image);
}


TEST(ImageTest, BadInputIsAnErrorNotACrash)
{
	// What is tested: DecodePng() of empty data, of bytes that are no PNG, of
	// a truncated PNG and of a PNG header that claims 100000 × 100000
	// pixels; ReadPng() of a missing file; an Image larger than the limits.
	// Success criteria: BadData for the first three, OutOfRange for the
	// huge one (before any allocation), IoError for the missing file, an
	// empty Image for the oversized one.
	// Why it matters: References come from files that may be damaged or
	// replaced; the test kit must report that, not allocate 40 GB.
	//
	// Given
	Image tiny(2, 2, {1, 2, 3, 255});
	std::vector<std::byte> png = *EncodePng(tiny);
	const std::vector<std::byte> truncated(png.begin(), png.begin() + png.size() / 2);
	std::vector<std::byte> huge = png;
	// The IHDR data start at byte 16 with width and height, big-endian; its
	// CRC over type and data (bytes 12 to 28) follows at byte 29.
	const std::array<std::uint8_t, 8> size = {0, 1, 0x86, 0xa0, 0, 1, 0x86, 0xa0};
	for (std::size_t i = 0; i < size.size(); i++)
		huge[16 + i] = static_cast<std::byte>(size[i]);
	const std::uint32_t crc = Crc32(std::span(huge).subspan(12, 17));
	for (std::size_t i = 0; i < 4; i++)
		huge[29 + i] = static_cast<std::byte>(crc >> (24 - 8 * i));
	const std::array<std::byte, 4> garbage = {std::byte{1}, std::byte{2}, std::byte{3},
		std::byte{4}};

	// When / Then
	EXPECT_EQ(DecodePng({}).error().code, ErrorCode::BadData);
	EXPECT_EQ(DecodePng(garbage).error().code, ErrorCode::BadData);
	EXPECT_EQ(DecodePng(truncated).error().code, ErrorCode::BadData);
	EXPECT_EQ(DecodePng(huge).error().code, ErrorCode::OutOfRange);
	EXPECT_EQ(ReadPng("/nonexistent/ngui.png").error().code, ErrorCode::IoError);
	EXPECT_TRUE(Image(kMaxImageDimension + 1, 1).IsEmpty());
	EXPECT_TRUE(Image(8192, 8192).IsEmpty());
	EXPECT_EQ(EncodePng(Image()).error().code, ErrorCode::InvalidArgument);
}


TEST(ImageTest, CpuPixelsAreUnpremultiplied)
{
	// What is tested: Image::FromCpuPixels() for BGRA and RGBA surfaces with
	// a row stride larger than the width.
	// Success criteria: Components are reordered to RGBA; premultiplied
	// (64, 0, 0, 128) becomes (128, 0, 0, 128); alpha 0 becomes transparent
	// black; the padding at the end of a row is skipped.
	// Why it matters: Skia renders into premultiplied BGRA on x86; PNG
	// files and the comparison work on straight RGBA (specification 9).
	//
	// Given: 2 × 2 pixels, stride 12 (one padding pixel per row).
	std::array<std::byte, 24> bytes{};
	const auto put = [&](std::size_t offset, std::uint8_t c0, std::uint8_t c1, std::uint8_t c2,
		std::uint8_t alpha) {
		bytes[offset] = std::byte{c0};
		bytes[offset + 1] = std::byte{c1};
		bytes[offset + 2] = std::byte{c2};
		bytes[offset + 3] = std::byte{alpha};
	};
	put(0, 0, 0, 255, 255);
	put(4, 0, 0, 64, 128);
	put(12, 9, 9, 9, 0);
	put(16, 255, 255, 255, 255);
	const gfx::CpuPixels bgra = {2, 2, 12, gfx::PixelFormat::Bgra8Premul, bytes};
	const gfx::CpuPixels rgba = {2, 2, 12, gfx::PixelFormat::Rgba8Premul, bytes};

	// When
	const Image fromBgra = Image::FromCpuPixels(bgra);
	const Image fromRgba = Image::FromCpuPixels(rgba);

	// Then
	EXPECT_EQ(fromBgra.At(0, 0), (Rgba8{255, 0, 0, 255}));
	EXPECT_EQ(fromBgra.At(1, 0), (Rgba8{128, 0, 0, 128}));
	EXPECT_EQ(fromBgra.At(0, 1), (Rgba8{0, 0, 0, 0}));
	EXPECT_EQ(fromBgra.At(1, 1), (Rgba8{255, 255, 255, 255}));
	EXPECT_EQ(fromRgba.At(0, 0), (Rgba8{0, 0, 255, 255}));
	EXPECT_EQ(fromRgba.At(1, 0), (Rgba8{0, 0, 128, 128}));
}


} // namespace
