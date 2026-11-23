/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

#include <ngui/test/Image.h>


NGUI_FUZZ_TARGET(Png)
{
	// What is tested: DecodePng() of arbitrary bytes, and for every image it
	// accepts, EncodePng() and DecodePng() again.
	// Success criteria: No crash or sanitizer report; an accepted image lies
	// within kMaxImageDimension and kMaxImagePixels and has exactly
	// width × height × 4 bytes; the round trip reproduces it.
	// Why it matters: Reference images are files from outside (screenshots
	// from Haiku, specification 21.3); the size limits guard the allocation
	// that a forged header would make huge.
	//
	// When
	const std::expected<ngui::test::Image, ngui::Error> image
		= ngui::test::DecodePng(std::as_bytes(data));
	if (!image.has_value())
		return;

	// Then
	NGUI_FUZZ_CHECK(image->Width() > 0 && image->Height() > 0);
	NGUI_FUZZ_CHECK(image->Width() <= ngui::test::kMaxImageDimension);
	NGUI_FUZZ_CHECK(image->Height() <= ngui::test::kMaxImageDimension);
	NGUI_FUZZ_CHECK(std::int64_t{image->Width()} * image->Height()
		<= ngui::test::kMaxImagePixels);
	NGUI_FUZZ_CHECK(image->Bytes().size()
		== static_cast<std::size_t>(image->Width()) * static_cast<std::size_t>(image->Height())
			* 4);
	const std::expected<std::vector<std::byte>, ngui::Error> encoded
		= ngui::test::EncodePng(*image);
	NGUI_FUZZ_CHECK(encoded.has_value());
	const std::expected<ngui::test::Image, ngui::Error> again
		= ngui::test::DecodePng(*encoded);
	NGUI_FUZZ_CHECK(again.has_value() && *again == *image);
}
