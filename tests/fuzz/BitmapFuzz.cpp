/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <ngui/Bitmap.h>


namespace {


using namespace ngui;


// The bytes of a fuzz input, taken one after the other; zeros at the end.
class Bytes {
public:
	explicit Bytes(std::span<const std::uint8_t> data)
		:
		fData(data)
	{
	}

	bool AtEnd() const
	{
		return fNext >= fData.size();
	}

	std::uint8_t Next()
	{
		return fNext < fData.size() ? fData[fNext++] : 0;
	}

	// A signed small value, -32 .. 95.
	float Coordinate()
	{
		return static_cast<float>(static_cast<int>(Next() % 128) - 32);
	}

private:
	std::span<const std::uint8_t> fData;
	std::size_t fNext = 0;
};


BColorSpace
SpaceFrom(std::uint8_t byte)
{
	// all of them, NoColorSpace included
	return static_cast<BColorSpace>(byte % 15);
}


} // namespace


NGUI_FUZZ_TARGET(Bitmap)
{
	// What is tested: A bitmap of 1 to 64 × 1 to 32 pixels in any color
	// space, with rows of any length the bytes give, filled with a pattern;
	// then imports: from raw data of any length, row length, offset and color
	// space, and parts of a second bitmap at any place, of any size, also
	// outside both.
	// Success criteria: No crash or sanitizer report (nothing is read or
	// written outside the buffers). An import with a valid color space and
	// row length succeeds; one with an invalid one fails. A part import
	// changes only the rows it reaches: rows above and below the target
	// rectangle keep their bytes. Image() gives a handle for every valid
	// bitmap.
	// Why it matters: ImportBits() takes sizes, offsets and lengths from
	// applications and files; Haiku's version could write past its buffer.
	//
	Bytes bytes(data);
	const float width = static_cast<float>(1 + bytes.Next() % 64);
	const float height = static_cast<float>(1 + bytes.Next() % 32);
	const BColorSpace space = SpaceFrom(bytes.Next());
	const std::uint8_t rowByte = bytes.Next();
	const int32 bytesPerRow = rowByte < 128 ? kAnyBytesPerRow : rowByte % 200;
	BBitmap bitmap({0, 0, width, height}, space, bytesPerRow);
	if (!bitmap.IsValid())
		return;
	std::uint8_t pattern = 0;
	for (std::uint8_t& byte : bitmap.Bits())
		byte = pattern++;

	while (!bytes.AtEnd()) {
		const std::uint8_t operation = bytes.Next();
		if (operation % 2 == 0) {
			// raw data
			std::vector<std::uint8_t> raw(static_cast<std::size_t>(bytes.Next()) * 4);
			for (std::uint8_t& byte : raw)
				byte = pattern++;
			const std::uint8_t rawRow = bytes.Next();
			const int32 rawBpr = rawRow == 0 ? kAnyBytesPerRow : rawRow - 64;
			const auto offset = static_cast<int32>(bytes.Next()) * 4 - 8;
			const BColorSpace rawSpace = SpaceFrom(bytes.Next());
			const auto result = bitmap.ImportBits(raw, rawBpr, offset, rawSpace);
			const bool valid = rawSpace != BColorSpace::NoColorSpace
				&& (rawBpr > 0 || rawBpr == kAnyBytesPerRow) && offset >= 0
				&& static_cast<std::size_t>(offset) <= bitmap.BitsLength();
			NGUI_FUZZ_CHECK(result.has_value() == valid);
		} else {
			// a part of another bitmap
			const BColorSpace otherSpace = SpaceFrom(bytes.Next());
			BBitmap other({0, 0, static_cast<float>(1 + bytes.Next() % 48),
				static_cast<float>(1 + bytes.Next() % 24)}, otherSpace);
			for (std::uint8_t& byte : other.Bits())
				byte = pattern++;
			const float fromX = bytes.Coordinate();
			const float fromY = bytes.Coordinate();
			const float toX = bytes.Coordinate();
			const float toY = bytes.Coordinate();
			const float sizeW = static_cast<float>(bytes.Next() % 80);
			const float sizeH = static_cast<float>(bytes.Next() % 40);
			const std::vector<std::uint8_t> before(bitmap.Bits().begin(), bitmap.Bits().end());
			const auto result = bitmap.ImportBits(other, {fromX, fromY}, {toX, toY},
				{sizeW, sizeH});
			NGUI_FUZZ_CHECK(result.has_value() == other.IsValid());
			// the rows the target rectangle (moved by a source outside) cannot
			// reach keep their bytes
			const float top = toY - std::min(fromY, 0.0f);
			const float bottom = toY + sizeH;
			const std::span<const std::uint8_t> after = bitmap.Bits();
			for (int32 row = 0; row < static_cast<int32>(height); row++) {
				if (static_cast<float>(row) >= top && static_cast<float>(row) < bottom)
					continue;
				const auto start = static_cast<std::size_t>(row * bitmap.BytesPerRow());
				NGUI_FUZZ_CHECK(std::equal(after.begin() + static_cast<std::ptrdiff_t>(start),
					after.begin() + static_cast<std::ptrdiff_t>(start
						+ static_cast<std::size_t>(bitmap.BytesPerRow())),
					before.begin() + static_cast<std::ptrdiff_t>(start)));
			}
		}
	}
	NGUI_FUZZ_CHECK(bitmap.Image() != 0);
}
