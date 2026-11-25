/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/Message.h>


namespace ngui {


/*!	A window of the application when the recording began. */
struct RecordedWindow {
			std::string			title;		/*!< Its title then. */
};


/*!	The state of the application when a recording began (specification
	20.8): what a replay needs to begin alike. The windows' frames and scales
	are the first events of each window (kWindowMoved, kWindowResized). */
struct RecordingStart {
			std::string			signature;	/*!< The application's signature. */
			std::string			look;		/*!< The current look's id (LookRegistry). */
			std::vector<std::string> languages;
									/*!< The preferred languages (BLocaleRoster); a
										replay does not change them. */
			std::vector<RecordedWindow> windows;
									/*!< The windows, in the order they were made; a
										replay finds as many. */
};


/*!	An event of the platform as the application got it. */
struct RecordedEvent {
			Clock::Time			time{};		/*!< When, by Clock::Default(), since the
												recording began. */
			std::int32_t		window = -1;
									/*!< Its window: the windows of the start are 0 to n -
										1, the windows made later n, n + 1, ... in the
										order they were made; -1 the application. */
			std::int32_t		popup = -1;	/*!< The index of the window's open popup (a
												menu) it went to, -1 for the window. */
			bool				masked = false;
									/*!< Whether what it typed was masked (rule T5): its
										characters are bullets, its key code 0. */
			BMessage			message;	/*!< The event (kMouseDown, kKeyDown,
												kWindowResized, kQuitRequested, ...). */
};


/*!	A recording of an application's input (specification 20.8): the state
	at its start and the events of the platform since, with the times of
	Clock::Default(). BApplication::StartRecording() writes one;
	test::Driver::Replay() plays it back headless with the virtual clock, so
	that a bug found in use becomes a regression test.

	The file is a sequence of frames, each a little-endian uint32 length and
	a flattened BMessage: the start ('NRCS') and then one per event ('NRCE').
	A recording ends where a frame is cut off (the application crashed
	while writing it); a damaged frame before that is BadData. Typed text is
	in it, but never into password fields or views marked private
	(BView::SetInputPrivate(); rule T5).
*/
struct Recording {
			RecordingStart		start;		/*!< The state at the start. */
			std::vector<RecordedEvent> events;
									/*!< The events, in the order they were dispatched. */

	static	std::expected<Recording, Error> Read(std::span<const std::byte> bytes);
									/*!< The recording in \a bytes: BadData if it does not
										begin with a start frame or a whole frame is no
										part of a recording. */
	static	std::expected<Recording, Error> Load(const std::filesystem::path& path);
									/*!< Read() of the file \a path; IoError if it cannot be
										read. */
			std::expected<std::vector<std::byte>, Error> Write() const;
									/*!< Its frames, as Read() takes them. */
			std::expected<void, Error> Save(const std::filesystem::path& path) const;
									/*!< Write() into the file \a path. */
};


} // namespace ngui
