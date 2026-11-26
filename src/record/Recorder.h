/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Record & Replay (specification 20.8): the recorder of the process, which
// the loopers hand the events of the platform (PlatformEvent.h) as they
// dispatch them, and the frames of a recording.


#include <cstdint>
#include <expected>
#include <filesystem>

#include <ngui/AppDefs.h>
#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/Recording.h>


namespace ngui {
class BHandler;
class BLooper;
class BWindow;
}


namespace ngui::detail {


// The 'what' of the frames.
inline constexpr std::uint32_t kRecordingStart = FourCharCode("NRCS");
inline constexpr std::uint32_t kRecordedEvent = FourCharCode("NRCE");
// Asks a window to record its frame and scale (as kWindowMoved and
// kWindowResized) when a recording begins.
inline constexpr std::uint32_t kRecordWindowState = FourCharCode("_RWS");
// The version of the frames.
inline constexpr std::int32_t kRecordingFormat = 1;


// Begins recording into \a path with \a start; the windows are numbered
// from their serials now. InvalidArgument while recording, IoError if the
// file cannot be written.
std::expected<void, Error> StartRecorder(const std::filesystem::path& path,
	const RecordingStart& start);
// Ends the recording and closes its file.
void StopRecorder();
bool IsRecording();

// Records the platform's event \a message, which \a looper dispatches to
// \a target, on the looper's thread.
void RecordPlatformEvent(BLooper& looper, const BHandler* target, const BMessage& message);
// Records the frame and scale of \a window as its events kWindowMoved and
// kWindowResized, on its thread.
void RecordWindowState(BWindow& window, const gfx::Rect& frame, float scale);

// \a event with what it types masked (rule T5): each character of "bytes"
// or of an input method's "be:string" a bullet, the key code 0; keys with
// Command, Control or Option (shortcuts) and keys that type no character
// (Backspace, arrows, Enter) stay.
BMessage MaskTyping(const BMessage& event);
// Whether \a what is an event that types (keys, input methods).
bool IsTyping(std::uint32_t what);

// The frames of a recording.
BMessage StartMessage(const RecordingStart& start);
BMessage EventMessage(const RecordedEvent& event);


} // namespace ngui::detail
