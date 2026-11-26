/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/SkiaPrintService.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <ngui/TextSystem.h>
#include <ngui/gfx/SkiaBackend.h>

#include "SystemPrinters.h"


namespace ngui {


namespace {


// The printers of the platform's print system.
std::vector<BPrinterInfo>
SystemPrinters(const SkiaPrintOptions& options)
{
	if (!options.systemPrinters)
		return {};
#if defined(_WIN32)
	return print::GdiPrinters();
#else
	return print::CupsPrinters(options.cupsLibrary);
#endif
}


bool
ValidPage(const BPrintPage& page)
{
	return std::isfinite(page.size.w) && std::isfinite(page.size.h) && page.size.w > 0
		&& page.size.h > 0 && page.size.w <= gfx::kPdfMaxPageSize
		&& page.size.h <= gfx::kPdfMaxPageSize;
}


} // namespace


struct SkiaPrintService::Data {
			const TextSystem*	text;
			SkiaPrintOptions	options;

			std::mutex			lock;
			gfx::SkiaBackend	backend;
			std::set<gfx::FontId> fonts;

			// The fonts the text system has loaded by now; under lock.
			void				AddFonts();
};


void
SkiaPrintService::Data::AddFonts()
{
	const TextSystem& system = text != nullptr ? *text : TextSystem::Default();
	for (const FontFile& font : system.Fonts()) {
		if (fonts.insert(font.id).second)
			(void)backend.AddFont(font.id, font.data, font.faceIndex);
	}
}


SkiaPrintService::SkiaPrintService(const TextSystem* text, SkiaPrintOptions options)
	:
	fData(std::make_unique<Data>())
{
	fData->text = text;
	fData->options = std::move(options);
}


SkiaPrintService::~SkiaPrintService() = default;


std::vector<BPrinterInfo>
SkiaPrintService::Printers() const
{
	std::vector<BPrinterInfo> printers = SystemPrinters(fData->options);
	const bool hasDefault = std::ranges::any_of(printers,
		[](const BPrinterInfo& printer) { return printer.isDefault; });
	printers.push_back({std::string(kPdfFilePrinter), "PDF file", !hasDefault, true});
	return printers;
}


std::expected<void, Error>
SkiaPrintService::Print(std::string_view jobName, std::span<const BPrintPage> pages,
	const BPrintSettings& settings)
{
	if (pages.empty())
		return Failure(ErrorCode::InvalidArgument, "no pages");
	if (!std::ranges::all_of(pages, ValidPage))
		return Failure(ErrorCode::InvalidArgument, "a page without a size PDF can hold");
	const std::vector<BPrinterInfo> printers = Printers();
	const auto printer = std::ranges::find_if(printers, [&](const BPrinterInfo& info) {
		return settings.printer.empty() ? info.isDefault : info.name == settings.printer;
	});
	if (printer == printers.end())
		return Failure(ErrorCode::NotFound, "no printer " + settings.printer);
	if (printer->writesFile && settings.file.empty())
		return Failure(ErrorCode::InvalidArgument, "the PDF printer needs a file");

	const std::scoped_lock lock(fData->lock);
	fData->AddFonts();

#if defined(_WIN32)
	if (!printer->writesFile) {
		// GDI: each page rastered by Skia
		const auto raster = [this](const BPrintPage& page, float dpi)
			-> std::expected<print::RasterPage, Error> {
			const std::expected<BBitmap, Error> bitmap = _RenderPage(page, dpi);
			if (!bitmap.has_value())
				return std::unexpected(bitmap.error());
			const auto width = static_cast<std::size_t>(bitmap->Bounds().Width());
			const auto height = static_cast<std::size_t>(bitmap->Bounds().Height());
			print::RasterPage pixels{static_cast<int32>(width), static_cast<int32>(height),
				std::vector<std::uint8_t>(width * height * 4)};
			const auto bytesPerRow = static_cast<std::size_t>(bitmap->BytesPerRow());
			for (std::size_t y = 0; y < height; y++) {
				std::copy_n(bitmap->Bits().data() + y * bytesPerRow, width * 4,
					pixels.bgra.data() + y * width * 4);
			}
			return pixels;
		};
		return print::GdiPrint(printer->name, jobName, pages, settings, raster);
	}
#endif

	gfx::PdfOptions pdf = fData->options.pdf;
	if (pdf.title.empty())
		pdf.title = std::string(jobName);
	std::ostringstream memory(std::ios::binary);
	std::ofstream file;
	std::ostream* out = &memory;
	if (printer->writesFile) {
		file.open(settings.file, std::ios::binary | std::ios::trunc);
		if (!file)
			return Failure(ErrorCode::IoError, "cannot write " + settings.file);
		out = &file;
	}
	{
		gfx::PdfDocument document(fData->backend, *out, pdf);
		for (const BPrintPage& page : pages) {
			if (auto begun = document.BeginPage(page.size); !begun.has_value())
				return begun;
			for (const BPrintPage::Placement& placement : page.placements) {
				if (placement.list == nullptr)
					continue;
				auto drawn = document.Draw(*placement.list, placement.area, placement.at,
					placement.pointsPerPixel);
				if (!drawn.has_value())
					return drawn;
			}
		}
		if (auto closed = document.Close(); !closed.has_value())
			return closed;
	}
	if (printer->writesFile)
		return {};
	const std::string bytes = std::move(memory).str();
	return print::CupsPrint(fData->options.cupsLibrary, printer->name, jobName,
		std::as_bytes(std::span(bytes)), settings.copies);
}


int32
SkiaPrintService::Resolution(std::string_view printer) const
{
#if defined(_WIN32)
	if (printer != kPdfFilePrinter)
		return print::GdiResolution(printer);
#else
	(void)printer;
#endif
	return static_cast<int32>(std::lround(fData->options.pdf.resolution > 0
		? fData->options.pdf.resolution : 576));
}


std::expected<BBitmap, Error>
SkiaPrintService::RenderPage(const BPrintPage& page, float dpi)
{
	const std::scoped_lock lock(fData->lock);
	fData->AddFonts();
	return _RenderPage(page, dpi);
}


// RenderPage() under the lock.
std::expected<BBitmap, Error>
SkiaPrintService::_RenderPage(const BPrintPage& page, float dpi)
{
	if (!ValidPage(page) || !std::isfinite(dpi) || dpi <= 0)
		return Failure(ErrorCode::InvalidArgument, "no page or resolution");
	const float width = std::round(page.size.w / 72 * dpi);
	const float height = std::round(page.size.h / 72 * dpi);
	if (width < 1 || height < 1 || width > 16384 || height > 16384)
		return Failure(ErrorCode::OutOfRange, "the page is too large at this resolution");
	BBitmap bitmap({0, 0, width, height}, BColorSpace::RGBA32);
	if (!bitmap.IsValid())
		return Failure(ErrorCode::OutOfRange, "the page is too large at this resolution");
	// B, G, R, A in memory; opaque, so premultiplied or not is the same
	const SkImageInfo info = SkImageInfo::Make(static_cast<int>(width),
		static_cast<int>(height), kBGRA_8888_SkColorType, kPremul_SkAlphaType);
	const sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(info, bitmap.Bits().data(),
		static_cast<std::size_t>(bitmap.BytesPerRow()));
	if (surface == nullptr)
		return Failure(ErrorCode::OutOfMemory, "no raster for the page");
	SkCanvas* canvas = surface->getCanvas();
	canvas->clear(SK_ColorWHITE);
	const float dotsPerPoint = dpi / 72;
	for (const BPrintPage::Placement& placement : page.placements) {
		if (placement.list == nullptr || !(placement.pointsPerPixel > 0))
			continue;
		// device = (at + (logical - area origin) * ppp) * dots per point
		const float scale = placement.pointsPerPixel * dotsPerPoint;
		const gfx::Point offset{placement.at.x / placement.pointsPerPixel - placement.area.x0,
			placement.at.y / placement.pointsPerPixel - placement.area.y0};
		canvas->save();
		canvas->clipRect(SkRect::MakeXYWH(std::floor(placement.at.x * dotsPerPoint + 0.5f),
			std::floor(placement.at.y * dotsPerPoint + 0.5f),
			std::floor(placement.area.Width() * scale + 0.5f),
			std::floor(placement.area.Height() * scale + 0.5f)), false);
		fData->backend._RenderOnCanvas(canvas, *placement.list, offset, scale, false);
		canvas->restore();
	}
	return bitmap;
}


bool
SkiaPrintService::Install(const TextSystem* text)
{
	if (BPrintService::Default() != nullptr)
		return false;
	static SkiaPrintService service(text);
	BPrintService::SetDefault(&service);
	return true;
}


} // namespace ngui
