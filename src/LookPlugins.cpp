/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Looks as plugins (specification 18.6, module M17c): a shared library made
// with NGUI_LOOK_PLUGIN(), loaded with dlopen() or LoadLibrary(), checked
// (build key, interface version, L12) and registered; it stays loaded while
// its look lives.


#include <ngui/LookRegistry.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <ngui/LookPlugin.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif


namespace ngui {


namespace {


using CreateFunction = ControlLook* (*)(std::uint32_t);
using DestroyFunction = void (*)(ControlLook*);
using BuildKeyFunction = const char* (*)();


#if defined(_WIN32)
constexpr std::string_view kLibraryExtension = ".dll";
#elif defined(__APPLE__)
constexpr std::string_view kLibraryExtension = ".dylib";
#else
constexpr std::string_view kLibraryExtension = ".so";
#endif


// A loaded shared library, unloaded when the last reference goes.
class PluginLibrary {
public:
	static std::expected<std::shared_ptr<PluginLibrary>, Error> Open(
		const std::filesystem::path& file)
	{
#if defined(_WIN32)
		// only the plugin's own directory and the system's, not the current
		// one or PATH, for the libraries it depends on; that needs an
		// absolute path with backslashes
		std::error_code error;
		std::filesystem::path full = std::filesystem::absolute(file, error);
		if (error)
			full = file;
		full.make_preferred();
		HMODULE handle = ::LoadLibraryExW(full.c_str(), nullptr,
			LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
		if (handle == nullptr) {
			return Failure(ErrorCode::NotFound, "cannot load " + file.string() + " (error "
				+ std::to_string(::GetLastError()) + ")");
		}
#else
		void* handle = ::dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
		if (handle == nullptr) {
			const char* reason = ::dlerror();
			return Failure(ErrorCode::NotFound, "cannot load " + file.string() + ": "
				+ (reason != nullptr ? reason : "unknown error"));
		}
#endif
		return std::shared_ptr<PluginLibrary>(new PluginLibrary(handle));
	}

	PluginLibrary(const PluginLibrary&) = delete;
	PluginLibrary& operator=(const PluginLibrary&) = delete;

	~PluginLibrary()
	{
#if defined(_WIN32)
		::FreeLibrary(static_cast<HMODULE>(fHandle));
#else
		::dlclose(fHandle);
#endif
	}

	// The function \a name exports, or nullptr.
	template<typename Function>
	Function Symbol(const char* name) const
	{
#if defined(_WIN32)
		// over void (*)(), the generic function pointer, which casts to any
		// other without -Wcast-function-type
		const FARPROC symbol = ::GetProcAddress(static_cast<HMODULE>(fHandle), name);
		return reinterpret_cast<Function>(reinterpret_cast<void (*)()>(symbol));
#else
		return reinterpret_cast<Function>(::dlsym(fHandle, name));
#endif
	}

private:
	explicit PluginLibrary(void* handle)
		:
		fHandle(handle)
	{
	}

	void* fHandle;
};


} // namespace


std::expected<std::string, Error>
LookRegistry::LoadPlugin(const std::filesystem::path& file)
{
	auto library = PluginLibrary::Open(file);
	if (!library.has_value())
		return std::unexpected(library.error());

	const auto create = (*library)->Symbol<CreateFunction>("ngui_create_control_look");
	const auto destroy = (*library)->Symbol<DestroyFunction>("ngui_destroy_control_look");
	const auto buildKey = (*library)->Symbol<BuildKeyFunction>("ngui_look_plugin_build_key");
	if (create == nullptr || destroy == nullptr || buildKey == nullptr) {
		return Failure(ErrorCode::InvalidArgument, file.string()
			+ " is no look plugin (NGUI_LOOK_PLUGIN())");
	}

	const char* theirs = buildKey();
	const std::string ours = LookPluginBuildKey();
	if (theirs == nullptr || ours != theirs) {
		return Failure(ErrorCode::NotSupported, file.string() + " was built for \""
			+ (theirs != nullptr ? theirs : "") + "\", this program is \"" + ours + "\"");
	}

	ControlLook* made = create(ControlLook::kInterfaceVersion);
	if (made == nullptr) {
		return Failure(ErrorCode::NotSupported, file.string()
			+ " declines the look interface version "
			+ std::to_string(ControlLook::kInterfaceVersion) + " (L12)");
	}
	// The look keeps its library loaded: the deleter holds it and drops it
	// only after the plugin destroyed the look.
	const std::shared_ptr<const ControlLook> look(made,
		[library = *library, destroy](const ControlLook* gone) {
			destroy(const_cast<ControlLook*>(gone));
		});

	std::string id(look->Info().id);
	if (!Register(id, [look] { return look; })) {
		return Failure(ErrorCode::InvalidArgument, file.string() + ": the look id \"" + id
			+ "\" is empty or taken");
	}
	return id;
}


LookPluginReport
LookRegistry::LoadPlugins(const std::filesystem::path& directory)
{
	LookPluginReport report;
	std::error_code error;
	std::vector<std::filesystem::path> files;
	for (std::filesystem::directory_iterator entry(directory, error), end;
			!error && entry != end; entry.increment(error)) {
		if (entry->is_regular_file(error) && entry->path().extension() == kLibraryExtension)
			files.push_back(entry->path());
	}
	if (error) {
		report.warnings.push_back("cannot read the look plugins in " + directory.string()
			+ ": " + error.message());
	}

	std::sort(files.begin(), files.end());
	for (const std::filesystem::path& file : files) {
		if (auto id = LoadPlugin(file))
			report.ids.push_back(std::move(*id));
		else
			report.warnings.push_back(id.error().message);
	}
	return report;
}


} // namespace ngui
