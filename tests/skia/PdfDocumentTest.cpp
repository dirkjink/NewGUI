/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// PDF documents made with Skia (specification 19.4). What a document shows
// is checked with poppler's pdftoppm (rastered) and pdftotext (its text),
// where the build found them (NGUI_TEST_PDFTOPPM, NGUI_TEST_PDFTOTEXT; the
// Linux CI installs poppler-utils); without them those tests are skipped.


#include <ngui/gfx/PdfDocument.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/HaikuLook.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/ImageRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>
#include <ngui/text/FontTextSystem.h>

#include "look/LookGallery.h"
#include "skia/Poppler.h"


namespace {


using namespace ngui;
using namespace ngui::gfx;
using namespace ngui::test;


std::string_view
Text(std::span<const std::byte> bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}


// The gallery of the Haiku look with two lines of text in the bundled fonts
// and a picture of 16 × 16 pixels below it, on white.
DisplayListRef
Gallery(const TextSystem& text, ImageHandle picture)
{
	const DisplayListRef controls = LookGallery(*CreateHaikuLook());
	const Rect bounds = controls->Bounds();
	Canvas canvas({0, 0, bounds.Width(), bounds.Height() + 60});
	canvas.FillRect({0, 0, bounds.Width(), bounds.Height() + 60}, {.color = {1, 1, 1, 1}});
	canvas.DrawList(controls, {-bounds.x0, -bounds.y0});
	text.Shape("Printed by NewGUI", FontRole::Plain).Draw(canvas, {6, bounds.Height() + 18},
		{.color = {0, 0, 0, 1}});
	text.Shape("Bold and blue", FontRole::Bold).Draw(canvas, {6, bounds.Height() + 38},
		{.color = {0.1f, 0.2f, 0.7f, 1}});
	canvas.DrawImage(picture, {0, 0, 16, 16}, {150, bounds.Height() + 24, 182,
		bounds.Height() + 56});
	return canvas.Finish();
}


// 16 × 16 pixels in four colored quarters, registered.
ImageHandle
Picture()
{
	std::vector<std::uint8_t> rgba(16 * 16 * 4);
	for (int y = 0; y < 16; y++) {
		for (int x = 0; x < 16; x++) {
			std::uint8_t* p = rgba.data() + static_cast<std::size_t>(y * 16 + x) * 4;
			const bool right = x >= 8;
			const bool bottom = y >= 8;
			p[0] = right ? std::uint8_t{255} : std::uint8_t{0};
			p[1] = bottom ? std::uint8_t{200} : std::uint8_t{0};
			p[2] = !right && !bottom ? std::uint8_t{255} : std::uint8_t{0};
			p[3] = std::uint8_t{255};
		}
	}
	return ImageRegistry::Default().Register({16, 16, 64, rgba}).value_or(0);
}


TEST(PdfDocumentTest, AnExportLooksAsOnTheScreen)
{
	// What is tested: The gallery of the Haiku look with text in the
	// bundled Noto Sans (regular and bold) and a picture scaled up twice,
	// exported as a PDF page (ExportPdf()) and rastered at 96 dpi, against
	// the same rendered by the Skia backend on the CPU at scale 1; the text
	// pdftotext reads from the page.
	// Success criteria: The page has the list's size in pixels (its bounds
	// times 0.75 points); it looks like the CPU rendering as two
	// rasterizers' images do (CompareRasterizers()); both lines are text in
	// the file, not just shapes. Skipped without pdftoppm and pdftotext.
	// Why it matters: A window exported or printed must show what the
	// screen shows: the same controls, text and pictures at the same places
	// (specification 19.4).
	//
	// Given
	if (!CanRaster() || !CanReadText())
		GTEST_SKIP() << "no pdftoppm or pdftotext";
	const auto text = text::FontTextSystem::CreateBundled(12);
	ASSERT_TRUE(text.has_value());
	const ImageHandle picture = Picture();
	const DisplayListRef list = Gallery(**text, picture);
	SkiaBackend cpu;
	for (const FontFile& font : (*text)->Fonts())
		(void)cpu.AddFont(font.id, font.data, font.faceIndex);

	// When
	const auto pdf = ExportPdf(*list, list->Bounds(), {}, text->get());
	const Image page = pdf.has_value() ? Raster(*pdf, 96) : Image{};
	const std::string words = pdf.has_value() ? TextOf(*pdf) : std::string();
	const auto reference = RenderToImage(cpu, *list);
	ImageRegistry::Default().Release(picture);

	// Then
	ASSERT_TRUE(pdf.has_value()) << pdf.error().message;
	ASSERT_TRUE(reference.has_value());
	EXPECT_EQ(page.Width(), reference->Width());
	EXPECT_EQ(page.Height(), reference->Height());
	const RasterizerComparison alike = CompareRasterizers(*reference, page);
	EXPECT_TRUE(alike.passed) << alike.Describe();
	EXPECT_NE(words.find("Printed by NewGUI"), std::string::npos) << words;
	EXPECT_NE(words.find("Bold and blue"), std::string::npos) << words;
}


TEST(PdfDocumentTest, PagesHaveTheirSizesAndPlaces)
{
	// What is tested: A document (PdfDocument) with a title and three pages:
	// US Letter with a square of 100 × 100 logical pixels at one inch from
	// the top left corner, red, its right half painted over in blue under a
	// clip; 300 × 200 points with the square at 2 points per pixel; and
	// 100 × 50 points left empty; rastered at 72 dpi.
	// Success criteria: Three pages; they are 612 × 792, 300 × 200 and
	// 100 × 50 pixels; the square covers 75 × 75 points from (72, 72) on the
	// first page and 200 × 200 points from (10, 0) on the second, red on the
	// left, blue on the right, white just outside; the document says its
	// title. Skipped without pdftoppm, except the counts.
	// Why it matters: Printing places views on paper: the page size, the
	// position and the scale given must be the ones on paper.
	//
	// Given
	Canvas canvas({0, 0, 100, 100});
	canvas.FillRect({0, 0, 100, 100}, {.color = {1, 0, 0, 1}});
	canvas.Save();
	canvas.ClipRect({50, 0, 100, 100});
	canvas.FillRect({0, 0, 100, 100}, {.color = {0, 0, 1, 1}});
	canvas.Restore();
	const DisplayListRef square = canvas.Finish();
	SkiaBackend backend;
	std::ostringstream out(std::ios::binary);

	// When
	{
		PdfDocument document(backend, out, {.title = "Three pages"});
		ASSERT_TRUE(document.BeginPage({612, 792}).has_value());
		ASSERT_TRUE(document.Draw(*square, {0, 0, 100, 100}, {72, 72}).has_value());
		ASSERT_TRUE(document.BeginPage({300, 200}).has_value());
		ASSERT_TRUE(document.Draw(*square, {0, 0, 100, 100}, {10, 0}, 2).has_value());
		ASSERT_TRUE(document.BeginPage({100, 50}).has_value());
		EXPECT_EQ(document.PageCount(), 3);
		ASSERT_TRUE(document.Close().has_value());
	}
	const std::string bytes = out.str();
	const std::span<const std::byte> pdf(reinterpret_cast<const std::byte*>(bytes.data()),
		bytes.size());

	// Then
	EXPECT_TRUE(bytes.starts_with("%PDF-"));
	EXPECT_NE(bytes.find("Three pages"), std::string::npos);
	if (!CanRaster())
		GTEST_SKIP() << "no pdftoppm";
	const Image letter = Raster(pdf, 72, 1);
	const Image wide = Raster(pdf, 72, 2);
	const Image small = Raster(pdf, 72, 3);
	EXPECT_EQ(letter.Width(), 612);
	EXPECT_EQ(letter.Height(), 792);
	EXPECT_EQ(wide.Width(), 300);
	EXPECT_EQ(wide.Height(), 200);
	EXPECT_EQ(small.Width(), 100);
	EXPECT_EQ(small.Height(), 50);
	const Rgba8 red{255, 0, 0, 255};
	const Rgba8 blue{0, 0, 255, 255};
	const Rgba8 white{255, 255, 255, 255};
	EXPECT_EQ(letter.At(73, 73), red);
	EXPECT_EQ(letter.At(108, 145), red);
	EXPECT_EQ(letter.At(111, 73), blue);
	EXPECT_EQ(letter.At(145, 145), blue);
	EXPECT_EQ(letter.At(70, 100), white);
	EXPECT_EQ(letter.At(148, 100), white);
	EXPECT_EQ(wide.At(11, 1), red);
	EXPECT_EQ(wide.At(108, 198), red);
	EXPECT_EQ(wide.At(112, 1), blue);
	EXPECT_EQ(wide.At(208, 198), blue);
	EXPECT_EQ(wide.At(8, 100), white);
	EXPECT_EQ(wide.At(212, 100), white);
	EXPECT_EQ(small.At(50, 25), white);
}


// A stream buffer that fails every write.
class FailingBuffer : public std::streambuf {
protected:
	int_type overflow(int_type /*c*/) override
	{
		return traits_type::eof();
	}
};


TEST(PdfDocumentTest, ItIsTheSameEveryTimeAndSaysWhatIsWrong)
{
	// What is tested: The same list exported twice; a document closed
	// without pages; wrong uses: bounds that are empty or not finite, a scale
	// of 0, a page of 0 points or of more than kPdfMaxPageSize, drawing
	// without a page or with an empty area, beginning a page after Close(),
	// a stream that fails.
	// Success criteria: Both exports are the same bytes; the document
	// without pages is a PDF with one page; the wrong uses are
	// InvalidArgument, the failing stream IoError, a second Close() does
	// nothing.
	// Why it matters: Documents are compared and archived: the same content
	// must give the same file, without dates or random IDs; and a program
	// must learn why nothing was printed.
	//
	// Given
	Canvas canvas({0, 0, 40, 30});
	canvas.FillRect({5, 5, 35, 25}, {.color = {0, 0.5f, 0, 1}});
	const DisplayListRef list = canvas.Finish();
	SkiaBackend backend;
	std::ostringstream empty(std::ios::binary);
	FailingBuffer failing;
	std::ostream broken(&failing);

	// When
	const auto first = ExportPdf(*list, list->Bounds());
	const auto second = ExportPdf(*list, list->Bounds());
	{
		PdfDocument document(backend, empty);
		ASSERT_TRUE(document.Close().has_value());
		EXPECT_TRUE(document.Close().has_value());
		const auto afterClose = document.BeginPage({10, 10});
		ASSERT_FALSE(afterClose.has_value());
		EXPECT_EQ(afterClose.error().code, ErrorCode::InvalidArgument);
	}
	std::ostringstream unused(std::ios::binary);
	PdfDocument document(backend, unused);
	const auto withoutPage = document.Draw(*list, list->Bounds(), {0, 0});
	const auto zeroPage = document.BeginPage({0, 10});
	const auto hugePage = document.BeginPage({10, kPdfMaxPageSize + 1});
	ASSERT_TRUE(document.BeginPage({10, 10}).has_value());
	const auto emptyArea = document.Draw(*list, {0, 0, 0, 10}, {0, 0});
	const auto zeroScale = document.Draw(*list, list->Bounds(), {0, 0}, 0);
	const auto emptyBounds = ExportPdf(*list, {0, 0, 0, 0});
	const auto infinite = ExportPdf(*list, {0, 0, std::numeric_limits<float>::infinity(), 1});
	const auto noScale = ExportPdf(*list, list->Bounds(), {.scale = 0});
	PdfExportBackend exporter;
	const auto failed = exporter.Export(*list, list->Bounds(), broken, {});

	// Then
	ASSERT_TRUE(first.has_value()) << first.error().message;
	ASSERT_TRUE(second.has_value());
	EXPECT_EQ(*first, *second);
	EXPECT_TRUE(Text(*first).starts_with("%PDF-"));
	EXPECT_TRUE(empty.str().starts_with("%PDF-"));
	EXPECT_NE(empty.str().find("%%EOF"), std::string::npos);
	for (const auto* wrong : {&withoutPage, &zeroPage, &hugePage, &emptyArea, &zeroScale}) {
		ASSERT_FALSE(wrong->has_value());
		EXPECT_EQ(wrong->error().code, ErrorCode::InvalidArgument);
	}
	for (const auto* wrong : {&emptyBounds, &infinite, &noScale}) {
		ASSERT_FALSE(wrong->has_value());
		EXPECT_EQ(wrong->error().code, ErrorCode::InvalidArgument);
	}
	ASSERT_FALSE(failed.has_value());
	EXPECT_EQ(failed.error().code, ErrorCode::IoError);
}


} // namespace
