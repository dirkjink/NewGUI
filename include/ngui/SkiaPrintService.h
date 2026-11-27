/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Bitmap.h>
#include <ngui/Error.h>
#include <ngui/PrintJob.h>
#include <ngui/gfx/PdfDocument.h>


namespace ngui {


class TextSystem;


/*!	The name of the printer of SkiaPrintService that writes PDF files. */
inline constexpr std::string_view kPdfFilePrinter = "PDF";


/*!	How a SkiaPrintService finds its printers. */
struct SkiaPrintOptions {
			bool				systemPrinters = true;
									/*!< Whether it offers the printers of the system
										(CUPS, Windows) besides the PDF printer. */
			std::string			cupsLibrary{};
									/*!< The CUPS library to load; empty for the system's
										(libcups.so.2, on macOS libcups.2.dylib). */
			gfx::PdfOptions		pdf{};	/*!< For the PDF it makes; the title is the job's
											name if empty. */
};


/*!	The print service of ngui_skia (specification 19.4): pages become a PDF
	through gfx::PdfDocument, which goes

	- to a file for the printer kPdfFilePrinter (BPrintSettings::file);
	- on Linux, BSD and macOS to CUPS, loaded with dlopen() when it is
	  needed: printers are CUPS's destinations, a job is the PDF with the
	  option copies;
	- on Windows to GDI: the pages are rastered by Skia at the printer's
	  resolution (at most 300 dpi) and printed with StretchDIBits(); a file
	  in BPrintSettings::file goes to DOCINFO::lpszOutput, so "Microsoft
	  Print to PDF" writes it without asking.

	Without CUPS or printers only the PDF printer is there, and it is then
	the default one. Thread-safe.
*/
class SkiaPrintService final : public BPrintService {
public:
	explicit					SkiaPrintService(const TextSystem* text = nullptr,
									SkiaPrintOptions options = {});
									/*!< A service drawing glyph runs in the fonts of \a text
										(TextSystem::Default() if nullptr). */
								~SkiaPrintService() override;

			std::vector<BPrinterInfo> Printers() const override;
									/*!< The system's printers, then the PDF printer. */
			std::expected<void, Error> Print(std::string_view jobName,
									std::span<const BPrintPage> pages,
									const BPrintSettings& settings) override;
									/*!< InvalidArgument for the PDF printer without a file
										or for a page without a size; NotFound for an
										unknown printer; IoError if CUPS, GDI or the file
										fail. Copies count for the system's printers. */
			int32				Resolution(std::string_view printer) const override;
									/*!< 576 for PDF and CUPS (the grid of the PDF), the
										printer's for GDI. */

			std::expected<BBitmap, Error> RenderPage(const BPrintPage& page, float dpi);
									/*!< \a page rastered at \a dpi pixels per inch on
										white (B_RGBA32), as GDI prints it; also for a
										preview. InvalidArgument for a page without a size
										or a resolution that is not positive, OutOfRange if
										the bitmap would be too large. */

	static	bool				Install(const TextSystem* text = nullptr);
									/*!< Makes a service of the process (with \a text) the
										default one, unless there is one; false then. */

private:
			struct Data;

			std::expected<BBitmap, Error> _RenderPage(const BPrintPage& page, float dpi);

			std::unique_ptr<Data> fData;
};


} // namespace ngui
