/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <string_view>

#include <ngui/MimeType.h>


NGUI_FUZZ_TARGET(MimeType)
{
	// What is tested: Arbitrary bytes as a MIME type (IsValid(), SetTo(),
	// the supertype, the database) and as a file name (GuessMimeType()).
	// Success criteria: No crash, hang or sanitizer report. SetTo() succeeds
	// exactly for a valid type, which it then holds; the supertype of a
	// valid type with a subtype is valid and contains it; a guessed type is
	// always valid and either installed or application/octet-stream.
	// Why it matters: Types and file names come from files, drops and
	// messages of other programs.
	//
	const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());

	ngui::BMimeType type;
	const bool valid = ngui::BMimeType::IsValid(text);
	NGUI_FUZZ_CHECK(type.SetTo(text).has_value() == valid);
	NGUI_FUZZ_CHECK(type.IsValid() == valid);
	if (valid) {
		NGUI_FUZZ_CHECK(type.Type() == text);
		const auto super = type.GetSupertype();
		NGUI_FUZZ_CHECK(super.has_value() == !type.IsSupertypeOnly());
		if (super.has_value())
			NGUI_FUZZ_CHECK(super->IsValid() && super->Contains(type));
		(void)type.ShortDescription();
		(void)type.FileExtensions();
	}

	const ngui::BMimeType guessed = ngui::BMimeType::GuessMimeType(text);
	NGUI_FUZZ_CHECK(guessed.IsValid());
	NGUI_FUZZ_CHECK(guessed.IsInstalled() || guessed == ngui::kFileMimeType);
}
