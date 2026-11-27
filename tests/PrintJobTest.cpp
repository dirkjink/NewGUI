/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/PrintJob.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Bitmap.h>
#include <ngui/Looper.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/PopUpMenu.h>
#include <ngui/TextControl.h>
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
		return {{"Office", "Office printer", true, false}, {"Lab", "", false, false},
			{"PDF", "PDF file", false, true}};
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


// The marked item of the menu field \a field marked anew at \a index, and
// invoked as a click on it would.
void
Choose(const ViewRef& field, int32 index)
{
	(void)field.Run([index](BView& view) {
		BMenuItem* item = static_cast<BMenuField&>(view).Menu()->ItemAt(index);
		item->SetMarked(true);
		(void)item->Invoke();
	});
}


void
SetText(const ViewRef& control, const char* text)
{
	(void)control.Run([text](BView& view) { static_cast<BTextControl&>(view).SetText(text); });
}


TEST(PrintJobTest, ThePageSetupPanelSetsPaperOrientationAndScale)
{
	// What is tested: ConfigPage() of a job on A4 upright at 100 %: the panel
	// "Page setup" shown, US Letter chosen, "Landscape" clicked, 150 typed
	// as the scale, OK clicked; then the panel again, set to A5 and
	// cancelled; then again and closed.
	// Success criteria: ConfigPage() returns at once with the panel's
	// messenger; the panel shows the paper, both orientations and the
	// scale. After OK the job's settings are Letter, landscape, 150 % and
	// the callback gets true; after Cancel and after closing it they stay,
	// the callback gets false, and the panel is gone.
	// Why it matters: Haiku's programs let the user choose the paper before
	// printing (ConfigPage()); the choice must reach the job, a cancelled
	// one must not.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-page-setup-test");
	Driver driver(app);
	BPrintJob job("Setup");
	std::atomic<int> accepted = 0;
	std::atomic<int> declined = 0;
	const auto count = [&](bool ok) { (ok ? accepted : declined)++; };

	// When
	const auto shown = job.ConfigPage(count);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef panel = driver.Window("Page setup");
	const bool complete = panel.Find("paper").Exists() && panel.Find("portrait").Exists()
		&& panel.Find("landscape").Exists() && panel.Find("scale").Exists();
	const std::optional<std::int32_t> portrait = panel.Find("portrait").Value();
	Choose(panel.Find("paper"), 2);
	ASSERT_TRUE(driver.Click(panel.Find("landscape")).has_value());
	SetText(panel.Find("scale"), "150");
	ASSERT_TRUE(driver.Click(panel.Find("ok")).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const BPrintSettings afterOk = job.Settings();
	const int acceptedAfterOk = accepted;

	(void)job.ConfigPage(count);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	panel = driver.Window("Page setup");
	Choose(panel.Find("paper"), 1);
	ASSERT_TRUE(driver.Click(panel.Find("cancel")).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const BPaper afterCancel = job.Settings().paper;

	const auto closing = job.ConfigPage(count);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	ASSERT_TRUE(closing.has_value());
	(void)closing->SendMessage(kQuitRequested);
	ASSERT_TRUE(closing->WaitForLooperQuit(5s).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const bool gone = !driver.Window("Page setup").IsValid();
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(shown.has_value()) << shown.error().message;
	EXPECT_TRUE(complete);
	EXPECT_EQ(portrait, kControlOn);
	EXPECT_EQ(afterOk.paper, BPaper::Letter());
	EXPECT_EQ(afterOk.orientation, BPrintOrientation::Landscape);
	EXPECT_FLOAT_EQ(afterOk.scale, 150);
	EXPECT_EQ(acceptedAfterOk, 1);
	EXPECT_EQ(afterCancel, BPaper::Letter());
	EXPECT_EQ(accepted, 1);
	EXPECT_EQ(declined, 2);
	EXPECT_TRUE(gone);
}


TEST(PrintJobTest, ThePrintPanelSetsPrinterFileCopiesAndPages)
{
	// What is tested: ConfigJob() with a print service of three printers
	// (Office the default, Lab, a PDF printer that writes files): the panel
	// "Print" shown; the PDF printer chosen, a file and 3 copies typed,
	// "Pages" clicked with 2 to 4, Print clicked. ConfigJob() without a
	// print service.
	// Success criteria: The panel starts on Office with the file field
	// disabled and all pages (from and to disabled); choosing the PDF
	// printer enables the file, "Pages" the range. After Print the job's
	// settings are the PDF printer, the file, 3 copies, pages 2 to 4, and
	// the callback gets true. Without a service ConfigJob() is
	// NotSupported.
	// Why it matters: The print panel is where a user picks the printer and
	// what to print (Haiku's ConfigJob()); the program prints what it
	// reports.
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-print-panel-test");
	Driver driver(app);
	BPrintJob job("Panel");
	const auto withoutService = job.ConfigJob();
	RecordingService service;
	const ScopedService installed(&service);
	std::atomic<int> accepted = 0;

	// When
	const auto shown = job.ConfigJob([&](bool ok) { accepted += ok ? 1 : 0; });
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef panel = driver.Window("Print");
	std::string firstPrinter;
	(void)panel.Find("printer").Run([&](BView& view) {
		if (const BMenuItem* marked = static_cast<BMenuField&>(view).Menu()->FindMarked())
			firstPrinter = marked->Label();
	});
	const bool fileAtFirst = panel.Find("file").IsEnabled();
	const bool rangeAtFirst = panel.Find("from").IsEnabled() || panel.Find("to").IsEnabled();
	Choose(panel.Find("printer"), 2);
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const bool fileForPdf = panel.Find("file").IsEnabled();
	SetText(panel.Find("file"), "/tmp/out.pdf");
	SetText(panel.Find("copies"), "3");
	ASSERT_TRUE(driver.Click(panel.Find("range")).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const bool rangeEnabled = panel.Find("from").IsEnabled() && panel.Find("to").IsEnabled();
	SetText(panel.Find("from"), "2");
	SetText(panel.Find("to"), "4");
	ASSERT_TRUE(driver.Click(panel.Find("print")).has_value());
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	const BPrintSettings settings = job.Settings();
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_FALSE(withoutService.has_value());
	EXPECT_EQ(withoutService.error().code, ErrorCode::NotSupported);
	ASSERT_TRUE(shown.has_value()) << shown.error().message;
	EXPECT_EQ(firstPrinter, "Office");
	EXPECT_FALSE(fileAtFirst);
	EXPECT_FALSE(rangeAtFirst);
	EXPECT_TRUE(fileForPdf);
	EXPECT_TRUE(rangeEnabled);
	EXPECT_EQ(settings.printer, "PDF");
	EXPECT_EQ(settings.file, "/tmp/out.pdf");
	EXPECT_EQ(settings.copies, 3);
	EXPECT_EQ(settings.firstPage, 2);
	EXPECT_EQ(settings.lastPage, 4);
	EXPECT_EQ(job.FirstPage(), 2);
	EXPECT_EQ(job.LastPage(), 4);
	EXPECT_EQ(accepted, 1);
}


} // namespace
