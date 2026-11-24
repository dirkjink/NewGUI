/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/interface/ColorConversion.cpp and the row
// sizes of src/kits/interface/Bitmap.cpp (commit b1d497bb69).
//
// Differences to Haiku:
// - Only the color spaces of BColorSpace: no CMAP8 (it needs the system
//   palette), no 48 and 64 bit, no YUV.
// - Buffers are spans; every read and write is checked against the end of
//   its buffer by index. Haiku's copy of equal color spaces measured the
//   lengths from the start of the buffers, not from the offsets, and could
//   read and write past their ends.
// - Each component of a source pixel is converted alone. Haiku's masks let
//   the top bits of the neighbouring component into the low bits of a
//   widened one (B_RGB16 to B_RGB32: red 0xfc for 0xf8 with green set),
//   and red into the one bit of B_RGBA15's alpha.
// - Rows start at their offsets; Haiku advanced B_GRAY1 rows by a padding
//   rounded to bytes, which drifted for offsets inside a byte.
// - B_GRAY1 is written as it is read: a set bit is black, below half
//   brightness. Haiku set the bit for bright pixels and ORed the brightness
//   into the neighbouring bits of the byte.
// - The pixels are read and written in the byte order of the machine, as
//   in Haiku; NewGUI's platforms are little endian.

#include "ColorConversion.h"

#include <algorithm>
#include <cstring>


namespace ngui::detail {


namespace {


// How a color space lies in a pixel, as Haiku's ConvertBits() tables give
// it: as a source, the shifts of the top of each component; as a
// destination, the offsets subtracted from them and the masks.
struct SourceLayout {
	std::int32_t	redShift;
	std::int32_t	greenShift;
	std::int32_t	blueShift;
	std::int32_t	alphaShift;
	std::int32_t	alphaBits;
	std::int32_t	bitsPerPixel;
	bool			swap;
	// NewGUI: the bits of red, green and blue below their shifts
	std::int32_t	redBits = 8;
	std::int32_t	greenBits = 8;
	std::int32_t	blueBits = 8;
};

struct DestLayout {
	std::int32_t	redOffset;
	std::int32_t	greenOffset;
	std::int32_t	blueOffset;
	std::int32_t	alphaOffset;
	std::uint32_t	redMask;
	std::uint32_t	greenMask;
	std::uint32_t	blueMask;
	std::uint32_t	alphaMask;
	std::int32_t	bitsPerPixel;
	bool			swap;
	bool			keepsAlpha;
									// the source's alpha is taken (else opaque)
};


bool
SourceLayoutOf(BColorSpace space, SourceLayout* layout)
{
	switch (space) {
		case BColorSpace::RGBA32:
			*layout = {24, 16, 8, 32, 8, 32, false};
			return true;
		case BColorSpace::RGBA32Big:
			*layout = {16, 24, 32, 8, 8, 32, false};
			return true;
		case BColorSpace::RGB32:
			*layout = {24, 16, 8, 0, 0, 32, false};
			return true;
		case BColorSpace::RGB32Big:
			*layout = {16, 24, 32, 0, 0, 32, false};
			return true;
		case BColorSpace::RGB24:
			*layout = {24, 16, 8, 0, 0, 24, false};
			return true;
		case BColorSpace::RGB24Big:
			*layout = {8, 16, 24, 0, 0, 24, false};
			return true;
		case BColorSpace::RGB16:
		case BColorSpace::RGB16Big:
			*layout = {16, 11, 5, 0, 0, 16, space == BColorSpace::RGB16Big, 5, 6, 5};
			return true;
		case BColorSpace::RGBA15:
		case BColorSpace::RGBA15Big:
			*layout = {15, 10, 5, 16, 1, 16, space == BColorSpace::RGBA15Big, 5, 5, 5};
			return true;
		case BColorSpace::RGB15:
		case BColorSpace::RGB15Big:
			*layout = {15, 10, 5, 0, 0, 16, space == BColorSpace::RGB15Big, 5, 5, 5};
			return true;
		case BColorSpace::Gray8:
			*layout = {8, 8, 8, 0, 0, 8, false};
			return true;
		case BColorSpace::Gray1:
			*layout = {8, 8, 8, 0, 0, 1, false};
			return true;
		case BColorSpace::NoColorSpace:
			break;
	}
	return false;
}


bool
DestLayoutOf(BColorSpace space, DestLayout* layout)
{
	switch (space) {
		case BColorSpace::RGBA32:
			*layout = {24, 16, 8, 32, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 32,
				false, true};
			return true;
		case BColorSpace::RGBA32Big:
			*layout = {16, 24, 32, 8, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff, 32,
				false, true};
			return true;
		// Haiku: "we set the unused alpha to 255 here. This is because BeOS
		// uses the unused alpha for B_OP_ALPHA even though it should not
		// care about it."
		case BColorSpace::RGB32:
			*layout = {24, 16, 8, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000, 32,
				false, false};
			return true;
		case BColorSpace::RGB32Big:
			*layout = {16, 24, 32, 0, 0x0000ff00, 0x00ff0000, 0xff000000, 0x000000ff, 32,
				false, false};
			return true;
		case BColorSpace::RGB24:
			*layout = {24, 16, 8, 0, 0xff0000, 0x00ff00, 0x0000ff, 0x000000, 24, false, false};
			return true;
		case BColorSpace::RGB24Big:
			*layout = {8, 16, 24, 0, 0x0000ff, 0x00ff00, 0xff0000, 0x000000, 24, false, false};
			return true;
		case BColorSpace::RGB16:
		case BColorSpace::RGB16Big:
			*layout = {16, 11, 5, 0, 0xf800, 0x07e0, 0x001f, 0x0000, 16,
				space == BColorSpace::RGB16Big, false};
			return true;
		case BColorSpace::RGBA15:
		case BColorSpace::RGBA15Big:
			*layout = {15, 10, 5, 16, 0x7c00, 0x03e0, 0x001f, 0x8000, 16,
				space == BColorSpace::RGBA15Big, true};
			return true;
		case BColorSpace::RGB15:
		case BColorSpace::RGB15Big:
			*layout = {15, 10, 5, 0, 0x7c00, 0x03e0, 0x001f, 0x0000, 16,
				space == BColorSpace::RGB15Big, false};
			return true;
		case BColorSpace::Gray8:
			*layout = {24, 16, 8, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, 8, false,
				false};
			return true;
		case BColorSpace::Gray1:
			*layout = {24, 16, 8, 0, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000, 1, false,
				false};
			return true;
		case BColorSpace::NoColorSpace:
			break;
	}
	return false;
}


// \a value shifted right by \a shift, left for a negative one (Haiku's
// "if (shift > 0) ... >> shift; else if (shift < 0) ... << -shift").
std::uint32_t
Shift(std::uint32_t value, std::int32_t shift)
{
	if (shift >= 32 || shift <= -32)
		return 0;
	if (shift > 0)
		return value >> shift;
	if (shift < 0)
		return value << -shift;
	return value;
}


// The bits of a component of \a bits below \a shift.
std::uint32_t
ComponentMask(std::int32_t shift, std::int32_t bits)
{
	return ((std::uint32_t(1) << bits) - 1) << (shift - bits);
}


// The brightness of the red, green and blue bytes of \a rgb (Haiku's
// brightness_for(): 0.301 R + 0.586 G + 0.113 B).
std::uint32_t
Brightness(std::uint32_t rgb)
{
	const std::uint32_t red = (rgb >> 16) & 0xff;
	const std::uint32_t green = (rgb >> 8) & 0xff;
	const std::uint32_t blue = rgb & 0xff;
	return (red * 308 + green * 600 + blue * 116) >> 10;
}


// A buffer with a position in bytes and, for one bit pixels, the pixel's
// index in its row.
struct Cursor {
	std::size_t		byte;
};


// Reads the pixel at \a cursor of \a layout (the source functions of Haiku's
// ConvertBits()); false at the end of the buffer.
bool
ReadPixel(std::span<const std::uint8_t> buffer, Cursor& cursor, BColorSpace space,
	std::int32_t index, std::uint32_t* pixel)
{
	switch (space) {
		case BColorSpace::RGB24:
		case BColorSpace::RGB24Big:
			// ReadRGB24()
			if (cursor.byte + 3 > buffer.size())
				return false;
			*pixel = buffer[cursor.byte] | (std::uint32_t(buffer[cursor.byte + 1]) << 8)
				| (std::uint32_t(buffer[cursor.byte + 2]) << 16);
			cursor.byte += 3;
			return true;
		case BColorSpace::Gray8:
			// ReadGray8()
			if (cursor.byte + 1 > buffer.size())
				return false;
			*pixel = buffer[cursor.byte];
			cursor.byte++;
			return true;
		case BColorSpace::Gray1:
		{
			// ReadGray1(): a set bit is black, a clear one white
			if (cursor.byte + 1 > buffer.size())
				return false;
			const std::int32_t shift = 7 - (index % 8);
			*pixel = ((buffer[cursor.byte] >> shift) & 0x01) != 0 ? 0x00 : 0xff;
			if (shift == 0)
				cursor.byte++;
			return true;
		}
		case BColorSpace::RGB16:
		case BColorSpace::RGB16Big:
		case BColorSpace::RGB15:
		case BColorSpace::RGB15Big:
		case BColorSpace::RGBA15:
		case BColorSpace::RGBA15Big:
		{
			if (cursor.byte + 2 > buffer.size())
				return false;
			std::uint16_t value = 0;
			std::memcpy(&value, buffer.data() + cursor.byte, 2);
			*pixel = value;
			cursor.byte += 2;
			return true;
		}
		default:
		{
			if (cursor.byte + 4 > buffer.size())
				return false;
			std::uint32_t value = 0;
			std::memcpy(&value, buffer.data() + cursor.byte, 4);
			*pixel = value;
			cursor.byte += 4;
			return true;
		}
	}
}


// Writes \a pixel at \a cursor (the destination functions of Haiku's
// ConvertBits()); false at the end of the buffer.
bool
WritePixel(std::span<std::uint8_t> buffer, Cursor& cursor, BColorSpace space,
	std::int32_t index, std::uint32_t pixel)
{
	switch (space) {
		case BColorSpace::RGB24:
		case BColorSpace::RGB24Big:
			// WriteRGB24(): the low three bytes, in memory order
			if (cursor.byte + 3 > buffer.size())
				return false;
			buffer[cursor.byte] = static_cast<std::uint8_t>(pixel);
			buffer[cursor.byte + 1] = static_cast<std::uint8_t>(pixel >> 8);
			buffer[cursor.byte + 2] = static_cast<std::uint8_t>(pixel >> 16);
			cursor.byte += 3;
			return true;
		case BColorSpace::Gray8:
			// WriteGray8()
			if (cursor.byte + 1 > buffer.size())
				return false;
			buffer[cursor.byte] = static_cast<std::uint8_t>(Brightness(pixel));
			cursor.byte++;
			return true;
		case BColorSpace::Gray1:
		{
			// WriteGray1(); NewGUI: a set bit for black, as ReadGray1() reads it
			if (cursor.byte + 1 > buffer.size())
				return false;
			const std::int32_t shift = 7 - (index % 8);
			const auto bit = static_cast<std::uint8_t>(0x01 << shift);
			if (Brightness(pixel) < 128)
				buffer[cursor.byte] = static_cast<std::uint8_t>(buffer[cursor.byte] | bit);
			else
				buffer[cursor.byte] = static_cast<std::uint8_t>(buffer[cursor.byte] & ~bit);
			if (shift == 0)
				cursor.byte++;
			return true;
		}
		case BColorSpace::RGB16:
		case BColorSpace::RGB16Big:
		case BColorSpace::RGB15:
		case BColorSpace::RGB15Big:
		case BColorSpace::RGBA15:
		case BColorSpace::RGBA15Big:
		{
			if (cursor.byte + 2 > buffer.size())
				return false;
			const auto value = static_cast<std::uint16_t>(pixel);
			std::memcpy(buffer.data() + cursor.byte, &value, 2);
			cursor.byte += 2;
			return true;
		}
		default:
		{
			if (cursor.byte + 4 > buffer.size())
				return false;
			std::memcpy(buffer.data() + cursor.byte, &pixel, 4);
			cursor.byte += 4;
			return true;
		}
	}
}


} // namespace


std::int32_t
RawBytesPerRow(BColorSpace colorSpace, std::int32_t width)
{
	switch (colorSpace) {
		case BColorSpace::RGB32:
		case BColorSpace::RGBA32:
		case BColorSpace::RGB32Big:
		case BColorSpace::RGBA32Big:
			return 4 * width;
		case BColorSpace::RGB24:
		case BColorSpace::RGB24Big:
			return 3 * width;
		case BColorSpace::RGB16:
		case BColorSpace::RGB15:
		case BColorSpace::RGBA15:
		case BColorSpace::RGB16Big:
		case BColorSpace::RGB15Big:
		case BColorSpace::RGBA15Big:
			return 2 * width;
		case BColorSpace::Gray8:
			return width;
		case BColorSpace::Gray1:
			return (width + 7) / 8;
		case BColorSpace::NoColorSpace:
			break;
	}
	return 0;
}


std::int32_t
BytesPerRow(BColorSpace colorSpace, std::int32_t width)
{
	const std::int32_t bpr = RawBytesPerRow(colorSpace, width);
	// align to int32
	return (bpr + 3) & 0x7ffffffc;
}


std::expected<void, Error>
ConvertBits(std::span<const std::uint8_t> source, std::span<std::uint8_t> dest,
	std::int32_t sourceBpr, std::int32_t destBpr, BColorSpace sourceSpace,
	BColorSpace destSpace, std::int32_t fromX, std::int32_t fromY, std::int32_t toX,
	std::int32_t toY, std::int32_t width, std::int32_t height)
{
	SourceLayout from{};
	DestLayout to{};
	if (width < 0 || height < 0 || sourceBpr < 0 || destBpr < 0
		|| !SourceLayoutOf(sourceSpace, &from) || !DestLayoutOf(destSpace, &to)) {
		return Failure(ErrorCode::InvalidArgument, "bad size or unsupported color space");
	}

	// Advance the buffers to reach their offsets
	std::int64_t srcOffsetX = fromX;
	std::int64_t srcOffsetY = fromY;
	std::int64_t dstOffsetX = toX;
	std::int64_t dstOffsetY = toY;
	std::int64_t rows = height;
	std::int64_t columns = width;
	if (srcOffsetX < 0) {
		dstOffsetX -= srcOffsetX;
		srcOffsetX = 0;
	}
	if (srcOffsetY < 0) {
		dstOffsetY -= srcOffsetY;
		rows += srcOffsetY;
		srcOffsetY = 0;
	}
	if (dstOffsetX < 0) {
		srcOffsetX -= dstOffsetX;
		dstOffsetX = 0;
	}
	if (dstOffsetY < 0) {
		srcOffsetY -= dstOffsetY;
		rows += dstOffsetY;
		dstOffsetY = 0;
	}

	const std::int64_t srcBitsPerRow = std::int64_t(sourceBpr) * 8;
	const std::int64_t dstBitsPerRow = std::int64_t(destBpr) * 8;

	// Ensure that the width fits
	columns = std::min(columns, (srcBitsPerRow - srcOffsetX * from.bitsPerPixel)
		/ from.bitsPerPixel);
	columns = std::min(columns, (dstBitsPerRow - dstOffsetX * to.bitsPerPixel)
		/ to.bitsPerPixel);
	if (columns <= 0 || rows <= 0)
		return {};

	const std::int64_t srcStart = (srcOffsetY * srcBitsPerRow
		+ srcOffsetX * from.bitsPerPixel) >> 3;
	const std::int64_t dstStart = (dstOffsetY * dstBitsPerRow
		+ dstOffsetX * to.bitsPerPixel) >> 3;
	if (srcStart >= static_cast<std::int64_t>(source.size())
		|| dstStart >= static_cast<std::int64_t>(dest.size())) {
		return {};
	}

	// Catch the copy case (NewGUI: within both buffers from the offsets)
	if (sourceSpace == destSpace && from.bitsPerPixel % 8 == 0) {
		const std::int64_t copyCount = (columns * from.bitsPerPixel) >> 3;
		for (std::int64_t row = 0; row < rows; row++) {
			const std::int64_t src = srcStart + row * sourceBpr;
			const std::int64_t dst = dstStart + row * destBpr;
			const std::int64_t count = std::min({copyCount,
				static_cast<std::int64_t>(source.size()) - src,
				static_cast<std::int64_t>(dest.size()) - dst});
			if (count <= 0)
				break;
			std::memcpy(dest.data() + dst, source.data() + src, static_cast<std::size_t>(count));
		}
		return {};
	}

	const std::int32_t redShift = from.redShift - to.redOffset;
	const std::int32_t greenShift = from.greenShift - to.greenOffset;
	const std::int32_t blueShift = from.blueShift - to.blueOffset;
	const std::int32_t alphaShift = from.alphaShift - to.alphaOffset;
	const std::int32_t alphaBits = to.keepsAlpha ? from.alphaBits : 0;
	const std::uint32_t redMask = ComponentMask(from.redShift, from.redBits);
	const std::uint32_t greenMask = ComponentMask(from.greenShift, from.greenBits);
	const std::uint32_t blueMask = ComponentMask(from.blueShift, from.blueBits);
	const std::uint32_t alphaMask = alphaBits > 0
		? ComponentMask(from.alphaShift, from.alphaBits) : 0;

	for (std::int64_t row = 0; row < rows; row++) {
		Cursor src{static_cast<std::size_t>(srcStart + row * sourceBpr)};
		Cursor dst{static_cast<std::size_t>(dstStart + row * destBpr)};
		auto srcIndex = static_cast<std::int32_t>(srcOffsetX);
		auto dstIndex = static_cast<std::int32_t>(dstOffsetX);
		for (std::int64_t column = 0; column < columns; column++) {
			std::uint32_t pixel = 0;
			if (!ReadPixel(source, src, sourceSpace, srcIndex++, &pixel))
				return {};

			// This is valid, as only 16 bit modes will need to swap
			if (from.swap)
				pixel = ((pixel << 8) | (pixel >> 8)) & 0xffff;

			// NewGUI: each component alone; Haiku's masks let the top bits of
			// the neighbouring one into the low bits of a widened component
			std::uint32_t result = (Shift(pixel & redMask, redShift) & to.redMask)
				| (Shift(pixel & greenMask, greenShift) & to.greenMask)
				| (Shift(pixel & blueMask, blueShift) & to.blueMask);
			if (alphaBits > 0) {
				result |= Shift(pixel & alphaMask, alphaShift) & to.alphaMask;
				// if we only had one alpha bit we want it to be 0/255
				if (alphaBits == 1 && (result & to.alphaMask) != 0)
					result |= to.alphaMask;
			} else
				result |= to.alphaMask;

			if (to.swap)
				result = ((result << 8) | (result >> 8)) & 0xffff;

			if (!WritePixel(dest, dst, destSpace, dstIndex++, result))
				return {};
		}
	}
	return {};
}


} // namespace ngui::detail
