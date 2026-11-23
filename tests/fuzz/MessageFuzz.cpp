/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include <ngui/Message.h>


NGUI_FUZZ_TARGET(Message)
{
	// What is tested: BMessage::FromFlattened() of arbitrary bytes, and for
	// every message it accepts, Flatten() and FromFlattened() again.
	// Success criteria: No crash or sanitizer report; an accepted input is
	// canonical: flattening the message gives exactly the input bytes, and
	// reading those gives an equal message.
	// Why it matters: Flattened messages come from other processes
	// (clipboard, drag and drop, remote control); forged counts and lengths
	// must neither crash nor allocate without bound.
	//
	// When
	const std::span<const std::byte> bytes = std::as_bytes(data);
	const std::expected<ngui::BMessage, ngui::Error> message
		= ngui::BMessage::FromFlattened(bytes);
	if (!message.has_value())
		return;

	// Then
	const std::expected<std::vector<std::byte>, ngui::Error> flattened = message->Flatten();
	NGUI_FUZZ_CHECK(flattened.has_value());
	NGUI_FUZZ_CHECK(std::vector<std::byte>(bytes.begin(), bytes.end()) == *flattened);
	const std::expected<ngui::BMessage, ngui::Error> again
		= ngui::BMessage::FromFlattened(*flattened);
	NGUI_FUZZ_CHECK(again.has_value() && *again == *message);
}
