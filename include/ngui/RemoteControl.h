/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Message.h>


/*!	Remote control (specification 20.7, module M26): scripting messages from
	another process of the same user, over a local socket.

	**Rule T4:** an application listens only after it is told to,
	BApplication::EnableRemoteControl() or the environment variable
	NGUI_REMOTE_CONTROL=1. The socket is a Unix domain socket (AF_UNIX, on
	Windows since Windows 10 1803) in a directory of the user's own
	(DefaultDirectory()): never reachable over the network, and only by the
	user the application runs as (directory 0700, socket 0600, and on Linux
	and macOS the peer's user id is checked).

	The protocol: a request is a frame, a little-endian uint32 length and a
	flattened BMessage; the application answers each with a frame of the
	reply. Only scripting messages are taken (kGetProperty, kSetProperty,
	kCreateProperty, kDeleteProperty, kCountProperties, kExecuteProperty,
	kGetSupportedSuites); anything else is answered kMessageNotUnderstood.
*/
namespace ngui::remote {


/*!	The directory of the sockets of the user's applications: on Linux
	$XDG_RUNTIME_DIR/ngui, otherwise /tmp/ngui-\<uid\>; on Windows
	%LOCALAPPDATA%\\ngui\\remote. Made with mode 0700 if missing; refused
	(InvalidArgument) if it exists but is not the user's own and private. */
std::expected<std::filesystem::path, Error> DefaultDirectory();


/*!	A running application that has remote control enabled. */
struct Endpoint {
			std::string			signature;	/*!< Its signature, as in its socket's name. */
			std::int64_t		process;	/*!< Its process id. */
			std::filesystem::path path;		/*!< Its socket. */
};


/*!	The socket of the application \a signature in process \a process in
	\a directory: "ngui-\<signature, / as _\>-\<process\>.sock". */
std::filesystem::path			EndpointPath(const std::filesystem::path& directory,
									std::string_view signature, std::int64_t process);

/*!	The applications with remote control in \a directory (DefaultDirectory()
	if empty) whose processes still run, in order of their paths. */
std::vector<Endpoint>			ListApplications(const std::filesystem::path& directory = {});


/*!	A connection to an application's remote control. */
class Client {
public:
								~Client();
								Client(Client&& other) noexcept;
									/*!< Takes the connection. */
			Client&				operator=(Client&& other) noexcept;
									/*!< Takes the connection. */

	static	std::expected<Client, Error> Connect(const std::filesystem::path& path);
									/*!< Connects to the socket \a path; NotFound if no
										application listens there. */
			std::expected<BMessage, Error> Send(const BMessage& message,
									std::chrono::milliseconds timeout
										= std::chrono::seconds(10));
									/*!< Sends \a message and waits for the reply; TimedOut
										after \a timeout, NotFound if the application went
										away. */

private:
								Client();

			struct Data;
			std::unique_ptr<Data> fData;
};


} // namespace ngui::remote
