/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include <ngui/Error.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/ExportBackend.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {
class TextSystem;
}


namespace ngui::gfx {


class SkiaBackend;


/*!	Points (1/72 inch) per logical pixel at 96 logical pixels per inch, as
	in CSS: a window of 800 logical pixels is 600 points (21.2 cm) wide. */
inline constexpr float kPdfPointsPerPixel = 0.75f;

/*!	The largest side of a page in points, 200 inches (5.08 m): what PDF
	viewers take without a user unit. */
inline constexpr float kPdfMaxPageSize = 14400;


/*!	What a PDF document says about itself, and how fine it is drawn. */
struct PdfOptions {
			std::string			title{};		/*!< The document's title; empty for none. */
			std::string			author{};		/*!< Its author; empty for none. */
			std::string			subject{};	/*!< Its subject; empty for none. */
			std::string			keywords{};	/*!< Its keywords; empty for none. */
			std::string			creator = "NewGUI";
									/*!< The program that made it. */
			float				resolution = 576;
									/*!< Dots per inch: the grid the edges of rule A1 are
										rounded to, and what Skia rasters at where it
										cannot write vectors; Skia also rounds page sizes
										to whole dots. 576 is 8 dots per point and 6 per
										logical pixel at 96 per inch: sizes in whole
										points stay exact, and edges on whole pixels stay
										there when the page is shown at 96 dpi. Not
										positive: 576. */
};


/*!	A PDF document made with Skia (specification 19.4): DisplayLists drawn
	on pages of any size, written to a stream when it is closed. Part of
	ngui_skia, like the backend whose images and fonts it uses.

	Pages are measured in points (1/72 inch), origin top left; lists are
	placed on them in logical pixels, kPdfPointsPerPixel points each unless
	said otherwise. Everything is drawn as vectors: the axis-parallel
	rectangles of rule A1 with their edges on a grid of
	PdfOptions::resolution, everything else as it is; glyph runs become text
	in the embedded fonts of the backend (AddFont()), images of
	ImageRegistry::Default() are embedded without loss. The SVG rules E1 to
	E3 do not apply: how text and clips look in the file is Skia's
	business.

	Not thread-safe; the backend must not render elsewhere meanwhile.
*/
class PdfDocument {
public:
								PdfDocument(SkiaBackend& backend, std::ostream& out,
									const PdfOptions& options = {});
									/*!< A document without pages that \a backend draws
										and that is written to \a out; both must outlive
										it. */
								~PdfDocument();
									/*!< Close()s it if that has not happened. */
								PdfDocument(const PdfDocument&) = delete;
			PdfDocument&		operator=(const PdfDocument&) = delete;

			std::expected<void, Error> BeginPage(Size size);
									/*!< Starts a page of \a size points, ending the open
										one. InvalidArgument for a size that is not positive
										or larger than kPdfMaxPageSize, or for a closed
										document. */
			std::expected<void, Error> Draw(const DisplayList& list, const Rect& area,
									Point at, float pointsPerPixel = kPdfPointsPerPixel);
									/*!< Draws \a area of \a list (logical pixels, cut to
										it) on the open page with its top left corner at
										\a at (points), \a pointsPerPixel points per
										logical pixel; nothing behind it. InvalidArgument
										without an open page, for an empty or not finite
										area, a position that is not finite or a factor
										that is not positive. */
			std::expected<void, Error> EndPage();
									/*!< Ends the open page; nothing without one. */
			std::expected<void, Error> Close();
									/*!< Ends the open page and writes the document to the
										stream; a document without pages has one empty page
										of 1 × 1 point, as PDF has no empty documents.
										IoError if the stream fails; a second Close() does
										nothing. */

			int					PageCount() const;
									/*!< The pages begun so far. */

private:
			struct Data;

			std::unique_ptr<Data> fData;
};


/*!	The ExportBackend of PDF documents (specification 19.4): a list as a
	document of one page, its bounds times kPdfPointsPerPixel times
	ExportOptions::scale points large. ExportOptions::text and
	ExportOptions::embedImages do not apply: glyph runs are always text in
	embedded fonts, images always embedded. Draws with a SkiaBackend of its
	own; it needs no window.
*/
class PdfExportBackend final : public ExportBackend {
public:
	explicit					PdfExportBackend(const TextSystem* text = nullptr,
									const PdfOptions& options = {});
									/*!< Fonts from \a text (TextSystem::Default() if
										nullptr), added before each export; \a options for
										the documents. */
								~PdfExportBackend() override;

			Caps				Capabilities() const override;
									/*!< What the Skia backend draws: the core profile,
										paths and concave polygons. */
			std::expected<void, Error> Export(const DisplayList& list, const Rect& bounds,
									std::ostream& out, const ExportOptions& options) override;
									/*!< Writes \a list, cut to \a bounds, as a PDF page.
										InvalidArgument for empty or not finite bounds, a
										scale that is not positive or a page larger than
										kPdfMaxPageSize, IoError if \a out fails. */

private:
			const TextSystem*	fText;
			PdfOptions			fOptions;
			std::unique_ptr<SkiaBackend> fBackend;
			std::set<FontId>	fFonts;
};


/*!	\a list as a PDF document of one page over \a bounds, through
	PdfExportBackend with \a text (TextSystem::Default() if nullptr). */
std::expected<std::vector<std::byte>, Error> ExportPdf(const DisplayList& list,
	const Rect& bounds, const ExportOptions& options = {}, const TextSystem* text = nullptr,
	const PdfOptions& pdf = {});


} // namespace ngui::gfx
