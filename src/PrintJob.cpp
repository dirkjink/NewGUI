/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/PrintJob.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

#include <ngui/View.h>
#include <ngui/Window.h>


namespace ngui {


namespace {


std::atomic<BPrintService*> sDefaultService = nullptr;


// Millimeters in points.
constexpr float
Millimeters(float millimeters)
{
	return millimeters * 72 / 25.4f;
}


} // namespace


BPaper
BPaper::A4()
{
	return {"A4", {Millimeters(210), Millimeters(297)}};
}


BPaper
BPaper::A5()
{
	return {"A5", {Millimeters(148), Millimeters(210)}};
}


BPaper
BPaper::Letter()
{
	return {"Letter", {612, 792}};
}


BPaper
BPaper::Legal()
{
	return {"Legal", {612, 1008}};
}


std::vector<BPaper>
BPaper::Standard()
{
	return {A4(), A5(), Letter(), Legal()};
}


int32
BPrintService::Resolution(std::string_view /*printer*/) const
{
	return 300;
}


BPrintService*
BPrintService::Default()
{
	return sDefaultService.load();
}


void
BPrintService::SetDefault(BPrintService* service)
{
	sDefaultService.store(service);
}


struct BPrintJob::Data {
			std::string			name;
			BPrintSettings		settings;
			std::vector<BPrintPage> pages;
			bool				begun = false;
			bool				pageOpen = false;

			// The current page, begun if none is open.
			BPrintPage&			CurrentPage(gfx::Size size);
};


BPrintPage&
BPrintJob::Data::CurrentPage(gfx::Size size)
{
	if (!pageOpen) {
		pages.push_back({size, {}});
		pageOpen = true;
	}
	return pages.back();
}


BPrintJob::BPrintJob(std::string name)
	:
	fData(std::make_unique<Data>())
{
	fData->name = std::move(name);
}


BPrintJob::~BPrintJob() = default;


std::expected<void, Error>
BPrintJob::BeginJob()
{
	BPrintService* service = BPrintService::Default();
	if (service == nullptr)
		return Failure(ErrorCode::NotSupported, "no print service is installed");
	if (!fData->settings.printer.empty()) {
		const std::vector<BPrinterInfo> printers = service->Printers();
		const bool known = std::ranges::any_of(printers, [&](const BPrinterInfo& printer) {
			return printer.name == fData->settings.printer;
		});
		if (!known)
			return Failure(ErrorCode::NotFound, "no printer " + fData->settings.printer);
	}
	fData->pages.clear();
	fData->pageOpen = false;
	fData->begun = true;
	return {};
}


std::expected<void, Error>
BPrintJob::DrawView(BView* view, gfx::Rect rect, gfx::Point where)
{
	if (!fData->begun)
		return Failure(ErrorCode::InvalidArgument, "the job is not begun");
	if (view == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no view");
	BSnapshotOptions options;
	options.area = rect;
	options.scale = 1;
	auto recorded = view->Record(options);
	if (!recorded.has_value())
		return std::unexpected(recorded.error());
	return DrawList(std::move(*recorded), rect, where);
}


std::expected<void, Error>
BPrintJob::DrawList(gfx::DisplayListRef list, gfx::Rect area, gfx::Point where)
{
	if (!fData->begun)
		return Failure(ErrorCode::InvalidArgument, "the job is not begun");
	if (list == nullptr || !area.IsFinite() || !(area.Width() > 0) || !(area.Height() > 0))
		return Failure(ErrorCode::InvalidArgument, "nothing to draw");
	if (!where.IsFinite())
		return Failure(ErrorCode::InvalidArgument, "no place on the page");
	const gfx::Rect paper = PaperRect();
	const gfx::Rect printable = PrintableRect();
	BPrintPage& page = fData->CurrentPage({paper.Width(), paper.Height()});
	page.placements.push_back({std::move(list), area,
		{printable.x0 + where.x, printable.y0 + where.y},
		0.75f * fData->settings.scale / 100});
	return {};
}


void
BPrintJob::SpoolPage()
{
	if (!fData->begun)
		return;
	// a page without drawing is a blank page
	if (!fData->pageOpen) {
		const gfx::Rect paper = PaperRect();
		(void)fData->CurrentPage({paper.Width(), paper.Height()});
	}
	fData->pageOpen = false;
}


std::expected<void, Error>
BPrintJob::CommitJob()
{
	if (!fData->begun)
		return Failure(ErrorCode::InvalidArgument, "the job is not begun");
	fData->pageOpen = false;
	fData->begun = false;
	std::vector<BPrintPage> pages = std::move(fData->pages);
	fData->pages.clear();
	if (pages.empty())
		return Failure(ErrorCode::InvalidArgument, "the job has no pages");
	BPrintService* service = BPrintService::Default();
	if (service == nullptr)
		return Failure(ErrorCode::NotSupported, "no print service is installed");
	return service->Print(fData->name, pages, fData->settings);
}


void
BPrintJob::CancelJob()
{
	fData->pages.clear();
	fData->pageOpen = false;
	fData->begun = false;
}


bool
BPrintJob::CanContinue() const
{
	return fData->begun;
}


const BPrintSettings&
BPrintJob::Settings() const
{
	return fData->settings;
}


void
BPrintJob::SetSettings(const BPrintSettings& settings)
{
	fData->settings = settings;
	fData->settings.copies = std::max(settings.copies, 1);
	if (!std::isfinite(settings.scale) || settings.scale <= 0)
		fData->settings.scale = 100;
}


gfx::Rect
BPrintJob::PaperRect() const
{
	const gfx::Size size = fData->settings.paper.size;
	if (fData->settings.orientation == BPrintOrientation::Landscape)
		return {0, 0, size.h, size.w};
	return {0, 0, size.w, size.h};
}


gfx::Rect
BPrintJob::PrintableRect() const
{
	const gfx::Rect paper = PaperRect();
	const BPrintMargins& margins = fData->settings.margins;
	gfx::Rect printable{paper.x0 + margins.left, paper.y0 + margins.top,
		paper.x1 - margins.right, paper.y1 - margins.bottom};
	if (printable.x1 < printable.x0)
		printable.x1 = printable.x0;
	if (printable.y1 < printable.y0)
		printable.y1 = printable.y0;
	return printable;
}


void
BPrintJob::GetResolution(int32* xDPI, int32* yDPI) const
{
	const BPrintService* service = BPrintService::Default();
	const int32 dpi = service != nullptr ? service->Resolution(fData->settings.printer) : 300;
	if (xDPI != nullptr)
		*xDPI = dpi;
	if (yDPI != nullptr)
		*yDPI = dpi;
}


int32
BPrintJob::FirstPage() const
{
	return std::max(fData->settings.firstPage, 1);
}


int32
BPrintJob::LastPage() const
{
	return std::max(fData->settings.lastPage, FirstPage());
}


int32
BPrintJob::SpooledPages() const
{
	return static_cast<int32>(fData->pages.size());
}


} // namespace ngui
