/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// A local stream socket (AF_UNIX: Unix domain sockets, on Windows since
// Windows 10 1803) for remote control (specification 20.7, rule T4), and
// the frames of the protocol: a little-endian uint32 length, then a
// flattened BMessage.


#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Message.h>


namespace ngui::remote::detail {


#ifdef _WIN32
using NativeSocket = std::uintptr_t;
#else
using NativeSocket = int;
#endif


// The largest frame read: a scripting message is small, and a client must
// not make the application allocate what it likes.
inline constexpr std::uint32_t kMaxFrameSize = 16u * 1024 * 1024;


class Socket {
public:
								Socket();
	explicit					Socket(NativeSocket native);
								~Socket();

								Socket(Socket&& other) noexcept;
			Socket&				operator=(Socket&& other) noexcept;
								Socket(const Socket&) = delete;
			Socket&				operator=(const Socket&) = delete;

	// A listening socket at \a path, which must not be longer than the
	// platform allows (about 100 bytes); an old file there is removed.
	// Only the user may connect: on POSIX the file gets mode 0600.
	static	std::expected<Socket, Error> Listen(const std::filesystem::path& path);
	static	std::expected<Socket, Error> Connect(const std::filesystem::path& path);

			bool				IsValid() const;
			void				Close();

	// A connection, or nullopt if none came within \a timeout.
			std::expected<std::optional<Socket>, Error> Accept(
									std::chrono::milliseconds timeout);
	// Whether the other end runs as the same user (SO_PEERCRED,
	// getpeereid()); true where the platform cannot tell (Windows: the
	// socket's directory in the user's profile keeps others out).
			bool				PeerIsSameUser() const;

			std::expected<void, Error> WriteAll(std::span<const std::byte> bytes);
	// Reads exactly \a bytes.size() bytes; TimedOut if nothing came for
	// \a timeout, NotFound if the other end closed, TimedOut as well if
	// \a stop becomes true.
			std::expected<void, Error> ReadExact(std::span<std::byte> bytes,
									std::chrono::milliseconds timeout,
									const std::atomic<bool>* stop = nullptr);

private:
			NativeSocket		fNative;
};


// \a message as a frame.
std::expected<std::vector<std::byte>, Error> EncodeFrame(const BMessage& message);

// The message of the frame \a frame (length and payload): BadData if the
// length does not match or is too large, or the payload is no message.
std::expected<BMessage, Error> DecodeFrame(std::span<const std::byte> frame);

std::expected<void, Error> WriteFrame(Socket& socket, const BMessage& message);
std::expected<BMessage, Error> ReadFrame(Socket& socket, std::chrono::milliseconds timeout,
	const std::atomic<bool>* stop = nullptr);


} // namespace ngui::remote::detail
