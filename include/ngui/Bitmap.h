/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <ngui/Error.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


/*!	The pixel layout of a BBitmap (Haiku's color_space). The names give the
	order of the components from the most significant bit of a pixel; a
	pixel is read as a little endian integer, so B_RGBA32 lies in memory as
	B, G, R, A. The "Big" variants are byte swapped. */
enum class BColorSpace : std::uint8_t {
	NoColorSpace,	/*!< B_NO_COLOR_SPACE: invalid. */
	RGB32,			/*!< B_RGB32: 8 bits each, the fourth byte unused (opaque). */
	RGBA32,			/*!< B_RGBA32: 8 bits each with alpha, not premultiplied. */
	RGB24,			/*!< B_RGB24: three bytes, B, G, R in memory. */
	RGB16,			/*!< B_RGB16: 5, 6 and 5 bits. */
	RGB15,			/*!< B_RGB15: 5 bits each, the top bit unused. */
	RGBA15,			/*!< B_RGBA15: 5 bits each and one bit of alpha. */
	Gray8,			/*!< B_GRAY8: one byte of brightness. */
	Gray1,			/*!< B_GRAY1: one bit, set for black; the leftmost pixel in the
						top bit of a byte. */
	RGB32Big,		/*!< B_RGB32_BIG */
	RGBA32Big,		/*!< B_RGBA32_BIG */
	RGB24Big,		/*!< B_RGB24_BIG: R, G, B in memory. */
	RGB16Big,		/*!< B_RGB16_BIG */
	RGB15Big,		/*!< B_RGB15_BIG */
	RGBA15Big		/*!< B_RGBA15_BIG */
};


/*!	Bytes per row: as the color space needs, padded to four bytes
	(B_ANY_BYTES_PER_ROW). */
inline constexpr int32 kAnyBytesPerRow = -1;


/*!	An image in memory (Haiku's BBitmap with B_BITMAP_NO_SERVER_LINK): pixels
	in one of the color spaces, row after row. Canvas::DrawImage() draws it
	through Image().

	Differences to Haiku: there is no app_server, so a bitmap is never a
	drawing target for views (no AddChild(), Lock(), offscreen views) and
	has no area or overlay; CMAP8, the 48 and 64 bit and the YUV color spaces
	are not supported. The pixels start as zeros. Bounds are half-open: a
	bitmap of {0, 0, 32, 32} has 32 × 32 pixels (Haiku: BRect(0, 0, 31,
	31)); sizes count pixels. ImportBits() returns an Error instead of a
	status_t. Not thread-safe.
*/
class BBitmap {
public:
								BBitmap(gfx::Rect bounds, BColorSpace colorSpace,
									int32 bytesPerRow = kAnyBytesPerRow);
									/*!< A bitmap of the size of \a bounds (rounded up to
										whole pixels); InitCheck() tells whether it could
										be made (a valid, not too large size, a supported
										color space, \a bytesPerRow at least what a row
										needs). */
								BBitmap(const BBitmap& source);
									/*!< A copy of the pixels. */
			BBitmap&			operator=(const BBitmap& source);
									/*!< Becomes a copy of \a source. */
								~BBitmap();
									/*!< Gives its image back to the registry. */

			std::expected<void, Error> InitCheck() const;
									/*!< Whether the bitmap was made; InvalidArgument
										otherwise. */
			bool				IsValid() const;
									/*!< InitCheck() succeeded. */

			std::span<std::uint8_t> Bits();
									/*!< The pixels to change; Image() takes them anew the
										next time. */
			std::span<const std::uint8_t> Bits() const;
									/*!< The pixels. */
			std::size_t			BitsLength() const;
									/*!< Bytes of all rows. */
			int32				BytesPerRow() const;
									/*!< Bytes of one row, with its padding. */
			BColorSpace			ColorSpace() const;
									/*!< The pixel layout. */
			gfx::Rect			Bounds() const;
									/*!< The rectangle it was made with, at whole pixels. */

			std::expected<void, Error> ImportBits(std::span<const std::uint8_t> data,
									int32 bytesPerRow, int32 offset,
									BColorSpace colorSpace);
									/*!< Converts \a data (rows of \a bytesPerRow bytes, or
										kAnyBytesPerRow for padded rows of the bitmap's
										width, in \a colorSpace) into its pixels, starting
										\a offset bytes into them; as far as both reach. */
			std::expected<void, Error> ImportBits(std::span<const std::uint8_t> data,
									int32 bytesPerRow, BColorSpace colorSpace,
									gfx::Point from, gfx::Point to, gfx::Size size);
									/*!< Converts the part of \a data at \a from of \a size
										pixels into its pixels at \a to, clipped to both. */
			std::expected<void, Error> ImportBits(const BBitmap& bitmap);
									/*!< Converts the pixels of \a bitmap, which must have
										the same bounds. */
			std::expected<void, Error> ImportBits(const BBitmap& bitmap, gfx::Point from,
									gfx::Point to, gfx::Size size);
									/*!< Converts the part of \a bitmap at \a from of \a size
										pixels into its pixels at \a to, clipped to both. */

			gfx::ImageHandle	Image() const;
									/*!< NewGUI: the pixels as an image of
										gfx::ImageRegistry::Default(), for
										Canvas::DrawImage() (source rectangle in pixels from
										0, 0); 0 for an invalid bitmap. The bitmap keeps
										the reference; changed pixels give a new image. */

private:
			void				_Changed();

			std::vector<std::uint8_t> fBits;
			gfx::Rect			fBounds;
			int32				fWidth = 0;
			int32				fHeight = 0;
			int32				fBytesPerRow = 0;
			BColorSpace			fColorSpace = BColorSpace::NoColorSpace;
			mutable gfx::ImageHandle fImage = 0;
									// registered from the current pixels, or 0
};


} // namespace ngui
