/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/PdfDocument.h>


NGUI_FUZZ_TARGET(PdfExport)
{
	// What is tested: The PDF export (through Skia) of every list
	// Deserialize() accepts from arbitrary bytes, over the list's bounds, at
	// scale 1 or at a scale from the first byte.
	// Success criteria: Nothing crashes (checked by the sanitizers); the
	// export fails only for bounds whose page PDF cannot hold; it is a PDF
	// from "%PDF-" to "%%EOF", and the same list gives the same bytes twice.
	// Why it matters: Lists come from outside (a remote-rendering stream, a
	// saved document); exporting or printing them must neither crash nor
	// write a file other programs cannot read, whatever numbers they hold.
	//
	// Given
	const auto list = ngui::gfx::DisplayList::Deserialize(std::as_bytes(data));
	if (!list.has_value())
		return;
	ngui::gfx::Rect bounds = (*list)->Bounds();
	if (bounds.IsEmpty())
		bounds = {0, 0, 1, 1};
	const float scale = data.empty() ? 1 : 0.25f + static_cast<float>(data[0] % 8) / 2;

	// When
	const auto first = ngui::gfx::ExportPdf(**list, bounds, {.scale = scale});
	const auto second = ngui::gfx::ExportPdf(**list, bounds, {.scale = scale});

	// Then
	if (!first.has_value()) {
		const float points = ngui::gfx::kPdfPointsPerPixel * scale;
		NGUI_FUZZ_CHECK(!bounds.IsFinite() || bounds.Width() * points > ngui::gfx::kPdfMaxPageSize
			|| bounds.Height() * points > ngui::gfx::kPdfMaxPageSize);
		return;
	}
	NGUI_FUZZ_CHECK(second.has_value() && *first == *second);
	const std::string_view pdf(reinterpret_cast<const char*>(first->data()), first->size());
	NGUI_FUZZ_CHECK(pdf.starts_with("%PDF-"));
	NGUI_FUZZ_CHECK(pdf.find("%%EOF", pdf.size() > 16 ? pdf.size() - 16 : 0)
		!= std::string_view::npos);
}
