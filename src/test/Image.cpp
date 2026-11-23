/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Image.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#include <png.h>


namespace ngui::test {


namespace {


bool
FitsLimits(std::int64_t width, std::int64_t height)
{
	return width > 0 && height > 0 && width <= kMaxImageDimension
		&& height <= kMaxImageDimension && width * height <= kMaxImagePixels;
}


std::size_t
Offset(std::int32_t width, std::int32_t x, std::int32_t y)
{
	return (static_cast<std::size_t>(y) * static_cast<std::size_t>(width)
		+ static_cast<std::size_t>(x)) * 4;
}


// png_image is a C struct whose resources png_image_free() releases.
class PngImage {
public:
	PngImage()
	{
		std::memset(&fImage, 0, sizeof(fImage));
		fImage.version = PNG_IMAGE_VERSION;
	}

	~PngImage()
	{
		png_image_free(&fImage);
	}

	PngImage(const PngImage&) = delete;
	PngImage& operator=(const PngImage&) = delete;

	png_image* operator->()
	{
		return &fImage;
	}

	png_image* Get()
	{
		return &fImage;
	}

	std::string Message() const
	{
		return fImage.message;
	}

private:
	png_image fImage;
};


} // namespace


Image::Image(std::int32_t width, std::int32_t height, Rgba8 fill)
{
	if (!FitsLimits(width, height))
		return;
	fWidth = width;
	fHeight = height;
	fBytes.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
	for (std::size_t i = 0; i < fBytes.size(); i += 4) {
		fBytes[i] = fill.r;
		fBytes[i + 1] = fill.g;
		fBytes[i + 2] = fill.b;
		fBytes[i + 3] = fill.a;
	}
}


Image
Image::FromCpuPixels(const gfx::CpuPixels& pixels)
{
	Image image(pixels.width, pixels.height);
	if (image.IsEmpty() || pixels.stride < static_cast<std::size_t>(pixels.width) * 4
		|| pixels.bytes.size() < pixels.stride * static_cast<std::size_t>(pixels.height - 1)
			+ static_cast<std::size_t>(pixels.width) * 4) {
		return Image();
	}
	const bool bgra = pixels.format == gfx::PixelFormat::Bgra8Premul;
	for (std::int32_t y = 0; y < pixels.height; y++) {
		const std::byte* row = pixels.bytes.data() + static_cast<std::size_t>(y) * pixels.stride;
		for (std::int32_t x = 0; x < pixels.width; x++) {
			const std::byte* p = row + static_cast<std::size_t>(x) * 4;
			const int c0 = static_cast<int>(p[0]);
			const int c1 = static_cast<int>(p[1]);
			const int c2 = static_cast<int>(p[2]);
			const int alpha = static_cast<int>(p[3]);
			const auto unpremultiply = [alpha](int value) {
				if (alpha == 0)
					return std::uint8_t{0};
				const int straight = (value * 255 + alpha / 2) / alpha;
				return static_cast<std::uint8_t>(straight > 255 ? 255 : straight);
			};
			const int red = bgra ? c2 : c0;
			const int blue = bgra ? c0 : c2;
			image.Set(x, y, {unpremultiply(red), unpremultiply(c1), unpremultiply(blue),
				static_cast<std::uint8_t>(alpha)});
		}
	}
	return image;
}


std::int32_t
Image::Width() const
{
	return fWidth;
}


std::int32_t
Image::Height() const
{
	return fHeight;
}


bool
Image::IsEmpty() const
{
	return fBytes.empty();
}


Rgba8
Image::At(std::int32_t x, std::int32_t y) const
{
	if (x < 0 || y < 0 || x >= fWidth || y >= fHeight)
		return {};
	const std::uint8_t* p = fBytes.data() + Offset(fWidth, x, y);
	return {p[0], p[1], p[2], p[3]};
}


void
Image::Set(std::int32_t x, std::int32_t y, Rgba8 pixel)
{
	if (x < 0 || y < 0 || x >= fWidth || y >= fHeight)
		return;
	std::uint8_t* p = fBytes.data() + Offset(fWidth, x, y);
	p[0] = pixel.r;
	p[1] = pixel.g;
	p[2] = pixel.b;
	p[3] = pixel.a;
}


std::span<const std::uint8_t>
Image::Bytes() const
{
	return fBytes;
}


gfx::PixelData
Image::Pixels() const
{
	return {fWidth, fHeight, static_cast<std::size_t>(fWidth) * 4, fBytes};
}


std::expected<Image, Error>
DecodePng(std::span<const std::byte> data)
{
	PngImage png;
	if (data.empty()
		|| png_image_begin_read_from_memory(png.Get(), data.data(), data.size()) == 0) {
		return Failure(ErrorCode::BadData, "no PNG: " + png.Message());
	}
	if (!FitsLimits(png->width, png->height))
		return Failure(ErrorCode::OutOfRange, "PNG too large");

	png->format = PNG_FORMAT_RGBA;
	Image image(static_cast<std::int32_t>(png->width), static_cast<std::int32_t>(png->height));
	std::vector<std::uint8_t> buffer(PNG_IMAGE_SIZE(*png.Get()));
	if (png_image_finish_read(png.Get(), nullptr, buffer.data(), 0, nullptr) == 0)
		return Failure(ErrorCode::BadData, "broken PNG: " + png.Message());
	for (std::int32_t y = 0; y < image.Height(); y++) {
		for (std::int32_t x = 0; x < image.Width(); x++) {
			const std::uint8_t* p = buffer.data() + Offset(image.Width(), x, y);
			image.Set(x, y, {p[0], p[1], p[2], p[3]});
		}
	}
	return image;
}


std::expected<Image, Error>
ReadPng(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return Failure(ErrorCode::IoError, "cannot open " + path.string());
	const std::vector<char> chars((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (file.bad())
		return Failure(ErrorCode::IoError, "cannot read " + path.string());
	return DecodePng(std::as_bytes(std::span(chars)));
}


std::expected<std::vector<std::byte>, Error>
EncodePng(const Image& image)
{
	if (image.IsEmpty())
		return Failure(ErrorCode::InvalidArgument, "empty image");
	PngImage png;
	png->width = static_cast<png_uint_32>(image.Width());
	png->height = static_cast<png_uint_32>(image.Height());
	png->format = PNG_FORMAT_RGBA;
	png_alloc_size_t size = 0;
	if (png_image_write_to_memory(png.Get(), nullptr, &size, 0, image.Bytes().data(), 0,
			nullptr) == 0) {
		return Failure(ErrorCode::InvalidArgument, "cannot encode: " + png.Message());
	}
	std::vector<std::byte> bytes(size);
	if (png_image_write_to_memory(png.Get(), bytes.data(), &size, 0, image.Bytes().data(), 0,
			nullptr) == 0) {
		return Failure(ErrorCode::InvalidArgument, "cannot encode: " + png.Message());
	}
	bytes.resize(size);
	return bytes;
}


std::expected<void, Error>
WritePng(const Image& image, const std::filesystem::path& path)
{
	const std::expected<std::vector<std::byte>, Error> bytes = EncodePng(image);
	if (!bytes.has_value())
		return std::unexpected(bytes.error());
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(reinterpret_cast<const char*>(bytes->data()),
		static_cast<std::streamsize>(bytes->size()));
	if (!file)
		return Failure(ErrorCode::IoError, "cannot write " + path.string());
	return {};
}


} // namespace ngui::test
