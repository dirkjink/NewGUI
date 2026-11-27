/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/PrintJob.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Bitmap.h>
#include <ngui/Looper.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/test/Driver.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>

#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A print service that keeps what it is given.
class RecordingService : public BPrintService {
public:
	std::vector<BPrinterInfo> Printers() const override
	{
		return {{"Office", "Office printer", true, false}, {"Lab", "", false, false}};
	}

	std::expected<void, Error> Print(std::string_view jobName,
		std::span<const BPrintPage> pages, const BPrintSettings& settings) override
	{
		if (settings.printer == "Lab")
			return Failure(ErrorCode::IoError, "the lab printer is jammed");
		name = std::string(jobName);
		printed.assign(pages.begin(), pages.end());
		lastSettings = settings;
		return {};
	}

	int32 Resolution(std::string_view printer) const override
	{
		return printer == "Lab" ? 600 : 1200;
	}

	std::string name;
	std::vector<BPrintPage> printed;
	BPrintSettings lastSettings;
};


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


gfx::DisplayListRef
Square()
{
	gfx::Canvas canvas({0, 0, 100, 50});
	canvas.FillRect({0, 0, 100, 50}, {.color = {1, 0, 0, 1}});
	return canvas.Finish();
}


TEST(PrintJobTest, AJobHandsItsPagesToTheService)
{
	// What is tested: A job "Report" on US Letter with margins of half an
	// inch: a list of 100 × 50 drawn at (10, 20) of the printable rectangle,
	// a page spooled, one spooled without drawing, a third drawn at 200 %
	// in landscape after SetSettings(); copies 0 and a scale of 0 in the
	// settings; the resolution of the default printer.
	// Success criteria: The service gets "Report" with three pages: 612 ×
	// 792 points with the list at (46, 56), 0.75 points per pixel; an empty
	// one of the same size; 792 × 612 with the list at 1.5 points per pixel.
	// The paper and printable rectangles follow the orientation, copies
	// become 1, a scale of 0 becomes 100; the resolution is the service's;
	// the job cannot continue after CommitJob().
	// Why it matters: Programs print by placing views on pages in points
	// (Haiku's BPrintJob); the pages must reach the printer as placed.
	//
	// Given
	RecordingService service;
	const ScopedService installed(&service);
	BPrintJob job("Report");
	BPrintSettings settings;
	settings.paper = BPaper::Letter();
	settings.copies = 0;
	settings.scale = 0;
	job.SetSettings(settings);
	const gfx::DisplayListRef square = Square();

	// When
	ASSERT_TRUE(job.BeginJob().has_value());
	const bool begun = job.CanContinue();
	const gfx::Rect paper = job.PaperRect();
	const gfx::Rect printable = job.PrintableRect();
	ASSERT_TRUE(job.DrawList(square, {0, 0, 100, 50}, {10, 20}).has_value());
	job.SpoolPage();
	job.SpoolPage();
	settings.orientation = BPrintOrientation::Landscape;
	settings.scale = 200;
	job.SetSettings(settings);
	const gfx::Rect landscape = job.PaperRect();
	ASSERT_TRUE(job.DrawList(square, {0, 0, 100, 50}, {0, 0}).has_value());
	const int32 spooled = job.SpooledPages();
	int32 dpi = 0;
	job.GetResolution(&dpi, nullptr);
	const auto committed = job.CommitJob();

	// Then
	ASSERT_TRUE(committed.has_value()) << committed.error().message;
	EXPECT_TRUE(begun);
	EXPECT_FALSE(job.CanContinue());
	EXPECT_EQ(paper, (gfx::Rect{0, 0, 612, 792}));
	EXPECT_EQ(printable, (gfx::Rect{36, 36, 576, 756}));
	EXPECT_EQ(landscape, (gfx::Rect{0, 0, 792, 612}));
	EXPECT_EQ(spooled, 3);
	EXPECT_EQ(dpi, 1200);
	EXPECT_EQ(service.name, "Report");
	EXPECT_EQ(service.lastSettings.copies, 1);
	ASSERT_EQ(service.printed.size(), 3u);
	const BPrintPage& first = service.printed[0];
	EXPECT_EQ(first.size, (gfx::Size{612, 792}));
	ASSERT_EQ(first.placements.size(), 1u);
	EXPECT_EQ(first.placements[0].list, square);
	EXPECT_EQ(first.placements[0].area, (gfx::Rect{0, 0, 100, 50}));
	EXPECT_EQ(first.placements[0].at, (gfx::Point{46, 56}));
	EXPECT_FLOAT_EQ(first.placements[0].pointsPerPixel, 0.75f);
	EXPECT_EQ(service.printed[1].size, (gfx::Size{612, 792}));
	EXPECT_TRUE(service.printed[1].placements.empty());
	const BPrintPage& third = service.printed[2];
	EXPECT_EQ(third.size, (gfx::Size{792, 612}));
	ASSERT_EQ(third.placements.size(), 1u);
	EXPECT_EQ(third.placements[0].at, (gfx::Point{36, 36}));
	EXPECT_FLOAT_EQ(third.placements[0].pointsPerPixel, 1.5f);
}


// A window of 120 × 80 with a view, on the pixel backend.
BMessenger
OpenWindow()
{
	auto window = std::make_unique<BWindow>(gfx::Rect{50, 50, 170, 130}, "Print",
		BWindowLook::Titled);
	(void)window->UseBackend(std::make_unique<PixelBackend>());
	window->AddChild(std::make_unique<BView>(gfx::Rect{10, 20, 60, 50}, "inner",
		kFollowLeftTop, BViewFlag::WillDraw));
	window->Show();
	return *BLooper::Run(std::move(window));
}


TEST(PrintJobTest, DrawViewPrintsWhatTheViewShows)
{
	// What is tested: DrawView() of a view of 50 × 30 in a window, a part
	// of 40 × 20 of it, on the window's thread; the placement rendered by
	// the pixel backend against the view's snapshot of the same part.
	// Success criteria: One page with the part placed at the printable
	// rectangle's corner; its list covers the part and renders to the
	// snapshot, pixel for pixel.
	// Why it matters: What a program prints of a view is what the view shows
	// on screen (specification 19).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-print-test");
	Driver driver(app);
	const BMessenger window = OpenWindow();
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	RecordingService service;
	const ScopedService installed(&service);
	BPrintJob job("View");
	PixelBackend pixels;

	// When
	ASSERT_TRUE(job.BeginJob().has_value());
	std::expected<void, Error> drawn = Failure(ErrorCode::NotFound);
	std::expected<BBitmap, Error> shot = Failure(ErrorCode::NotFound);
	(void)driver.Window("Print").Find("inner").Run([&](BView& view) {
		drawn = job.DrawView(&view, {5, 5, 45, 25}, {0, 0});
		BSnapshotOptions part;
		part.area = gfx::Rect{5, 5, 45, 25};
		part.scale = 1;
		shot = view.Snapshot(part);
	});
	const auto committed = job.CommitJob();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(drawn.has_value()) << drawn.error().message;
	ASSERT_TRUE(committed.has_value());
	ASSERT_TRUE(shot.has_value());
	ASSERT_EQ(service.printed.size(), 1u);
	ASSERT_EQ(service.printed[0].placements.size(), 1u);
	const BPrintPage::Placement& placement = service.printed[0].placements[0];
	EXPECT_EQ(placement.area, (gfx::Rect{5, 5, 45, 25}));
	EXPECT_EQ(placement.at, (gfx::Point{36, 36}));
	EXPECT_EQ(placement.list->Bounds(), placement.area);
	const auto printed = RenderToImage(pixels, *placement.list);
	ASSERT_TRUE(printed.has_value());
	EXPECT_TRUE(CompareImages(Image::FromBitmap(*shot), *printed, Tolerance::Exact()).passed);
}


TEST(PrintJobTest, ItSaysWhatIsWrong)
{
	// What is tested: A job without a print service; with one: a printer it
	// does not have, drawing before BeginJob(), a view without a window, an
	// empty area, committing without pages, a cancelled job, a printer that
	// fails; the default papers.
	// Success criteria: Without a service BeginJob() is NotSupported; the
	// unknown printer NotFound; drawing before the job, without a window or
	// an area, and committing without pages or after CancelJob()
	// InvalidArgument; the service's IoError comes back from CommitJob().
	// A4 is 595.3 × 841.9 points and the first of the standard papers.
	// Why it matters: A program must learn why nothing was printed instead
	// of waiting for paper.
	//
	// Given
	BPrintJob job("Errors");
	const gfx::DisplayListRef square = Square();
	BView loose({0, 0, 10, 10}, "loose", kFollowLeftTop, BViewFlag::WillDraw);

	// When / Then
	const auto withoutService = job.BeginJob();
	ASSERT_FALSE(withoutService.has_value());
	EXPECT_EQ(withoutService.error().code, ErrorCode::NotSupported);

	RecordingService service;
	const ScopedService installed(&service);
	BPrintSettings settings;
	settings.printer = "Basement";
	job.SetSettings(settings);
	const auto unknown = job.BeginJob();
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);

	settings.printer = "Lab";
	job.SetSettings(settings);
	const auto early = job.DrawList(square, {0, 0, 100, 50}, {0, 0});
	ASSERT_FALSE(early.has_value());
	EXPECT_EQ(early.error().code, ErrorCode::InvalidArgument);
	ASSERT_TRUE(job.BeginJob().has_value());
	const auto noWindow = job.DrawView(&loose, {0, 0, 10, 10}, {0, 0});
	const auto noArea = job.DrawList(square, {0, 0, 0, 50}, {0, 0});
	const auto noPages = job.CommitJob();
	for (const auto* wrong : {&early, &noWindow, &noArea, &noPages}) {
		ASSERT_FALSE(wrong->has_value());
		EXPECT_EQ(wrong->error().code, ErrorCode::InvalidArgument);
	}

	ASSERT_TRUE(job.BeginJob().has_value());
	ASSERT_TRUE(job.DrawList(square, {0, 0, 100, 50}, {0, 0}).has_value());
	job.CancelJob();
	EXPECT_FALSE(job.CanContinue());
	const auto cancelled = job.CommitJob();
	ASSERT_FALSE(cancelled.has_value());
	EXPECT_EQ(cancelled.error().code, ErrorCode::InvalidArgument);

	ASSERT_TRUE(job.BeginJob().has_value());
	ASSERT_TRUE(job.DrawList(square, {0, 0, 100, 50}, {0, 0}).has_value());
	const auto jammed = job.CommitJob();
	ASSERT_FALSE(jammed.has_value());
	EXPECT_EQ(jammed.error().code, ErrorCode::IoError);

	const BPaper a4 = BPaper::A4();
	EXPECT_NEAR(a4.size.w, 595.28f, 0.01f);
	EXPECT_NEAR(a4.size.h, 841.89f, 0.01f);
	EXPECT_EQ(BPaper::Standard().front(), a4);
	EXPECT_EQ(job.FirstPage(), 1);
	EXPECT_GE(job.LastPage(), job.FirstPage());
}


} // namespace
