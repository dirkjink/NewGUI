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
#include <ngui/gfx/Backend.h>


namespace ngui::gfx {


/*!	The backends a program can choose from (specification 12, M11): each
	registered with a name and a priority. The default registry always has
	"dump" (DumpBackend) with priority 0; Skia and Blend2D register
	themselves with higher priorities when they are built in.
*/
class BackendRegistry {
public:
	/*!	Creates a new backend. */
	using Factory = std::function<std::unique_ptr<Backend>()>;

								BackendRegistry() = default;
									/*!< An empty registry; normally Default() is used. */

								BackendRegistry(const BackendRegistry&) = delete;
									/*!< Not copyable. */
			BackendRegistry&	operator=(const BackendRegistry&) = delete;
									/*!< Not copyable. */

	static	BackendRegistry&	Default();
									/*!< The registry of the process, with "dump". */

			bool				Register(std::string name, int priority, Factory factory);
									/*!< Adds a backend; false if \a name is taken or empty or
										\a factory is empty. */
			std::vector<std::string> Names() const;
									/*!< The registered names, highest priority first. */

			std::expected<std::unique_ptr<Backend>, Error> Create(std::string_view name) const;
									/*!< A new backend \a name; ErrorCode::NotFound if there
										is none, ErrorCode::NotSupported if its factory fails. */
			std::expected<std::unique_ptr<Backend>, Error> CreatePreferred(
									std::string_view preferred) const;
									/*!< The backend \a preferred if it is registered (for a
										setting or an environment variable), otherwise the one
										with the highest priority. */

private:
			struct Entry {
				std::string		name;
				int				priority;
				Factory			factory;
			};

	mutable	std::mutex			fLock;
			std::vector<Entry>	fEntries;
};


} // namespace ngui::gfx
