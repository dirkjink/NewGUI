/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "SystemPrinters.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winspool.h>


namespace ngui::print {


namespace {


std::wstring
Wide(std::string_view text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	(void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		wide.data(), length);
	return wide;
}


std::string
Utf8(const wchar_t* text)
{
	if (text == nullptr || *text == L'\0')
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr,
		nullptr);
	std::string utf8(static_cast<std::size_t>(std::max(length - 1, 0)), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), length, nullptr, nullptr);
	return utf8;
}


std::wstring
DefaultPrinter()
{
	DWORD size = 0;
	(void)GetDefaultPrinterW(nullptr, &size);
	if (size == 0)
		return {};
	std::wstring name(size, L'\0');
	if (!GetDefaultPrinterW(name.data(), &size))
		return {};
	name.resize(size > 0 ? size - 1 : 0);
	return name;
}


// The printer's settings with paper, orientation and copies of \a settings
// for a page of \a page points; empty if the driver gives none.
std::vector<std::byte>
DeviceMode(const std::wstring& printer, gfx::Size page, int32 copies)
{
	HANDLE handle = nullptr;
	if (!OpenPrinterW(const_cast<wchar_t*>(printer.c_str()), &handle, nullptr))
		return {};
	const LONG size = DocumentPropertiesW(nullptr, handle, const_cast<wchar_t*>(printer.c_str()),
		nullptr, nullptr, 0);
	std::vector<std::byte> bytes(size > 0 ? static_cast<std::size_t>(size) : 0);
	auto* mode = reinterpret_cast<DEVMODEW*>(bytes.data());
	if (bytes.empty() || DocumentPropertiesW(nullptr, handle,
			const_cast<wchar_t*>(printer.c_str()), mode, nullptr, DM_OUT_BUFFER) != IDOK) {
		ClosePrinter(handle);
		return {};
	}
	// the paper upright in tenths of a millimeter, turned for a wide page
	const bool landscape = page.w > page.h;
	const float shortSide = std::min(page.w, page.h);
	const float longSide = std::max(page.w, page.h);
	mode->dmFields |= DM_PAPERWIDTH | DM_PAPERLENGTH | DM_ORIENTATION | DM_COPIES;
	mode->dmPaperSize = 0;
	mode->dmPaperWidth = static_cast<short>(std::lround(shortSide * 254 / 72));
	mode->dmPaperLength = static_cast<short>(std::lround(longSide * 254 / 72));
	mode->dmOrientation = static_cast<short>(landscape ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT);
	mode->dmCopies = static_cast<short>(std::clamp(copies, 1, 9999));
	(void)DocumentPropertiesW(nullptr, handle, const_cast<wchar_t*>(printer.c_str()), mode,
		mode, DM_IN_BUFFER | DM_OUT_BUFFER);
	ClosePrinter(handle);
	return bytes;
}


// Deletes a device context when it goes.
struct DeviceContext {
	HDC			dc = nullptr;
	bool		printing = false;

	~DeviceContext()
	{
		if (printing)
			(void)AbortDoc(dc);
		if (dc != nullptr)
			(void)DeleteDC(dc);
	}
};


} // namespace


std::vector<BPrinterInfo>
CupsPrinters(const std::string& /*library*/)
{
	return {};
}


std::expected<void, Error>
CupsPrint(const std::string& /*library*/, std::string_view /*printer*/,
	std::string_view /*title*/, std::span<const std::byte> /*pdf*/, int32 /*copies*/)
{
	return Failure(ErrorCode::NotSupported, "CUPS is not used on Windows");
}


std::vector<BPrinterInfo>
GdiPrinters()
{
	DWORD needed = 0;
	DWORD count = 0;
	constexpr DWORD kFlags = PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS;
	(void)EnumPrintersW(kFlags, nullptr, 4, nullptr, 0, &needed, &count);
	if (needed == 0)
		return {};
	std::vector<std::byte> buffer(needed);
	if (!EnumPrintersW(kFlags, nullptr, 4, reinterpret_cast<LPBYTE>(buffer.data()), needed,
			&needed, &count)) {
		return {};
	}
	const std::wstring defaultPrinter = DefaultPrinter();
	const auto* infos = reinterpret_cast<const PRINTER_INFO_4W*>(buffer.data());
	std::vector<BPrinterInfo> printers;
	for (DWORD i = 0; i < count; i++) {
		if (infos[i].pPrinterName == nullptr)
			continue;
		printers.push_back({Utf8(infos[i].pPrinterName), {},
			defaultPrinter == infos[i].pPrinterName, false});
	}
	return printers;
}


std::expected<void, Error>
GdiPrint(std::string_view printer, std::string_view title, std::span<const BPrintPage> pages,
	const BPrintSettings& settings, const PageRasterizer& raster)
{
	const std::wstring name = Wide(printer);
	const std::vector<std::byte> mode = DeviceMode(name, pages.front().size, settings.copies);
	DeviceContext context;
	context.dc = CreateDCW(nullptr, name.c_str(), nullptr,
		mode.empty() ? nullptr : reinterpret_cast<const DEVMODEW*>(mode.data()));
	if (context.dc == nullptr)
		return Failure(ErrorCode::NotFound, "Windows has no printer " + std::string(printer));

	const std::wstring document = Wide(title);
	const std::wstring output = Wide(settings.file);
	DOCINFOW info{};
	info.cbSize = sizeof(info);
	info.lpszDocName = document.c_str();
	info.lpszOutput = output.empty() ? nullptr : output.c_str();
	if (StartDocW(context.dc, &info) <= 0)
		return Failure(ErrorCode::IoError, "Windows cannot start the print job");
	context.printing = true;

	// the printable area: device pixels from the paper's corner
	const int dpiX = GetDeviceCaps(context.dc, LOGPIXELSX);
	const int dpiY = GetDeviceCaps(context.dc, LOGPIXELSY);
	const int offsetX = GetDeviceCaps(context.dc, PHYSICALOFFSETX);
	const int offsetY = GetDeviceCaps(context.dc, PHYSICALOFFSETY);
	const float dpi = static_cast<float>(std::min(std::max(dpiX, dpiY), 300));
	for (const BPrintPage& page : pages) {
		auto pixels = raster(page, dpi);
		if (!pixels.has_value())
			return std::unexpected(pixels.error());
		if (StartPage(context.dc) <= 0)
			return Failure(ErrorCode::IoError, "Windows cannot start a page");
		BITMAPINFO bitmap{};
		bitmap.bmiHeader.biSize = sizeof(bitmap.bmiHeader);
		bitmap.bmiHeader.biWidth = pixels->width;
		bitmap.bmiHeader.biHeight = -pixels->height;	// top down
		bitmap.bmiHeader.biPlanes = 1;
		bitmap.bmiHeader.biBitCount = 32;
		bitmap.bmiHeader.biCompression = BI_RGB;
		const int width = static_cast<int>(std::lround(page.size.w / 72
			* static_cast<float>(dpiX)));
		const int height = static_cast<int>(std::lround(page.size.h / 72
			* static_cast<float>(dpiY)));
		const int drawn = StretchDIBits(context.dc, -offsetX, -offsetY, width, height, 0, 0,
			pixels->width, pixels->height, pixels->bgra.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
		if (drawn == 0 || drawn == static_cast<int>(GDI_ERROR) || EndPage(context.dc) <= 0)
			return Failure(ErrorCode::IoError, "Windows cannot print a page");
	}
	if (EndDoc(context.dc) <= 0)
		return Failure(ErrorCode::IoError, "Windows cannot finish the print job");
	context.printing = false;
	return {};
}


int32
GdiResolution(std::string_view printer)
{
	const std::wstring name = Wide(printer.empty() ? Utf8(DefaultPrinter().c_str()) : printer);
	HDC dc = CreateICW(nullptr, name.c_str(), nullptr, nullptr);
	if (dc == nullptr)
		return 300;
	const int dpi = GetDeviceCaps(dc, LOGPIXELSX);
	(void)DeleteDC(dc);
	return dpi > 0 ? dpi : 300;
}


} // namespace ngui::print
