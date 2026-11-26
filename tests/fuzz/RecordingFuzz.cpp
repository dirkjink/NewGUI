/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <span>

#include <ngui/Recording.h>

#include "record/Recorder.h"


NGUI_FUZZ_TARGET(Recording)
{
	// What is tested: Recording::Read() of arbitrary bytes; of what it
	// reads, Write() and Read() again, and MaskTyping() of each event, once
	// and twice.
	// Success criteria: Nothing crashes (checked by the sanitizers); a
	// failure is BadData; what was read is written and read back with as
	// many events and windows; masking twice is masking once.
	// Why it matters: Recordings come from other machines and from crashes
	// (specification 20.8); no file may crash a test that replays it.
	//
	// When
	const auto recording = ngui::Recording::Read(std::as_bytes(data));

	// Then
	if (!recording.has_value()) {
		NGUI_FUZZ_CHECK(recording.error().code == ngui::ErrorCode::BadData);
		return;
	}
	const auto bytes = recording->Write();
	NGUI_FUZZ_CHECK(bytes.has_value());
	if (!bytes.has_value())
		return;
	const auto again = ngui::Recording::Read(*bytes);
	NGUI_FUZZ_CHECK(again.has_value());
	if (!again.has_value())
		return;
	NGUI_FUZZ_CHECK(again->events.size() == recording->events.size());
	NGUI_FUZZ_CHECK(again->start.windows.size() == recording->start.windows.size());
	for (const ngui::RecordedEvent& event : recording->events) {
		const ngui::BMessage once = ngui::detail::MaskTyping(event.message);
		const ngui::BMessage twice = ngui::detail::MaskTyping(once);
		NGUI_FUZZ_CHECK(once.Flatten() == twice.Flatten());
	}
}
