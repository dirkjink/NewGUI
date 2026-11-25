/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "PngWriter.h"

#include <algorithm>
#include <array>
#include <string_view>


namespace ngui::gfx::detail {


namespace {


// CRC-32 of PNG chunks (ISO 3309, polynomial 0xedb88320).
std::uint32_t
Crc32(std::span<const std::uint8_t> bytes, std::uint32_t crc = 0)
{
	static const std::array<std::uint32_t, 256> kTable = [] {
		std::array<std::uint32_t, 256> table{};
		for (std::uint32_t n = 0; n < 256; n++) {
			std::uint32_t c = n;
			for (int k = 0; k < 8; k++)
				c = (c & 1) != 0 ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		return table;
	}();
	crc = ~crc;
	for (const std::uint8_t byte : bytes)
		crc = kTable[(crc ^ byte) & 0xff] ^ (crc >> 8);
	return ~crc;
}


void
PutBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value)
{
	out.push_back(static_cast<std::uint8_t>(value >> 24));
	out.push_back(static_cast<std::uint8_t>(value >> 16));
	out.push_back(static_cast<std::uint8_t>(value >> 8));
	out.push_back(static_cast<std::uint8_t>(value));
}


void
PutChunk(std::vector<std::uint8_t>& out, std::string_view type,
	std::span<const std::uint8_t> data)
{
	PutBigEndian(out, static_cast<std::uint32_t>(data.size()));
	const std::size_t typeAt = out.size();
	for (const char c : type)
		out.push_back(static_cast<std::uint8_t>(c));
	out.insert(out.end(), data.begin(), data.end());
	PutBigEndian(out, Crc32(std::span(out).subspan(typeAt)));
}


} // namespace


std::vector<std::uint8_t>
EncodeStoredPng(std::uint32_t width, std::uint32_t height, std::span<const std::uint8_t> rgba)
{
	// the filtered rows: filter type 0 (none), then the pixels
	const std::size_t rowBytes = static_cast<std::size_t>(width) * 4;
	std::vector<std::uint8_t> raw;
	raw.reserve((rowBytes + 1) * height);
	for (std::uint32_t y = 0; y < height; y++) {
		raw.push_back(0);
		const auto row = rgba.subspan(y * rowBytes, rowBytes);
		raw.insert(raw.end(), row.begin(), row.end());
	}

	// zlib: header, stored blocks of at most 65535 bytes, Adler-32
	std::vector<std::uint8_t> zlib = {0x78, 0x01};
	std::size_t at = 0;
	do {
		const std::size_t length = std::min<std::size_t>(raw.size() - at, 65535);
		const bool last = at + length == raw.size();
		zlib.push_back(static_cast<std::uint8_t>(last ? 1 : 0));
		zlib.push_back(static_cast<std::uint8_t>(length));
		zlib.push_back(static_cast<std::uint8_t>(length >> 8));
		zlib.push_back(static_cast<std::uint8_t>(~length));
		zlib.push_back(static_cast<std::uint8_t>(~length >> 8));
		zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(at),
			raw.begin() + static_cast<std::ptrdiff_t>(at + length));
		at += length;
	} while (at < raw.size());
	std::uint32_t a = 1;
	std::uint32_t b = 0;
	for (const std::uint8_t byte : raw) {
		a = (a + byte) % 65521;
		b = (b + a) % 65521;
	}
	PutBigEndian(zlib, (b << 16) | a);

	std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
	std::vector<std::uint8_t> header;
	PutBigEndian(header, width);
	PutBigEndian(header, height);
	// 8 bits per channel, RGBA, deflate, adaptive filtering, no interlace
	header.insert(header.end(), {8, 6, 0, 0, 0});
	PutChunk(png, "IHDR", header);
	PutChunk(png, "IDAT", zlib);
	PutChunk(png, "IEND", {});
	return png;
}


std::string
Base64(std::span<const std::uint8_t> bytes)
{
	static constexpr std::string_view kDigits =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string text;
	text.reserve((bytes.size() + 2) / 3 * 4);
	for (std::size_t i = 0; i < bytes.size(); i += 3) {
		const std::uint32_t remaining = static_cast<std::uint32_t>(bytes.size() - i);
		std::uint32_t group = static_cast<std::uint32_t>(bytes[i]) << 16;
		if (remaining > 1)
			group |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
		if (remaining > 2)
			group |= bytes[i + 2];
		text += kDigits[(group >> 18) & 63];
		text += kDigits[(group >> 12) & 63];
		text += remaining > 1 ? kDigits[(group >> 6) & 63] : '=';
		text += remaining > 2 ? kDigits[group & 63] : '=';
	}
	return text;
}


} // namespace ngui::gfx::detail
