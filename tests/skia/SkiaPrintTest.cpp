/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The print service of ngui_skia: the PDF printer, CUPS (a stand-in library,
// tests/print/FakeCups.cpp, where there is CUPS), the raster pages of GDI,
// and on Windows the printer "Microsoft Print to PDF" where it is installed.


#include <ngui/SkiaPrintService.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/HaikuLook.h>
#include <ngui/PrintJob.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>
#include <ngui/text/FontTextSystem.h>

#include "look/LookGallery.h"
#include "skia/Poppler.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::test;


std::string
ReadFile(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}


std::span<const std::byte>
Bytes(const std::string& text)
{
	return std::as_bytes(std::span(text));
}


// A square of 100 × 100 logical pixels, red with a blue right half.
gfx::DisplayListRef
Square()
{
	gfx::Canvas canvas({0, 0, 100, 100});
	canvas.FillRect({0, 0, 100, 100}, {.color = {1, 0, 0, 1}});
	canvas.FillRect({50, 0, 100, 100}, {.color = {0, 0, 1, 1}});
	return canvas.Finish();
}


// Installs \a service as the default one while it lives.
class ScopedService {
public:
	explicit ScopedService(BPrintService* service)
	{
		BPrintService::SetDefault(service);
	}

	~ScopedService()
	{
		BPrintService::SetDefault(nullptr);
	}
};


// A job "Squares" on US Letter: the square at the printable rectangle's
// corner on page 1, at (100, 0) at 200 % on page 2.
std::expected<void, Error>
PrintSquares(const BPrintSettings& base)
{
	BPrintJob job("Squares");
	BPrintSettings settings = base;
	settings.paper = BPaper::Letter();
	job.SetSettings(settings);
	if (auto begun = job.BeginJob(); !begun.has_value())
		return begun;
	(void)job.DrawList(Square(), {0, 0, 100, 100}, {0, 0});
	job.SpoolPage();
	settings.scale = 200;
	job.SetSettings(settings);
	(void)job.DrawList(Square(), {0, 0, 100, 100}, {100, 0});
	return job.CommitJob();
}


TEST(SkiaPrintTest, ThePdfPrinterWritesTheJob)
{
	// What is tested: A service without the system's printers, installed as
	// the default; the job "Squares" (two pages of US Letter, the square of
	// 100 logical pixels at the corner of the printable rectangle, then at
	// 200 % at (100, 0) of it) printed on the PDF printer into a file,
	// rastered at 72 dpi.
	// Success criteria: The service has only the PDF printer, as default;
	// the file is a PDF titled "Squares" with two pages of 612 × 792; the
	// square covers 75 × 75 points from (36, 36), red then blue, on page 1
	// and 150 × 150 points from (136, 36) on page 2. Skipped without
	// pdftoppm, except the file and its title.
	// Why it matters: Printing to a PDF file works everywhere, also without
	// a printer, and is what a print preview and an archive need.
	//
	// Given
	SkiaPrintService service(nullptr, {.systemPrinters = false});
	const ScopedService installed(&service);
	const std::filesystem::path file = std::filesystem::path(testing::TempDir())
		/ "squares.pdf";
	std::filesystem::remove(file);
	BPrintSettings settings;
	settings.printer = std::string(kPdfFilePrinter);
	settings.file = file.string();

	// When
	const std::vector<BPrinterInfo> printers = service.Printers();
	const auto printed = PrintSquares(settings);
	const std::string pdf = ReadFile(file);

	// Then
	ASSERT_TRUE(printed.has_value()) << printed.error().message;
	ASSERT_EQ(printers.size(), 1u);
	EXPECT_EQ(printers[0].name, kPdfFilePrinter);
	EXPECT_TRUE(printers[0].isDefault);
	EXPECT_TRUE(printers[0].writesFile);
	EXPECT_TRUE(pdf.starts_with("%PDF-"));
	EXPECT_NE(pdf.find("Squares"), std::string::npos);
	if (!CanRaster())
		GTEST_SKIP() << "no pdftoppm";
	const Image first = Raster(Bytes(pdf), 72, 1);
	const Image second = Raster(Bytes(pdf), 72, 2);
	EXPECT_EQ(first.Width(), 612);
	EXPECT_EQ(first.Height(), 792);
	EXPECT_EQ(second.Width(), 612);
	const Rgba8 red{255, 0, 0, 255};
	const Rgba8 blue{0, 0, 255, 255};
	const Rgba8 white{255, 255, 255, 255};
	EXPECT_EQ(first.At(37, 37), red);
	EXPECT_EQ(first.At(110, 110), blue);
	EXPECT_EQ(first.At(34, 37), white);
	EXPECT_EQ(first.At(112, 37), white);
	EXPECT_EQ(second.At(137, 37), red);
	EXPECT_EQ(second.At(284, 184), blue);
	EXPECT_EQ(second.At(288, 100), white);
}


#if defined(NGUI_TEST_FAKE_CUPS)
TEST(SkiaPrintTest, CupsGetsTheDocument)
{
	// What is tested: A service with a stand-in for libcups (destinations
	// Office as default with a description, Lab, Lab/duplex, Broken); the job
	// "Squares" printed on Lab/duplex in 3 copies and on the PDF printer;
	// then on Broken.
	// Success criteria: The printers are the four destinations, then the
	// PDF printer, which is not the default; CUPS gets a job on Lab titled
	// "Squares" with copies=3 in application/pdf, the same bytes the PDF
	// printer writes; Broken is IoError with CUPS's message.
	// Why it matters: On Linux, BSD and macOS printing goes through CUPS; a
	// job must reach the printer chosen, with its copies, as the document a
	// user would preview.
	//
	// Given
	const std::filesystem::path directory = std::filesystem::path(testing::TempDir())
		/ "fakecups";
	std::filesystem::create_directories(directory);
	std::filesystem::remove(directory / "job.pdf");
	const std::string directoryName = directory.string();
	const ScopedEnvironment fakeDirectory("FAKE_CUPS_DIR", directoryName.c_str());
	SkiaPrintService service(nullptr, {.cupsLibrary = NGUI_TEST_FAKE_CUPS});
	const ScopedService installed(&service);
	BPrintSettings settings;
	settings.printer = "Lab/duplex";
	settings.copies = 3;
	BPrintSettings toFile;
	toFile.printer = std::string(kPdfFilePrinter);
	toFile.file = (directory / "file.pdf").string();
	BPrintSettings broken;
	broken.printer = "Broken";

	// When
	const std::vector<BPrinterInfo> printers = service.Printers();
	const auto printed = PrintSquares(settings);
	const auto filed = PrintSquares(toFile);
	const auto refused = PrintSquares(broken);

	// Then
	ASSERT_EQ(printers.size(), 5u);
	EXPECT_EQ(printers[0].name, "Office");
	EXPECT_EQ(printers[0].description, "Office printer");
	EXPECT_TRUE(printers[0].isDefault);
	EXPECT_EQ(printers[2].name, "Lab/duplex");
	EXPECT_EQ(printers[4].name, kPdfFilePrinter);
	EXPECT_FALSE(printers[4].isDefault);
	ASSERT_TRUE(printed.has_value()) << printed.error().message;
	ASSERT_TRUE(filed.has_value()) << filed.error().message;
	EXPECT_EQ(ReadFile(directory / "job.txt"),
		"printer=Lab\ntitle=Squares\nformat=application/pdf\ncopies=3\n");
	const std::string sent = ReadFile(directory / "job.pdf");
	EXPECT_TRUE(sent.starts_with("%PDF-"));
	EXPECT_EQ(sent, ReadFile(directory / "file.pdf"));
	ASSERT_FALSE(refused.has_value());
	EXPECT_EQ(refused.error().code, ErrorCode::IoError);
	EXPECT_NE(refused.error().message.find("the printer is broken"), std::string::npos);
}
#endif


TEST(SkiaPrintTest, WithoutCupsThereIsOnlyThePdfPrinter)
{
	// What is tested: A service whose CUPS library does not exist (without
	// the system's printers on Windows); a job for a printer "Office" and
	// one on the PDF printer without a file.
	// Success criteria: Only the PDF printer, as default; "Office" is
	// NotFound at BeginJob(), the PDF printer without a file
	// InvalidArgument.
	// Why it matters: A machine without CUPS still prints to PDF, and a
	// program learns why its printer is not there.
	//
	// Given
#if defined(_WIN32)
	SkiaPrintService service(nullptr, {.systemPrinters = false});
#else
	SkiaPrintService service(nullptr, {.cupsLibrary = "/nonexistent/libcups.so.2"});
#endif
	const ScopedService installed(&service);
	BPrintSettings office;
	office.printer = "Office";
	BPrintSettings noFile;
	noFile.printer = std::string(kPdfFilePrinter);

	// When
	const std::vector<BPrinterInfo> printers = service.Printers();
	const auto unknown = PrintSquares(office);
	const auto withoutFile = PrintSquares(noFile);

	// Then
	ASSERT_EQ(printers.size(), 1u);
	EXPECT_TRUE(printers[0].isDefault);
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
	ASSERT_FALSE(withoutFile.has_value());
	EXPECT_EQ(withoutFile.error().code, ErrorCode::InvalidArgument);
}


TEST(SkiaPrintTest, RasterPagesLookAsThePdf)
{
	// What is tested: A page of 300 × 200 points with the gallery of the
	// Haiku look and a line of text at the corner and the square at (200,
	// 20) at 0.5 points per pixel, rastered for GDI (RenderPage()) at 72 and
	// at 96 dpi, and printed as PDF and rastered by pdftoppm at 96 dpi;
	// pages without a size or resolution.
	// Success criteria: 300 × 200 pixels at 72 dpi with the square's red at
	// (201, 21) and blue at (248, 68), white around; 400 × 267 at 96 dpi,
	// looking like the PDF (CompareRasterizers()); InvalidArgument for the
	// wrong pages. The PDF comparison is skipped without pdftoppm.
	// Why it matters: On Windows pages reach the printer as these rasters;
	// they must show what the PDF shows.
	//
	// Given
	const auto text = text::FontTextSystem::CreateBundled(12);
	ASSERT_TRUE(text.has_value());
	SkiaPrintService service(text->get(), {.systemPrinters = false});
	gfx::Canvas canvas({0, 0, 240, 160});
	canvas.FillRect({0, 0, 240, 160}, {.color = {0.9f, 0.9f, 0.9f, 1}});
	canvas.DrawList(LookGallery(*CreateHaikuLook()), {0, 0});
	(*text)->Shape("Printed", FontRole::Plain).Draw(canvas, {8, 150}, {.color = {0, 0, 0, 1}});
	const BPrintPage page{{300, 200}, {{canvas.Finish(), {0, 0, 240, 160}, {0, 0}, 0.75f},
		{Square(), {0, 0, 100, 100}, {200, 20}, 0.5f}}};
	const std::filesystem::path file = std::filesystem::path(testing::TempDir()) / "page.pdf";
	BPrintSettings settings;
	settings.printer = std::string(kPdfFilePrinter);
	settings.file = file.string();

	// When
	const auto small = service.RenderPage(page, 72);
	const auto screen = service.RenderPage(page, 96);
	const auto printed = service.Print("Page", std::span(&page, 1), settings);
	const auto noSize = service.RenderPage({{0, 200}, {}}, 72);
	const auto noDpi = service.RenderPage(page, 0);

	// Then
	ASSERT_TRUE(small.has_value()) << small.error().message;
	ASSERT_TRUE(screen.has_value());
	ASSERT_TRUE(printed.has_value());
	const Image at72 = Image::FromBitmap(*small);
	const Image at96 = Image::FromBitmap(*screen);
	EXPECT_EQ(at72.Width(), 300);
	EXPECT_EQ(at72.Height(), 200);
	EXPECT_EQ(at72.At(201, 21), (Rgba8{255, 0, 0, 255}));
	EXPECT_EQ(at72.At(248, 68), (Rgba8{0, 0, 255, 255}));
	EXPECT_EQ(at72.At(251, 21), (Rgba8{255, 255, 255, 255}));
	EXPECT_EQ(at96.Width(), 400);
	EXPECT_EQ(at96.Height(), 267);
	for (const auto* wrong : {&noSize, &noDpi}) {
		ASSERT_FALSE(wrong->has_value());
		EXPECT_EQ(wrong->error().code, ErrorCode::InvalidArgument);
	}
	if (!CanRaster())
		GTEST_SKIP() << "no pdftoppm";
	const Image pdf = Raster(Bytes(ReadFile(file)), 96);
	const RasterizerComparison alike = CompareRasterizers(pdf, at96);
	EXPECT_TRUE(alike.passed) << alike.Describe();
}


TEST(SkiaPrintTest, WindowsPrintsThroughGdi)
{
	// What is tested: On Windows, the job "Squares" on the printer
	// "Microsoft Print to PDF" with a file (DOCINFO::lpszOutput), where that
	// printer is installed.
	// Success criteria: The service lists the printer; the job succeeds and
	// the file is a PDF. Skipped elsewhere and without that printer.
	// Why it matters: GDI is how Windows prints; this printer is the one a
	// machine without paper has, and it writes what GDI got.
	//
	// Given
	SkiaPrintService service;
	const std::vector<BPrinterInfo> printers = service.Printers();
	const bool hasPrinter = std::ranges::any_of(printers, [](const BPrinterInfo& printer) {
		return printer.name == "Microsoft Print to PDF";
	});
	if (!hasPrinter)
		GTEST_SKIP() << "no printer Microsoft Print to PDF";
	const ScopedService installed(&service);
	const std::filesystem::path file = std::filesystem::path(testing::TempDir()) / "gdi.pdf";
	std::filesystem::remove(file);
	BPrintSettings settings;
	settings.printer = "Microsoft Print to PDF";
	settings.file = file.string();

	// When
	const auto printed = PrintSquares(settings);

	// Then
	ASSERT_TRUE(printed.has_value()) << printed.error().message;
	EXPECT_TRUE(ReadFile(file).starts_with("%PDF-"));
}


} // namespace
