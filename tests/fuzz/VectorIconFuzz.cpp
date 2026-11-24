/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>

#include <ngui/Bitmap.h>
#include <ngui/IconUtils.h>
#include <ngui/svg/LunaSvgRenderer.h>


NGUI_FUZZ_TARGET(VectorIcon)
{
	// What is tested: Arbitrary bytes as an SVG icon, drawn with the lunasvg
	// renderer by BIconUtils::GetVectorIcon() into a bitmap of 1 to 64 × 1
	// to 64 pixels in B_RGBA32 or another color space, as the first three
	// bytes say.
	// Success criteria: No crash, hang or sanitizer report. The result is
	// either drawn or BadData (the bitmap is valid and a renderer is
	// installed). A drawn B_RGBA32 bitmap has no color where it is fully
	// transparent (alpha 0 means 0, 0, 0, as the renderer writes it).
	// Why it matters: Icons may come from files and resources of any
	// origin; lunasvg reads them in the application's process.
	//
	if (data.size() < 3)
		return;
	// installed for this input only: in the regression test other tests
	// follow
	static const ngui::svg::LunaSvgRenderer renderer;
	struct Installed {
		Installed() { ngui::VectorIconRenderer::SetDefault(&renderer); }
		~Installed() { ngui::VectorIconRenderer::SetDefault(nullptr); }
	} installed;
	const float width = static_cast<float>(1 + data[0] % 64);
	const float height = static_cast<float>(1 + data[1] % 64);
	// RGBA32 mostly, every other color space sometimes
	const auto space = data[2] < 192 ? ngui::BColorSpace::RGBA32
		: static_cast<ngui::BColorSpace>(1 + data[2] % 14);
	ngui::BBitmap bitmap({0, 0, width, height}, space);
	NGUI_FUZZ_CHECK(bitmap.IsValid());

	const auto result = ngui::BIconUtils::GetVectorIcon(data.subspan(3), &bitmap);
	NGUI_FUZZ_CHECK(result.has_value() || result.error().code == ngui::ErrorCode::BadData);
	if (result.has_value() && space == ngui::BColorSpace::RGBA32) {
		const std::span<const std::uint8_t> bits = bitmap.Bits();
		for (std::size_t i = 0; i + 3 < bits.size(); i += 4) {
			if (bits[i + 3] == 0)
				NGUI_FUZZ_CHECK(bits[i] == 0 && bits[i + 1] == 0 && bits[i + 2] == 0);
		}
	}
}
