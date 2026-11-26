/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The print systems of the platforms behind SkiaPrintService: CUPS (Linux,
// BSD, macOS) takes the PDF; GDI (Windows) takes pages rastered by Skia.


#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/PrintJob.h>


namespace ngui::print {


/*!	A page in pixels: rows of B, G, R, A from the top, opaque. */
struct RasterPage {
			int32				width = 0;
			int32				height = 0;
			std::vector<std::uint8_t> bgra;
};


/*!	Rasters a page at a resolution in dots per inch. */
using PageRasterizer = std::function<std::expected<RasterPage, Error>(const BPrintPage& page,
	float dpi)>;


/*!	The destinations of CUPS loaded from \a library (the system's if empty);
	none without it. */
std::vector<BPrinterInfo> CupsPrinters(const std::string& library);

/*!	Sends \a pdf to the CUPS destination \a printer as the job \a title, in
	\a copies copies. NotSupported without the library, IoError if CUPS
	refuses it. */
std::expected<void, Error> CupsPrint(const std::string& library, std::string_view printer,
	std::string_view title, std::span<const std::byte> pdf, int32 copies);


/*!	The printers of Windows; none elsewhere. */
std::vector<BPrinterInfo> GdiPrinters();

/*!	Prints \a pages on the Windows printer \a printer through GDI, each
	rastered by \a raster at the printer's resolution (at most 300 dpi);
	NotSupported elsewhere. */
std::expected<void, Error> GdiPrint(std::string_view printer, std::string_view title,
	std::span<const BPrintPage> pages, const BPrintSettings& settings,
	const PageRasterizer& raster);

/*!	The dots per inch of the Windows printer \a printer; 300 if unknown. */
int32 GdiResolution(std::string_view printer);


} // namespace ngui::print
