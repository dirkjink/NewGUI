/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Record & Replay (specification 20.8): the recorder of the process and the
// file format of ngui/Recording.h.


#include <ngui/Recording.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <ngui/InterfaceDefs.h>
#include <ngui/Looper.h>
#include <ngui/Window.h>

#include "WindowList.h"
#include "record/Recorder.h"
#include "remote/LocalSocket.h"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif


namespace ngui::detail {


namespace {


struct Recorder {
			std::mutex			lock;
			std::FILE*			file = nullptr;
			Clock::Time			begin{};
			std::vector<std::uint64_t> startSerials;
									// the windows of the start, numbered 0 to n - 1
			std::uint64_t		firstNewSerial = 0;
									// the serial of window n

								~Recorder()
								{
									if (file != nullptr)
										(void)std::fclose(file);
								}
};


std::atomic<std::shared_ptr<Recorder>> sRecorder;


// The number of the window with \a serial in the recording.
std::int32_t
WindowNumber(const Recorder& recorder, std::uint64_t serial)
{
	const auto found = std::ranges::find(recorder.startSerials, serial);
	if (found != recorder.startSerials.end())
		return static_cast<std::int32_t>(found - recorder.startSerials.begin());
	if (serial < recorder.firstNewSerial)
		return -1;
	const std::uint64_t number = recorder.startSerials.size() + (serial - recorder.firstNewSerial);
	return static_cast<std::int32_t>(std::min<std::uint64_t>(number,
		std::numeric_limits<std::int32_t>::max()));
}


void
Write(Recorder& recorder, const RecordedEvent& event)
{
	const auto frame = remote::detail::EncodeFrame(EventMessage(event));
	if (!frame.has_value())
		return;
	std::scoped_lock lock(recorder.lock);
	if (recorder.file == nullptr)
		return;
	// at once, so that a crash loses at most the event in hand
	(void)std::fwrite(frame->data(), 1, frame->size(), recorder.file);
	(void)std::fflush(recorder.file);
}


// A file only the user may read (it may hold what they typed): created with
// mode 0600 on POSIX; on Windows the user's profile keeps others out.
std::FILE*
OpenPrivate(const std::filesystem::path& path)
{
#ifdef _WIN32
	return _wfopen(path.c_str(), L"wb");
#else
	const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return nullptr;
	// an existing file keeps its mode: make it private as well
	(void)fchmod(fd, 0600);
	std::FILE* file = fdopen(fd, "wb");
	if (file == nullptr)
		(void)close(fd);
	return file;
#endif
}


// \a text with each character a bullet, its control characters (keys that
// type none) as they are.
std::string
Bullets(std::string_view text)
{
	std::string masked;
	for (std::size_t i = 0; i < text.size(); i++) {
		const auto byte = static_cast<unsigned char>(text[i]);
		if (byte < 0x20 || byte == 0x7f)
			masked += static_cast<char>(byte);
		else if (i == 0 || (byte & 0xc0) != 0x80)
			masked += "•";
	}
	return masked;
}


// The number of items of the field \a name, 0 without it.
std::int32_t
Count(const BMessage& message, std::string_view name)
{
	const auto info = message.GetInfo(name);
	return info.has_value() ? info->count : 0;
}


std::expected<BMessage, Error>
DecodeAt(std::span<const std::byte> bytes, std::size_t* offset, bool* end)
{
	*end = false;
	const std::size_t left = bytes.size() - *offset;
	if (left < 4) {
		*end = true;
		return BMessage();
	}
	std::uint32_t size = 0;
	for (int i = 3; i >= 0; i--)
		size = (size << 8) | std::to_integer<std::uint32_t>(bytes[*offset + i]);
	if (size > remote::detail::kMaxFrameSize)
		return Failure(ErrorCode::BadData, "a frame of the recording is too large");
	if (left - 4 < size) {
		// cut off: where the recording ends
		*end = true;
		return BMessage();
	}
	auto message = remote::detail::DecodeFrame(bytes.subspan(*offset, 4 + std::size_t{size}));
	*offset += 4 + std::size_t{size};
	if (!message.has_value())
		return Failure(ErrorCode::BadData, "a frame of the recording is damaged");
	return message;
}


} // namespace


std::expected<void, Error>
StartRecorder(const std::filesystem::path& path, const RecordingStart& start)
{
	if (sRecorder.load() != nullptr)
		return Failure(ErrorCode::InvalidArgument, "a recording is running");
	auto recorder = std::make_shared<Recorder>();
	recorder->file = OpenPrivate(path);
	if (recorder->file == nullptr) {
		return Failure(ErrorCode::IoError, "cannot write " + path.string() + ": "
			+ std::error_code(errno, std::generic_category()).message());
	}
	const auto frame = remote::detail::EncodeFrame(StartMessage(start));
	if (!frame.has_value())
		return std::unexpected(frame.error());
	if (std::fwrite(frame->data(), 1, frame->size(), recorder->file) != frame->size()
		|| std::fflush(recorder->file) != 0) {
		return Failure(ErrorCode::IoError, "cannot write " + path.string());
	}

	const std::vector<WindowEntry> windows = WindowEntries();
	for (const WindowEntry& window : windows)
		recorder->startSerials.push_back(window.serial);
	recorder->firstNewSerial = NextWindowSerial();
	recorder->begin = Clock::Default().Now();
	std::shared_ptr<Recorder> none;
	if (!sRecorder.compare_exchange_strong(none, recorder))
		return Failure(ErrorCode::InvalidArgument, "a recording is running");
	// the windows' frames and scales as their first events
	for (const WindowEntry& window : windows)
		(void)window.messenger.SendMessage(kRecordWindowState);
	return {};
}


void
StopRecorder()
{
	const std::shared_ptr<Recorder> recorder = sRecorder.exchange(nullptr);
	if (recorder == nullptr)
		return;
	std::scoped_lock lock(recorder->lock);
	if (recorder->file != nullptr)
		(void)std::fclose(recorder->file);
	recorder->file = nullptr;
}


bool
IsRecording()
{
	return sRecorder.load() != nullptr;
}


void
RecordPlatformEvent(BLooper& looper, const BHandler* target, const BMessage& message)
{
	const std::shared_ptr<Recorder> recorder = sRecorder.load();
	if (recorder == nullptr)
		return;
	RecordedEvent event;
	event.time = Clock::Default().Now() - recorder->begin;
	if (const auto* window = dynamic_cast<const BWindow*>(&looper)) {
		event.window = WindowNumber(*recorder, ngui::WindowAccess::Serial(*window));
		if (event.window < 0)
			return;
		event.popup = ngui::WindowAccess::PopupIndex(*window, target);
		event.masked = IsTyping(message.what) && ngui::WindowAccess::MasksInput(*window);
	}
	event.message = event.masked ? MaskTyping(message) : message;
	Write(*recorder, event);
}


void
RecordWindowState(BWindow& window, const gfx::Rect& frame, float scale)
{
	const std::shared_ptr<Recorder> recorder = sRecorder.load();
	if (recorder == nullptr)
		return;
	RecordedEvent event;
	event.time = Clock::Default().Now() - recorder->begin;
	event.window = WindowNumber(*recorder, ngui::WindowAccess::Serial(window));
	if (event.window < 0)
		return;
	event.message = BMessage(kWindowMoved);
	(void)event.message.AddPoint("where", {frame.x0, frame.y0});
	Write(*recorder, event);
	event.message = BMessage(kWindowResized);
	(void)event.message.AddFloat("width", frame.Width());
	(void)event.message.AddFloat("height", frame.Height());
	(void)event.message.AddFloat("be:scale", scale);
	Write(*recorder, event);
}


bool
IsTyping(std::uint32_t what)
{
	return what == kKeyDown || what == kKeyUp || what == kInputMethodEvent;
}


BMessage
MaskTyping(const BMessage& event)
{
	BMessage masked(event);
	if (event.what == kKeyDown || event.what == kKeyUp) {
		const auto modifiers = BModifierFlags::FromRaw(
			static_cast<std::uint32_t>(event.GetInt32("modifiers", 0)));
		const bool shortcut = modifiers.Has(BModifier::Command)
			|| modifiers.Has(BModifier::Control) || modifiers.Has(BModifier::Option);
		const std::int32_t count = Count(event, "bytes");
		for (std::int32_t i = 0; i < count && !shortcut; i++) {
			const std::string bytes(event.FindString("bytes", i).value_or(""));
			(void)masked.ReplaceString("bytes", i, Bullets(bytes));
		}
		if (event.HasInt32("key"))
			(void)masked.ReplaceInt32("key", 0);
	} else if (event.what == kInputMethodEvent) {
		if (const auto text = event.FindString("be:string")) {
			const std::string original(*text);
			(void)masked.ReplaceString("be:string", Bullets(original));
			// the selection's byte offsets into the masked text
			const std::int32_t count = Count(event, "be:selection");
			for (std::int32_t i = 0; i < count; i++) {
				const std::int32_t offset = event.FindInt32("be:selection", i).value_or(0);
				const std::size_t clamped = static_cast<std::size_t>(std::clamp<std::int32_t>(
					offset, 0, static_cast<std::int32_t>(original.size())));
				(void)masked.ReplaceInt32("be:selection", i,
					static_cast<std::int32_t>(Bullets(original.substr(0, clamped)).size()));
			}
		}
	}
	return masked;
}


BMessage
StartMessage(const RecordingStart& start)
{
	BMessage message(kRecordingStart);
	(void)message.AddInt32("format", kRecordingFormat);
	(void)message.AddString("signature", start.signature);
	(void)message.AddString("look", start.look);
	for (const std::string& language : start.languages)
		(void)message.AddString("language", language);
	for (const RecordedWindow& window : start.windows)
		(void)message.AddString("window", window.title);
	return message;
}


BMessage
EventMessage(const RecordedEvent& event)
{
	BMessage message(kRecordedEvent);
	(void)message.AddInt64("time", event.time.count());
	(void)message.AddInt32("window", event.window);
	(void)message.AddInt32("popup", event.popup);
	(void)message.AddBool("masked", event.masked);
	(void)message.AddMessage("event", event.message);
	return message;
}


} // namespace ngui::detail


namespace ngui {


std::expected<Recording, Error>
Recording::Read(std::span<const std::byte> bytes)
{
	Recording recording;
	std::size_t offset = 0;
	bool end = false;
	auto first = detail::DecodeAt(bytes, &offset, &end);
	if (!first.has_value())
		return std::unexpected(first.error());
	if (end || first->what != detail::kRecordingStart)
		return Failure(ErrorCode::BadData, "no recording: its start is missing");
	if (first->GetInt32("format", 0) != detail::kRecordingFormat)
		return Failure(ErrorCode::BadData, "a recording of another format");
	recording.start.signature = first->GetString("signature", "");
	recording.start.look = first->GetString("look", "");
	for (std::int32_t i = 0; i < detail::Count(*first, "language"); i++)
		recording.start.languages.emplace_back(first->FindString("language", i).value_or(""));
	for (std::int32_t i = 0; i < detail::Count(*first, "window"); i++) {
		recording.start.windows.push_back(
			{std::string(first->FindString("window", i).value_or(""))});
	}

	while (true) {
		auto frame = detail::DecodeAt(bytes, &offset, &end);
		if (!frame.has_value())
			return std::unexpected(frame.error());
		if (end)
			break;
		auto event = frame->FindMessage("event");
		if (frame->what != detail::kRecordedEvent || !event.has_value())
			return Failure(ErrorCode::BadData, "a frame of the recording is no event");
		RecordedEvent recorded;
		recorded.time = Clock::Time(frame->GetInt64("time", 0));
		recorded.window = frame->GetInt32("window", -1);
		recorded.popup = frame->GetInt32("popup", -1);
		recorded.masked = frame->GetBool("masked", false);
		recorded.message = std::move(*event);
		recording.events.push_back(std::move(recorded));
	}
	return recording;
}


std::expected<Recording, Error>
Recording::Load(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return Failure(ErrorCode::IoError, "cannot read " + path.string());
	std::vector<char> chars((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	if (file.bad())
		return Failure(ErrorCode::IoError, "cannot read " + path.string());
	return Read(std::as_bytes(std::span(chars)));
}


std::expected<std::vector<std::byte>, Error>
Recording::Write() const
{
	std::vector<std::byte> bytes;
	auto frame = remote::detail::EncodeFrame(detail::StartMessage(start));
	if (!frame.has_value())
		return std::unexpected(frame.error());
	bytes.insert(bytes.end(), frame->begin(), frame->end());
	for (const RecordedEvent& event : events) {
		frame = remote::detail::EncodeFrame(detail::EventMessage(event));
		if (!frame.has_value())
			return std::unexpected(frame.error());
		bytes.insert(bytes.end(), frame->begin(), frame->end());
	}
	return bytes;
}


std::expected<void, Error>
Recording::Save(const std::filesystem::path& path) const
{
	const auto bytes = Write();
	if (!bytes.has_value())
		return std::unexpected(bytes.error());
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	if (file)
		file.write(reinterpret_cast<const char*>(bytes->data()),
			static_cast<std::streamsize>(bytes->size()));
	if (!file)
		return Failure(ErrorCode::IoError, "cannot write " + path.string());
	return {};
}


} // namespace ngui
