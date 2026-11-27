/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "LocalSocket.h"

#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <winsock2.h>
#	include <afunix.h>
#else
#	include <fcntl.h>
#	include <poll.h>
#	include <sys/socket.h>
#	include <sys/stat.h>
#	include <sys/types.h>
#	include <sys/un.h>
#	include <unistd.h>
#	include <cerrno>
#endif


namespace ngui::remote::detail {


namespace {


#ifdef _WIN32
const NativeSocket kInvalid = static_cast<NativeSocket>(INVALID_SOCKET);


// Winsock is started once and never stopped: the process may use sockets
// until its end.
bool
StartWinsock()
{
	static const bool started = [] {
		WSADATA data;
		return WSAStartup(MAKEWORD(2, 2), &data) == 0;
	}();
	return started;
}


SOCKET
Handle(NativeSocket native)
{
	return static_cast<SOCKET>(native);
}


std::string
LastError()
{
	return std::system_category().message(WSAGetLastError());
}
#else
constexpr NativeSocket kInvalid = -1;


std::string
LastError()
{
	return std::system_category().message(errno);
}
#endif


// The address of \a path; InvalidArgument if it does not fit.
std::expected<sockaddr_un, Error>
AddressOf(const std::filesystem::path& path)
{
	sockaddr_un address{};
	address.sun_family = AF_UNIX;
	const std::string text = path.string();
	if (text.empty() || text.size() >= sizeof(address.sun_path))
		return Failure(ErrorCode::InvalidArgument, "the socket path is too long: " + text);
	std::memcpy(address.sun_path, text.c_str(), text.size() + 1);
	return address;
}


#ifdef __APPLE__
// What Linux sets when making a socket: closed on exec, and no SIGPIPE when
// the peer is gone (macOS has neither SOCK_CLOEXEC nor MSG_NOSIGNAL).
int
Prepared(int native)
{
	if (native >= 0) {
		(void)fcntl(native, F_SETFD, FD_CLOEXEC);
		const int on = 1;
		(void)setsockopt(native, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
	}
	return native;
}
#endif


NativeSocket
NewSocket()
{
#ifdef _WIN32
	if (!StartWinsock())
		return kInvalid;
	return static_cast<NativeSocket>(socket(AF_UNIX, SOCK_STREAM, 0));
#elif defined(__APPLE__)
	return Prepared(socket(AF_UNIX, SOCK_STREAM, 0));
#else
	return socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
#endif
}


// Whether \a native can be read (or accepted) within \a timeout.
std::expected<bool, Error>
WaitReadable(NativeSocket native, std::chrono::milliseconds timeout)
{
#ifdef _WIN32
	WSAPOLLFD entry{Handle(native), POLLRDNORM, 0};
	const int ready = WSAPoll(&entry, 1, static_cast<INT>(timeout.count()));
#else
	pollfd entry{native, POLLIN, 0};
	int ready = 0;
	do {
		ready = poll(&entry, 1, static_cast<int>(timeout.count()));
	} while (ready < 0 && errno == EINTR);
#endif
	if (ready < 0)
		return Failure(ErrorCode::IoError, "poll: " + LastError());
	return ready > 0;
}


void
CloseNative(NativeSocket native)
{
#ifdef _WIN32
	(void)closesocket(Handle(native));
#else
	(void)close(native);
#endif
}


} // namespace


Socket::Socket()
	:
	fNative(kInvalid)
{
}


Socket::Socket(NativeSocket native)
	:
	fNative(native)
{
}


Socket::~Socket()
{
	Close();
}


Socket::Socket(Socket&& other) noexcept
	:
	fNative(std::exchange(other.fNative, kInvalid))
{
}


Socket&
Socket::operator=(Socket&& other) noexcept
{
	if (this != &other) {
		Close();
		fNative = std::exchange(other.fNative, kInvalid);
	}
	return *this;
}


bool
Socket::IsValid() const
{
	return fNative != kInvalid;
}


void
Socket::Close()
{
	if (fNative != kInvalid)
		CloseNative(std::exchange(fNative, kInvalid));
}


std::expected<Socket, Error>
Socket::Listen(const std::filesystem::path& path)
{
	const auto address = AddressOf(path);
	if (!address.has_value())
		return std::unexpected(address.error());
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
	Socket listener(NewSocket());
	if (!listener.IsValid())
		return Failure(ErrorCode::IoError, "socket: " + LastError());
#ifdef _WIN32
	if (bind(Handle(listener.fNative), reinterpret_cast<const sockaddr*>(&*address),
			sizeof(*address)) != 0
		|| listen(Handle(listener.fNative), 4) != 0) {
		return Failure(ErrorCode::IoError, "bind " + path.string() + ": " + LastError());
	}
#else
	// created for the user only: no moment in which others may connect
	const mode_t previous = umask(0177);
	const int bound = bind(listener.fNative, reinterpret_cast<const sockaddr*>(&*address),
		sizeof(*address));
	(void)umask(previous);
	if (bound != 0 || chmod(path.c_str(), 0600) != 0 || listen(listener.fNative, 4) != 0)
		return Failure(ErrorCode::IoError, "bind " + path.string() + ": " + LastError());
#endif
	return listener;
}


std::expected<Socket, Error>
Socket::Connect(const std::filesystem::path& path)
{
	const auto address = AddressOf(path);
	if (!address.has_value())
		return std::unexpected(address.error());
	Socket connection(NewSocket());
	if (!connection.IsValid())
		return Failure(ErrorCode::IoError, "socket: " + LastError());
#ifdef _WIN32
	const int connected = connect(Handle(connection.fNative),
		reinterpret_cast<const sockaddr*>(&*address), sizeof(*address));
#else
	const int connected = connect(connection.fNative,
		reinterpret_cast<const sockaddr*>(&*address), sizeof(*address));
#endif
	if (connected != 0)
		return Failure(ErrorCode::NotFound, "connect " + path.string() + ": " + LastError());
	return connection;
}


std::expected<std::optional<Socket>, Error>
Socket::Accept(std::chrono::milliseconds timeout)
{
	const auto readable = WaitReadable(fNative, timeout);
	if (!readable.has_value())
		return std::unexpected(readable.error());
	if (!*readable)
		return std::nullopt;
#ifdef _WIN32
	Socket accepted(static_cast<NativeSocket>(accept(Handle(fNative), nullptr, nullptr)));
#elif defined(__APPLE__)
	Socket accepted(Prepared(accept(fNative, nullptr, nullptr)));
#else
	Socket accepted(accept4(fNative, nullptr, nullptr, SOCK_CLOEXEC));
#endif
	if (!accepted.IsValid())
		return Failure(ErrorCode::IoError, "accept: " + LastError());
	return std::optional<Socket>(std::move(accepted));
}


bool
Socket::PeerIsSameUser() const
{
#if defined(__linux__)
	ucred credentials{};
	socklen_t size = sizeof(credentials);
	if (getsockopt(fNative, SOL_SOCKET, SO_PEERCRED, &credentials, &size) != 0)
		return false;
	return credentials.uid == getuid();
#elif defined(__APPLE__) || defined(__FreeBSD__)
	uid_t user = 0;
	gid_t group = 0;
	if (getpeereid(fNative, &user, &group) != 0)
		return false;
	return user == getuid();
#else
	return true;
#endif
}


std::expected<void, Error>
Socket::WriteAll(std::span<const std::byte> bytes)
{
	while (!bytes.empty()) {
#ifdef _WIN32
		const int chunk = static_cast<int>(std::min<std::size_t>(bytes.size(), 1u << 20));
		const int sent = send(Handle(fNative), reinterpret_cast<const char*>(bytes.data()),
			chunk, 0);
#else
#	ifdef MSG_NOSIGNAL
		const ssize_t sent = send(fNative, bytes.data(), bytes.size(), MSG_NOSIGNAL);
#	else
		const ssize_t sent = send(fNative, bytes.data(), bytes.size(), 0);
#	endif
		if (sent < 0 && errno == EINTR)
			continue;
#endif
		if (sent <= 0)
			return Failure(ErrorCode::IoError, "send: " + LastError());
		bytes = bytes.subspan(static_cast<std::size_t>(sent));
	}
	return {};
}


std::expected<void, Error>
Socket::ReadExact(std::span<std::byte> bytes, std::chrono::milliseconds timeout,
	const std::atomic<bool>* stop)
{
	// in slices, so that \a stop is seen within a fifth of a second
	constexpr std::chrono::milliseconds kSlice(200);
	auto waited = std::chrono::milliseconds(0);
	while (!bytes.empty()) {
		if (stop != nullptr && stop->load())
			return Failure(ErrorCode::TimedOut, "stopped");
		const auto readable = WaitReadable(fNative, std::min(kSlice, timeout - waited));
		if (!readable.has_value())
			return std::unexpected(readable.error());
		if (!*readable) {
			waited += kSlice;
			if (waited >= timeout)
				return Failure(ErrorCode::TimedOut, "nothing to read");
			continue;
		}
#ifdef _WIN32
		const int chunk = static_cast<int>(std::min<std::size_t>(bytes.size(), 1u << 20));
		const int got = recv(Handle(fNative), reinterpret_cast<char*>(bytes.data()), chunk, 0);
#else
		const ssize_t got = recv(fNative, bytes.data(), bytes.size(), 0);
		if (got < 0 && errno == EINTR)
			continue;
#endif
		if (got == 0)
			return Failure(ErrorCode::NotFound, "the other end closed the connection");
		if (got < 0)
			return Failure(ErrorCode::IoError, "recv: " + LastError());
		bytes = bytes.subspan(static_cast<std::size_t>(got));
		waited = std::chrono::milliseconds(0);
	}
	return {};
}


std::expected<std::vector<std::byte>, Error>
EncodeFrame(const BMessage& message)
{
	auto flattened = message.Flatten();
	if (!flattened.has_value())
		return std::unexpected(flattened.error());
	if (flattened->size() > kMaxFrameSize)
		return Failure(ErrorCode::InvalidArgument, "the message is too large to send");
	const auto size = static_cast<std::uint32_t>(flattened->size());
	std::vector<std::byte> frame;
	frame.reserve(4 + flattened->size());
	for (int shift = 0; shift < 32; shift += 8)
		frame.push_back(static_cast<std::byte>((size >> shift) & 0xff));
	frame.insert(frame.end(), flattened->begin(), flattened->end());
	return frame;
}


namespace {


std::uint32_t
LengthOf(std::span<const std::byte> header)
{
	std::uint32_t size = 0;
	for (int i = 0; i < 4; i++)
		size |= static_cast<std::uint32_t>(header[static_cast<std::size_t>(i)]) << (8 * i);
	return size;
}


} // namespace


std::expected<BMessage, Error>
DecodeFrame(std::span<const std::byte> frame)
{
	if (frame.size() < 4)
		return Failure(ErrorCode::BadData, "a frame has a length");
	const std::uint32_t size = LengthOf(frame.first(4));
	if (size > kMaxFrameSize || size != frame.size() - 4)
		return Failure(ErrorCode::BadData, "the frame's length does not match");
	return BMessage::FromFlattened(frame.subspan(4));
}


std::expected<void, Error>
WriteFrame(Socket& socket, const BMessage& message)
{
	const auto frame = EncodeFrame(message);
	if (!frame.has_value())
		return std::unexpected(frame.error());
	return socket.WriteAll(*frame);
}


std::expected<BMessage, Error>
ReadFrame(Socket& socket, std::chrono::milliseconds timeout, const std::atomic<bool>* stop)
{
	std::vector<std::byte> frame(4);
	if (auto read = socket.ReadExact(frame, timeout, stop); !read)
		return std::unexpected(read.error());
	const std::uint32_t size = LengthOf(frame);
	if (size > kMaxFrameSize)
		return Failure(ErrorCode::BadData, "the frame is too large");
	frame.resize(4 + static_cast<std::size_t>(size));
	if (auto read = socket.ReadExact(std::span(frame).subspan(4), timeout, stop); !read)
		return std::unexpected(read.error());
	return DecodeFrame(frame);
}


} // namespace ngui::remote::detail
