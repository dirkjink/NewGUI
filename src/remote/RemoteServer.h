/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The application's side of remote control (specification 20.7): a thread
// that accepts connections on the socket and passes their scripting
// messages to the application.


#include <atomic>
#include <expected>
#include <filesystem>
#include <thread>

#include <ngui/Error.h>
#include <ngui/Messenger.h>

#include "LocalSocket.h"


namespace ngui::remote::detail {


// Whether \a what is a scripting command remote control passes on.
bool IsRemoteCommand(std::uint32_t what);


class RemoteServer {
public:
	// Listens at \a path and passes requests to \a application.
	static	std::expected<std::unique_ptr<RemoteServer>, Error> Start(
									const std::filesystem::path& path,
									const BMessenger& application);
								~RemoteServer();
									// stops the thread and removes the socket

			const std::filesystem::path& Path() const;

private:
								RemoteServer(std::filesystem::path path, Socket listener,
									BMessenger application);
			void				_Run();
			void				_Serve(Socket& connection);

			std::filesystem::path fPath;
			Socket				fListener;
			BMessenger			fApplication;
			std::atomic<bool>	fStop = false;
			std::thread			fThread;
};


} // namespace ngui::remote::detail
