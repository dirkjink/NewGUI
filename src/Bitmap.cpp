/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/interface/Bitmap.cpp (commit b1d497bb69), the
// part of a bitmap without app_server link (B_BITMAP_NO_SERVER_LINK).
//
// Differences to Haiku: see Bitmap.h. Haiku's IntegerWidth() + 1 is the
// width rounded up of the half-open bounds; ImportBits() with a size takes
// pixel counts (Haiku: BSize, one less).

#include <ngui/Bitmap.h>

#include <climits>
#include <cmath>
#include <utility>
#include <vector>

#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/ImageRegistry.h>

#include "ColorConversion.h"


namespace ngui {


BBitmap::BBitmap(gfx::Rect bounds, BColorSpace colorSpace, int32 bytesPerRow)
{
	// check params
	const float widthF = std::ceil(bounds.x1 - bounds.x0);
	const float heightF = std::ceil(bounds.y1 - bounds.y0);
	if (!(widthF >= 1) || !(heightF >= 1)
		|| detail::RawBytesPerRow(colorSpace, 1) == 0) {
		return;
	}
	// bounds is in floats and might be valid but much larger than what we
	// can handle the size could not be expressed in int32
	if (static_cast<double>(widthF) * static_cast<double>(heightF) > double(INT_MAX / 4))
		return;

	const auto width = static_cast<int32>(widthF);
	const auto height = static_cast<int32>(heightF);
	const int32 bpr = detail::BytesPerRow(colorSpace, width);
	if (bytesPerRow < 0)
		bytesPerRow = bpr;
	else if (bytesPerRow < detail::RawBytesPerRow(colorSpace, width))
		return;
	// NewGUI: rows of any length a caller asks for, but no more than the
	// whole bitmap may hold
	if (static_cast<double>(bytesPerRow) * height > double(INT_MAX))
		return;

	fBits.assign(static_cast<std::size_t>(bytesPerRow) * static_cast<std::size_t>(height), 0);
	fBounds = {bounds.x0, bounds.y0, bounds.x0 + widthF, bounds.y0 + heightF};
	fWidth = width;
	fHeight = height;
	fBytesPerRow = bytesPerRow;
	fColorSpace = colorSpace;
}


BBitmap::BBitmap(const BBitmap& source)
	:
	fBits(source.fBits),
	fBounds(source.fBounds),
	fWidth(source.fWidth),
	fHeight(source.fHeight),
	fBytesPerRow(source.fBytesPerRow),
	fColorSpace(source.fColorSpace)
{
}


BBitmap&
BBitmap::operator=(const BBitmap& source)
{
	if (this == &source)
		return *this;
	_Changed();
	fBits = source.fBits;
	fBounds = source.fBounds;
	fWidth = source.fWidth;
	fHeight = source.fHeight;
	fBytesPerRow = source.fBytesPerRow;
	fColorSpace = source.fColorSpace;
	return *this;
}


BBitmap::~BBitmap()
{
	_Changed();
}


std::expected<void, Error>
BBitmap::InitCheck() const
{
	if (fColorSpace == BColorSpace::NoColorSpace)
		return Failure(ErrorCode::InvalidArgument, "bad bounds, color space or bytes per row");
	return {};
}


bool
BBitmap::IsValid() const
{
	return InitCheck().has_value();
}


std::span<std::uint8_t>
BBitmap::Bits()
{
	_Changed();
	return fBits;
}


std::span<const std::uint8_t>
BBitmap::Bits() const
{
	return fBits;
}


std::size_t
BBitmap::BitsLength() const
{
	return fBits.size();
}


int32
BBitmap::BytesPerRow() const
{
	return fBytesPerRow;
}


BColorSpace
BBitmap::ColorSpace() const
{
	return fColorSpace;
}


gfx::Rect
BBitmap::Bounds() const
{
	return fBounds;
}


std::expected<void, Error>
BBitmap::ImportBits(std::span<const std::uint8_t> data, int32 bytesPerRow, int32 offset,
	BColorSpace colorSpace)
{
	if (!IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid bitmap");
	if (offset < 0 || static_cast<std::size_t>(offset) > fBits.size())
		return Failure(ErrorCode::InvalidArgument, "bad offset");

	if (bytesPerRow <= 0) {
		if (bytesPerRow == kAnyBytesPerRow)
			bytesPerRow = detail::BytesPerRow(colorSpace, fWidth);
		else
			return Failure(ErrorCode::InvalidArgument, "bad bytes per row");
	}

	_Changed();
	return detail::ConvertBits(data, std::span<std::uint8_t>(fBits).subspan(
		static_cast<std::size_t>(offset)), bytesPerRow, fBytesPerRow, colorSpace, fColorSpace,
		0, 0, 0, 0, fWidth, fHeight);
}


std::expected<void, Error>
BBitmap::ImportBits(std::span<const std::uint8_t> data, int32 bytesPerRow,
	BColorSpace colorSpace, gfx::Point from, gfx::Point to, gfx::Size size)
{
	if (!IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid bitmap");
	if (!(size.w >= 0) || !(size.h >= 0) || size.w > float(INT_MAX) || size.h > float(INT_MAX))
		return Failure(ErrorCode::InvalidArgument, "bad size");

	if (bytesPerRow <= 0) {
		if (bytesPerRow == kAnyBytesPerRow)
			bytesPerRow = detail::BytesPerRow(colorSpace, fWidth);
		else
			return Failure(ErrorCode::InvalidArgument, "bad bytes per row");
	}

	// Haiku's (int32) casts of the points
	const auto point = [](float value) {
		if (!(value > float(INT_MIN)) || !(value < float(INT_MAX)))
			return value > 0 ? INT_MAX : INT_MIN;
		return static_cast<int32>(value);
	};
	_Changed();
	return detail::ConvertBits(data, fBits, bytesPerRow, fBytesPerRow, colorSpace, fColorSpace,
		point(from.x), point(from.y), point(to.x), point(to.y), static_cast<int32>(size.w),
		static_cast<int32>(size.h));
}


std::expected<void, Error>
BBitmap::ImportBits(const BBitmap& bitmap)
{
	if (!IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid bitmap");
	if (!bitmap.IsValid() || bitmap.Bounds() != fBounds)
		return Failure(ErrorCode::InvalidArgument, "other bounds");

	return ImportBits(bitmap.Bits(), bitmap.BytesPerRow(), 0, bitmap.ColorSpace());
}


std::expected<void, Error>
BBitmap::ImportBits(const BBitmap& bitmap, gfx::Point from, gfx::Point to, gfx::Size size)
{
	if (!IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid bitmap");
	if (!bitmap.IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid source");

	return ImportBits(bitmap.Bits(), bitmap.BytesPerRow(), bitmap.ColorSpace(), from, to,
		size);
}


gfx::ImageHandle
BBitmap::Image() const
{
	if (fImage != 0 || !IsValid())
		return fImage;

	// The registry takes RGBA bytes, not premultiplied (B_RGBA32 with red and
	// blue swapped).
	const std::int32_t stride = fWidth * 4;
	std::vector<std::uint8_t> rgba(static_cast<std::size_t>(stride) * fHeight, 0);
	if (!detail::ConvertBits(fBits, rgba, fBytesPerRow, stride, fColorSpace,
			BColorSpace::RGBA32, 0, 0, 0, 0, fWidth, fHeight).has_value()) {
		return 0;
	}
	for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
		std::swap(rgba[i], rgba[i + 2]);
	const gfx::PixelData pixels{fWidth, fHeight, static_cast<std::size_t>(stride), rgba};
	if (auto image = gfx::ImageRegistry::Default().Register(pixels))
		fImage = *image;
	return fImage;
}


void
BBitmap::_Changed()
{
	if (fImage != 0) {
		gfx::ImageRegistry::Default().Release(fImage);
		fImage = 0;
	}
}


std::expected<std::vector<std::byte>, Error>
EncodePng(const BBitmap& bitmap, std::string_view backendName)
{
	if (!bitmap.IsValid())
		return Failure(ErrorCode::InvalidArgument, "an invalid bitmap");
	// as B_RGBA32 (B, G, R, A in memory), then R and B swapped: the RGBA of
	// PixelData
	const gfx::Rect bounds = bitmap.Bounds();
	const auto width = static_cast<std::int32_t>(bounds.Width());
	const auto height = static_cast<std::int32_t>(bounds.Height());
	const std::size_t stride = static_cast<std::size_t>(width) * 4;
	std::vector<std::uint8_t> rgba(stride * static_cast<std::size_t>(height));
	if (auto converted = detail::ConvertBits(bitmap.Bits(), rgba, bitmap.BytesPerRow(),
			static_cast<std::int32_t>(stride), bitmap.ColorSpace(), BColorSpace::RGBA32, 0, 0,
			0, 0, width, height); !converted.has_value()) {
		return std::unexpected(converted.error());
	}
	for (std::size_t i = 0; i + 3 < rgba.size(); i += 4)
		std::swap(rgba[i], rgba[i + 2]);

	auto backend = backendName.empty() ? gfx::BackendRegistry::Default().CreatePreferred({})
		: gfx::BackendRegistry::Default().Create(backendName);
	if (!backend.has_value())
		return std::unexpected(backend.error());
	return (*backend)->EncodePng({width, height, stride, rgba});
}


} // namespace ngui
