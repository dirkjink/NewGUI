/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <string>

#include "locale/NumberGrouping.h"


NGUI_FUZZ_TARGET(NumberGrouping)
{
	// What is tested: LocalizeNumber() with a grouping from the bytes (up
	// to the first byte 0xff, as C's lconv::grouping, any sizes, 0 and
	// CHAR_MAX), the separators "_" and "'", and a number made of the
	// remaining bytes as digits (a leading "-" and a "." from the bytes).
	// Success criteria: No crash or sanitizer report and no endless loop;
	// without its "_" and with "." for "'" the result is the number again;
	// it never begins or ends with a separator.
	// Why it matters: The grouping comes from the system's locale data
	// (and Windows' converted), any of which may be odd.
	//
	// Given
	std::size_t at = 0;
	std::string grouping;
	while (at < data.size() && data[at] != 0xff && grouping.size() < 16)
		grouping.push_back(static_cast<char>(data[at++]));
	at++;
	std::string number;
	if (at < data.size() && data[at] % 2 == 1)
		number.push_back('-');
	for (; at < data.size() && number.size() < 64; at++) {
		if (data[at] == '.' && number.find('.') == std::string::npos && !number.empty()
			&& number.back() != '-') {
			number.push_back('.');
		} else
			number.push_back(static_cast<char>('0' + data[at] % 10));
	}
	if (number.empty() || number == "-" || number.back() == '.')
		return;
	const ngui::locale::NumberConventions conventions{"'", "_", grouping, "-"};

	// When
	const std::string localized = ngui::locale::LocalizeNumber(number, conventions);

	// Then
	std::string back;
	for (const char c : localized) {
		if (c != '_')
			back.push_back(c == '\'' ? '.' : c);
	}
	NGUI_FUZZ_CHECK(back == number);
	NGUI_FUZZ_CHECK(!localized.starts_with('_') && !localized.ends_with('_'));
	NGUI_FUZZ_CHECK(localized.find("-_") == std::string::npos);
	NGUI_FUZZ_CHECK(localized.find("_'") == std::string::npos);
}
