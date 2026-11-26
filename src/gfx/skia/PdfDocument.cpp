/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/PdfDocument.h>

#include <cmath>
#include <sstream>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkDocument.h"
#include "include/core/SkRect.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/docs/SkPDFDocument.h"

#include <ngui/TextSystem.h>
#include <ngui/gfx/SkiaBackend.h>


namespace ngui::gfx {


namespace {


bool
IsPositive(float value)
{
	return std::isfinite(value) && value > 0;
}


} // namespace


struct PdfDocument::Data {
			Data(SkiaBackend& drawing, std::ostream& target, const PdfOptions& options)
				:
				backend(drawing),
				out(target),
				resolution(IsPositive(options.resolution) ? options.resolution : 576)
			{
				SkPDF::Metadata metadata;
				metadata.fTitle = SkString(options.title.c_str());
				metadata.fAuthor = SkString(options.author.c_str());
				metadata.fSubject = SkString(options.subject.c_str());
				metadata.fKeywords = SkString(options.keywords.c_str());
				metadata.fCreator = SkString(options.creator.c_str());
				metadata.fRasterDPI = resolution;
				// images deflated, without loss: a user interface's pictures
				// stay sharp, and Skia needs no JPEG library
				metadata.allowNoJpegs = true;
				document = SkPDF::MakeDocument(&stream, metadata);
			}

			SkiaBackend&		backend;
			std::ostream&		out;
			// Skia is built without RTTI, so its streams cannot be derived
			// from here: the document is written to memory, then to out.
			SkDynamicMemoryWStream stream;
			float				resolution;
			sk_sp<SkDocument>	document;
			SkCanvas*			page = nullptr;
			int					pages = 0;
			bool				closed = false;
};


PdfDocument::PdfDocument(SkiaBackend& backend, std::ostream& out, const PdfOptions& options)
	:
	fData(std::make_unique<Data>(backend, out, options))
{
}


PdfDocument::~PdfDocument()
{
	(void)Close();
}


std::expected<void, Error>
PdfDocument::BeginPage(Size size)
{
	if (fData->closed || fData->document == nullptr)
		return Failure(ErrorCode::InvalidArgument, "the document is closed");
	if (!IsPositive(size.w) || !IsPositive(size.h))
		return Failure(ErrorCode::InvalidArgument, "a page without a size");
	if (size.w > kPdfMaxPageSize || size.h > kPdfMaxPageSize)
		return Failure(ErrorCode::InvalidArgument, "a page larger than PDF allows");
	(void)EndPage();
	fData->page = fData->document->beginPage(size.w, size.h);
	if (fData->page == nullptr)
		return Failure(ErrorCode::NotSupported, "Skia cannot begin the page");
	fData->pages++;
	return {};
}


std::expected<void, Error>
PdfDocument::Draw(const DisplayList& list, const Rect& area, Point at, float pointsPerPixel)
{
	if (fData->page == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no page begun");
	if (!area.IsFinite() || !(area.Width() > 0) || !(area.Height() > 0))
		return Failure(ErrorCode::InvalidArgument, "an empty area");
	if (!at.IsFinite() || !IsPositive(pointsPerPixel))
		return Failure(ErrorCode::InvalidArgument, "no place on the page");

	// Device units of the resolution's grid: A1 edges are rounded to them,
	// the canvas maps them to points.
	const float dotsPerPoint = fData->resolution / 72;
	const float scale = pointsPerPixel * dotsPerPoint;
	SkCanvas* canvas = fData->page;
	canvas->save();
	canvas->translate(at.x, at.y);
	canvas->scale(1 / dotsPerPoint, 1 / dotsPerPoint);
	canvas->clipRect(SkRect::MakeWH(std::floor(area.Width() * scale + 0.5f),
		std::floor(area.Height() * scale + 0.5f)), false);
	fData->backend._RenderOnCanvas(canvas, list, {-area.x0, -area.y0}, scale, true);
	canvas->restore();
	return {};
}


std::expected<void, Error>
PdfDocument::EndPage()
{
	if (fData->page == nullptr)
		return {};
	fData->document->endPage();
	fData->page = nullptr;
	return {};
}


std::expected<void, Error>
PdfDocument::Close()
{
	if (fData->closed)
		return {};
	if (fData->document != nullptr && fData->pages == 0)
		(void)BeginPage({1, 1});
	(void)EndPage();
	fData->closed = true;
	if (fData->document != nullptr)
		fData->document->close();
	fData->document = nullptr;
	const sk_sp<SkData> bytes = fData->stream.detachAsData();
	fData->out.write(static_cast<const char*>(bytes->data()),
		static_cast<std::streamsize>(bytes->size()));
	fData->out.flush();
	if (!fData->out)
		return Failure(ErrorCode::IoError, "the PDF could not be written");
	return {};
}


int
PdfDocument::PageCount() const
{
	return fData->pages;
}


PdfExportBackend::PdfExportBackend(const TextSystem* text, const PdfOptions& options)
	:
	fText(text),
	fOptions(options),
	fBackend(std::make_unique<SkiaBackend>())
{
}


PdfExportBackend::~PdfExportBackend() = default;


Caps
PdfExportBackend::Capabilities() const
{
	Caps caps;
	caps.bezierPaths = true;
	caps.concavePolygons = true;
	return caps;
}


std::expected<void, Error>
PdfExportBackend::Export(const DisplayList& list, const Rect& bounds, std::ostream& out,
	const ExportOptions& options)
{
	if (!bounds.IsFinite() || !(bounds.Width() > 0) || !(bounds.Height() > 0))
		return Failure(ErrorCode::InvalidArgument, "empty bounds");
	if (!IsPositive(options.scale))
		return Failure(ErrorCode::InvalidArgument, "a scale that is not positive");
	const float pointsPerPixel = kPdfPointsPerPixel * options.scale;
	const Size page{bounds.Width() * pointsPerPixel, bounds.Height() * pointsPerPixel};

	// the fonts the text system has loaded by now, fallbacks too
	const TextSystem& text = fText != nullptr ? *fText : TextSystem::Default();
	for (const FontFile& font : text.Fonts()) {
		if (fFonts.insert(font.id).second)
			(void)fBackend->AddFont(font.id, font.data, font.faceIndex);
	}

	PdfDocument document(*fBackend, out, fOptions);
	if (auto begun = document.BeginPage(page); !begun.has_value())
		return begun;
	if (auto drawn = document.Draw(list, bounds, {0, 0}, pointsPerPixel); !drawn.has_value())
		return drawn;
	return document.Close();
}


std::expected<std::vector<std::byte>, Error>
ExportPdf(const DisplayList& list, const Rect& bounds, const ExportOptions& options,
	const TextSystem* text, const PdfOptions& pdf)
{
	PdfExportBackend backend(text, pdf);
	std::ostringstream out(std::ios::binary);
	if (auto exported = backend.Export(list, bounds, out, options); !exported.has_value())
		return std::unexpected(exported.error());
	const std::string bytes = std::move(out).str();
	const auto* data = reinterpret_cast<const std::byte*>(bytes.data());
	return std::vector<std::byte>(data, data + bytes.size());
}


} // namespace ngui::gfx
