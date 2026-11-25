/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform {


/*!	The window systems a program can run on, by name (module P1,
	specification 20.2): "headless" is always there; SdlPlatform::Register()
	adds "sdl3". BApplication::UsePlatform(name) creates one from
	Default(). A platform with a negative priority is only created by its
	name ("headless": a program without a display must not run unseen).
	Thread-safe.
*/
class PlatformRegistry {
public:
	/*!	Creates a new platform; an error if it cannot (no display ...). */
	using Factory = std::function<std::expected<std::unique_ptr<Platform>, Error>()>;

								PlatformRegistry() = default;
									/*!< An empty registry; normally Default() is used. */
								PlatformRegistry(const PlatformRegistry&) = delete;
									/*!< Not copyable. */
			PlatformRegistry&	operator=(const PlatformRegistry&) = delete;
									/*!< Not copyable. */

	static	PlatformRegistry&	Default();
									/*!< The registry of the process, with "headless" at
										priority -1. */

			bool				Register(std::string name, int priority, Factory factory);
									/*!< Adds a platform; false if \a name is taken or empty
										or \a factory is empty. */
			std::vector<std::string> Names() const;
									/*!< The registered names, highest priority first. */
			std::expected<std::unique_ptr<Platform>, Error> Create(std::string_view name) const;
									/*!< A new platform \a name; NotFound if there is
										none, else its factory's result. */
			std::expected<std::unique_ptr<Platform>, Error> CreatePreferred(
									std::string_view preferred = {}) const;
									/*!< The platform \a preferred, else the one the
										environment variable NGUI_PLATFORM names (tests
										run with "headless"), else the first of priority
										0 or more that can be created. NotFound (with the
										last error) if none can. */

private:
			struct Entry {
				std::string		name;
				int				priority;
				Factory			factory;
			};

	mutable	std::mutex			fLock;
			std::vector<Entry>	fEntries;
};


} // namespace ngui::platform
