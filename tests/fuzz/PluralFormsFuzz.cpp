/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstdint>
#include <string_view>

#include "locale/PluralForms.h"


NGUI_FUZZ_TARGET(PluralForms)
{
	// What is tested: PluralForms::Parse() of arbitrary bytes as a
	// "Plural-Forms" value, then FormFor() for counts 0 to 200 and some
	// large ones.
	// Success criteria: No crash, sanitizer report or stack overflow; a
	// rule that parses has 1 to 16 forms, and every form it gives is below
	// that count.
	// Why it matters: The formula is a small language read from catalog
	// files (gettext's C expressions) and evaluated for every plural text.
	//
	// Given
	const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
	const auto rule = ngui::locale::PluralForms::Parse(text);

	// When / Then
	if (!rule.has_value())
		return;
	NGUI_FUZZ_CHECK(rule->Count() >= 1 && rule->Count() <= 16);
	for (std::uint64_t n = 0; n <= 200; n++)
		NGUI_FUZZ_CHECK(rule->FormFor(n) < rule->Count());
	for (const std::uint64_t n : {UINT64_MAX, UINT64_MAX / 2 + 1, std::uint64_t{1} << 32})
		NGUI_FUZZ_CHECK(rule->FormFor(n) < rule->Count());
}
