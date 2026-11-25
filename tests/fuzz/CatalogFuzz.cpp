/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstdint>
#include <cstring>
#include <span>

#include <ngui/Catalog.h>


NGUI_FUZZ_TARGET(Catalog)
{
	// What is tested: BCatalog::FromData() of arbitrary bytes, then
	// GetString() and GetPluralString() of a few texts, in a few contexts
	// and for counts from the bytes, on the catalogs that load.
	// Success criteria: No crash or sanitizer report; a broken file is an
	// error, never a read outside the bytes; every text returned is there
	// and terminated.
	// Why it matters: Catalogs are files on disk that may be damaged or
	// made to attack (module G7).
	//
	// Given
	const auto catalog = ngui::BCatalog::FromData(std::as_bytes(data));

	// When / Then
	if (!catalog.has_value())
		return;
	const char* texts[] = {"", "Open", "a", "%d file"};
	const char* contexts[] = {nullptr, "", "Menu", "a"};
	for (const char* text : texts) {
		for (const char* context : contexts) {
			const char* translated = (*catalog)->GetString(text, context);
			NGUI_FUZZ_CHECK(translated != nullptr);
			(void)std::strlen(translated);
		}
	}
	const std::uint64_t counts[] = {0, 1, 2, 5, 11, 21, 101, data.empty() ? 7u : data[0]};
	for (const std::uint64_t count : counts) {
		const char* form = (*catalog)->GetPluralString("%d file", "%d files", count, "Menu");
		NGUI_FUZZ_CHECK(form != nullptr);
		(void)std::strlen(form);
	}
}
