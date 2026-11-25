/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/RemoteControl.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <system_error>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/PropertyInfo.h>

#include "Environment.h"
#include "LocalSocket.h"
#include "RemoteServer.h"

#ifdef _WIN32
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <windows.h>
// windows.h makes SendMessage a macro; BMessenger has a method of that name
#	undef SendMessage
#else
#	include <sys/stat.h>
#	include <unistd.h>
#	include <cerrno>
#	include <csignal>
#endif


namespace ngui::remote {


namespace {


using namespace std::chrono_literals;


// How long the application may take to answer one request.
constexpr auto kReplyTimeout = 10s;

// How long a connection may stay silent before it is closed.
constexpr auto kIdleTimeout = 60s;


#ifndef _WIN32
// \a directory, made with mode 0700 if missing; InvalidArgument unless it
// is a directory of the user's that no one else may enter.
std::expected<std::filesystem::path, Error>
PrivateDirectory(const std::filesystem::path& directory)
{
	if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
		return Failure(ErrorCode::IoError, "cannot make " + directory.string());
	struct stat status{};
	if (lstat(directory.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)
		|| status.st_uid != getuid() || (status.st_mode & 0077) != 0) {
		return Failure(ErrorCode::InvalidArgument, directory.string()
			+ " is not a private directory of the user's");
	}
	return directory;
}
#endif


bool
IsRunning(std::int64_t process)
{
	if (process <= 0)
		return false;
#ifdef _WIN32
	HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
		static_cast<DWORD>(process));
	if (handle == nullptr)
		return false;
	DWORD code = 0;
	const bool running = GetExitCodeProcess(handle, &code) != 0 && code == STILL_ACTIVE;
	CloseHandle(handle);
	return running;
#else
	return kill(static_cast<pid_t>(process), 0) == 0 || errno == EPERM;
#endif
}


} // namespace


std::expected<std::filesystem::path, Error>
DefaultDirectory()
{
#ifdef _WIN32
	const auto local = ngui::detail::EnvironmentVariable("LOCALAPPDATA");
	if (!local.has_value() || local->empty())
		return Failure(ErrorCode::NotFound, "no LOCALAPPDATA");
	std::filesystem::path directory = std::filesystem::path(*local) / "ngui" / "remote";
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
		return Failure(ErrorCode::IoError, "cannot make " + directory.string());
	return directory;
#else
	if (const auto runtime = ngui::detail::EnvironmentVariable("XDG_RUNTIME_DIR");
			runtime.has_value() && !runtime->empty()) {
		return PrivateDirectory(std::filesystem::path(*runtime) / "ngui");
	}
	return PrivateDirectory(std::filesystem::path("/tmp")
		/ ("ngui-" + std::to_string(getuid())));
#endif
}


std::filesystem::path
EndpointPath(const std::filesystem::path& directory, std::string_view signature,
	std::int64_t process)
{
	std::string name(signature);
	std::ranges::replace(name, '/', '_');
	return directory / ("ngui-" + name + "-" + std::to_string(process) + ".sock");
}


std::vector<Endpoint>
ListApplications(const std::filesystem::path& directory)
{
	std::filesystem::path where = directory;
	if (where.empty()) {
		const auto found = DefaultDirectory();
		if (!found.has_value())
			return {};
		where = *found;
	}
	std::vector<Endpoint> endpoints;
	std::error_code error;
	for (const auto& entry : std::filesystem::directory_iterator(where, error)) {
		const std::string name = entry.path().filename().string();
		if (!name.starts_with("ngui-") || !name.ends_with(".sock"))
			continue;
		const std::string_view stem = std::string_view(name).substr(5, name.size() - 10);
		const std::size_t dash = stem.rfind('-');
		if (dash == std::string_view::npos)
			continue;
		std::int64_t process = 0;
		const std::string_view digits = stem.substr(dash + 1);
		const auto [end, parsed] = std::from_chars(digits.data(),
			digits.data() + digits.size(), process);
		if (parsed != std::errc() || end != digits.data() + digits.size() || !IsRunning(process))
			continue;
		// a signature has one slash, between type and subtype; the type has no
		// underscore
		std::string signature(stem.substr(0, dash));
		if (const std::size_t slash = signature.find('_'); slash != std::string::npos)
			signature[slash] = '/';
		endpoints.push_back({std::move(signature), process, entry.path()});
	}
	std::ranges::sort(endpoints, {}, &Endpoint::path);
	return endpoints;
}


// #pragma mark - Client


struct Client::Data {
	detail::Socket	socket;
};


Client::Client()
	:
	fData(std::make_unique<Data>())
{
}


Client::~Client() = default;
Client::Client(Client&& other) noexcept = default;
Client& Client::operator=(Client&& other) noexcept = default;


std::expected<Client, Error>
Client::Connect(const std::filesystem::path& path)
{
	auto socket = detail::Socket::Connect(path);
	if (!socket.has_value())
		return std::unexpected(socket.error());
	Client client;
	client.fData->socket = std::move(*socket);
	return client;
}


std::expected<BMessage, Error>
Client::Send(const BMessage& message, std::chrono::milliseconds timeout)
{
	if (!fData->socket.IsValid())
		return Failure(ErrorCode::NotFound, "not connected");
	if (auto written = detail::WriteFrame(fData->socket, message); !written)
		return std::unexpected(written.error());
	return detail::ReadFrame(fData->socket, timeout);
}


// #pragma mark - Server


namespace detail {


bool
IsRemoteCommand(std::uint32_t what)
{
	return what == kGetProperty || what == kSetProperty || what == kCreateProperty
		|| what == kDeleteProperty || what == kCountProperties || what == kExecuteProperty
		|| what == kGetSupportedSuites;
}


RemoteServer::RemoteServer(std::filesystem::path path, Socket listener, BMessenger application)
	:
	fPath(std::move(path)),
	fListener(std::move(listener)),
	fApplication(std::move(application))
{
}


std::expected<std::unique_ptr<RemoteServer>, Error>
RemoteServer::Start(const std::filesystem::path& path, const BMessenger& application)
{
	auto listener = Socket::Listen(path);
	if (!listener.has_value())
		return std::unexpected(listener.error());
	std::unique_ptr<RemoteServer> server(new RemoteServer(path, std::move(*listener),
		application));
	server->fThread = std::thread([raw = server.get()] { raw->_Run(); });
	return server;
}


RemoteServer::~RemoteServer()
{
	fStop = true;
	if (fThread.joinable())
		fThread.join();
	fListener.Close();
	std::error_code ignored;
	std::filesystem::remove(fPath, ignored);
}


const std::filesystem::path&
RemoteServer::Path() const
{
	return fPath;
}


// One connection at a time: remote control is a person's tool or a test's,
// not a server for many.
void
RemoteServer::_Run()
{
	while (!fStop.load()) {
		auto accepted = fListener.Accept(200ms);
		if (!accepted.has_value())
			return;
		if (!accepted->has_value())
			continue;
		Socket connection = std::move(**accepted);
		if (!connection.PeerIsSameUser())
			continue;
		_Serve(connection);
	}
}


void
RemoteServer::_Serve(Socket& connection)
{
	while (!fStop.load()) {
		auto request = ReadFrame(connection, kIdleTimeout, &fStop);
		if (!request.has_value()) {
			// an answer for a frame that is no message; then close
			if (request.error().code == ErrorCode::BadData) {
				BMessage reply(kMessageNotUnderstood);
				(void)reply.AddInt32("error", ScriptingStatus(std::unexpected(request.error())));
				(void)reply.AddString("message", request.error().message);
				(void)WriteFrame(connection, reply);
			}
			return;
		}
		BMessage reply(kMessageNotUnderstood);
		if (!IsRemoteCommand(request->what)) {
			(void)reply.AddInt32("error", ScriptingStatus(std::unexpected(Error{
				ErrorCode::NotSupported, ""})));
			(void)reply.AddString("message", "remote control takes scripting messages only");
		} else if (auto sent = fApplication.SendMessage(*request, &reply, kReplyTimeout);
				!sent) {
			reply = BMessage(kMessageNotUnderstood);
			(void)reply.AddInt32("error", ScriptingStatus(std::unexpected(sent.error())));
			(void)reply.AddString("message", sent.error().message);
		}
		if (!WriteFrame(connection, reply).has_value())
			return;
	}
}


} // namespace detail


} // namespace ngui::remote
