/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// PDFs read with poppler's tools, where the build found them
// (NGUI_TEST_PDFTOPPM, NGUI_TEST_PDFTOTEXT; the Linux CI installs
// poppler-utils). The files go to GoogleTest's temporary directory, named
// after the running test.


#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <utility>

#include <gtest/gtest.h>

#include <ngui/test/Image.h>


namespace ngui::test {


// Page \a page (from 1) of the PDF \a bytes rastered by pdftoppm at \a dpi;
// an empty image without pdftoppm or if it fails.
inline Image
Raster(std::span<const std::byte> bytes, int dpi, int page = 1)
{
#if defined(NGUI_TEST_PDFTOPPM)
	const std::filesystem::path directory = testing::TempDir();
	const std::string name = testing::UnitTest::GetInstance()->current_test_info()->name();
	const std::filesystem::path pdf = directory / (name + ".pdf");
	const std::filesystem::path png = directory / (name + "-raster");
	{
		std::ofstream out(pdf, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	}
	const std::string command = std::string("\"") + NGUI_TEST_PDFTOPPM + "\" -png -singlefile -r "
		+ std::to_string(dpi) + " -f " + std::to_string(page) + " -l " + std::to_string(page)
		+ " \"" + pdf.string() + "\" \"" + png.string() + "\"";
	if (std::system(command.c_str()) != 0)
		return {};
	auto image = ReadPng(png.string() + ".png");
	return image.has_value() ? std::move(*image) : Image{};
#else
	(void)bytes;
	(void)dpi;
	(void)page;
	return {};
#endif
}


// The text of the PDF \a bytes as pdftotext reads it; empty without it.
inline std::string
TextOf(std::span<const std::byte> bytes)
{
#if defined(NGUI_TEST_PDFTOTEXT)
	const std::filesystem::path directory = testing::TempDir();
	const std::string name = testing::UnitTest::GetInstance()->current_test_info()->name();
	const std::filesystem::path pdf = directory / (name + "-text.pdf");
	const std::filesystem::path text = directory / (name + ".txt");
	{
		std::ofstream out(pdf, std::ios::binary);
		out.write(reinterpret_cast<const char*>(bytes.data()),
			static_cast<std::streamsize>(bytes.size()));
	}
	const std::string command = std::string("\"") + NGUI_TEST_PDFTOTEXT + "\" \""
		+ pdf.string() + "\" \"" + text.string() + "\"";
	if (std::system(command.c_str()) != 0)
		return {};
	std::ifstream in(text, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
#else
	(void)bytes;
	return {};
#endif
}


inline bool
CanRaster()
{
#if defined(NGUI_TEST_PDFTOPPM)
	return true;
#else
	return false;
#endif
}


inline bool
CanReadText()
{
#if defined(NGUI_TEST_PDFTOTEXT)
	return true;
#else
	return false;
#endif
}


} // namespace ngui::test
