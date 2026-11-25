/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include <ngui/Bitmap.h>
#include <ngui/Error.h>
#include <ngui/gfx/Backend.h>


namespace ngui::test {


/*!	One pixel: RGBA, 8 bits per component, not premultiplied. */
struct Rgba8 {
			std::uint8_t		r = 0;	/*!< Red. */
			std::uint8_t		g = 0;	/*!< Green. */
			std::uint8_t		b = 0;	/*!< Blue. */
			std::uint8_t		a = 0;	/*!< Alpha, 255 is opaque. */

			constexpr bool		operator==(const Rgba8& other) const = default;
									/*!< All four components equal. */
};


/*!	The largest width or height an Image may have, in pixels. */
inline constexpr std::int32_t kMaxImageDimension = 16384;

/*!	The largest number of pixels an Image may have (16 M, 64 MB). */
inline constexpr std::int64_t kMaxImagePixels = std::int64_t{1} << 24;


/*!	A picture for the test kit (specification 20.5, 21): RGBA8, not
	premultiplied, rows from top to bottom without padding, as a PNG holds
	it. Rendered results, references and difference images are Images.
*/
class Image {
public:
								Image() = default;
									/*!< An empty image, 0 × 0. */
								Image(std::int32_t width, std::int32_t height,
									Rgba8 fill = {});
									/*!< \a width × \a height pixels of \a fill; empty if
										a dimension is not positive or the size exceeds
										the limits. */

	static	Image				FromCpuPixels(const gfx::CpuPixels& pixels);
									/*!< A copy of the pixels of a CPU surface, converted to
										RGBA and unpremultiplied. */
	static	Image				FromBitmap(const BBitmap& bitmap);
									/*!< A copy of \a bitmap's pixels in RGBA, e.g. of a
										BWindow::Snapshot(); empty for an invalid bitmap. */

			std::int32_t		Width() const;
									/*!< Width in pixels. */
			std::int32_t		Height() const;
									/*!< Height in pixels. */
			bool				IsEmpty() const;
									/*!< No pixels. */

			Rgba8				At(std::int32_t x, std::int32_t y) const;
									/*!< The pixel at (\a x, \a y); transparent black
										outside. */
			void				Set(std::int32_t x, std::int32_t y, Rgba8 pixel);
									/*!< Sets the pixel at (\a x, \a y); ignored outside. */

			std::span<const std::uint8_t> Bytes() const;
									/*!< Width × height × 4 bytes. */
			gfx::PixelData		Pixels() const;
									/*!< The bytes as PixelData, e.g. for
										ImageRegistry::Register(). */

			bool				operator==(const Image& other) const = default;
									/*!< Same size and same bytes. */

private:
			std::int32_t		fWidth = 0;
			std::int32_t		fHeight = 0;
			std::vector<std::uint8_t> fBytes;
};


/*!	Decodes a PNG of any color type and depth to RGBA8. Fails with BadData if
	\a data is no PNG, and with OutOfRange if the image exceeds
	kMaxImageDimension or kMaxImagePixels. */
std::expected<Image, Error>		DecodePng(std::span<const std::byte> data);

/*!	Reads and decodes the PNG file at \a path; IoError if it cannot be read. */
std::expected<Image, Error>		ReadPng(const std::filesystem::path& path);

/*!	Encodes \a image as an 8-bit RGBA PNG. The same image gives the same
	bytes with the same libpng and zlib. */
std::expected<std::vector<std::byte>, Error> EncodePng(const Image& image);

/*!	Encodes \a image and writes it to \a path, replacing the file. */
std::expected<void, Error>		WritePng(const Image& image, const std::filesystem::path& path);


} // namespace ngui::test
