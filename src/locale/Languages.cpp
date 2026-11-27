/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "Languages.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include "../Environment.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif


namespace ngui::locale {


namespace {


// The items of a list separated by ":".
std::vector<std::string>
Split(std::string_view list)
{
	std::vector<std::string> items;
	while (!list.empty()) {
		const std::size_t end = std::min(list.find(':'), list.size());
		if (end > 0)
			items.emplace_back(list.substr(0, end));
		list.remove_prefix(std::min(end + 1, list.size()));
	}
	return items;
}


// A variable of the environment, empty if it is not set.
std::string
Environment(const char* name)
{
	return detail::EnvironmentVariable(name).value_or(std::string());
}


#ifdef _WIN32

std::string
Utf8(std::wstring_view text)
{
	if (text.empty())
		return {};
	const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string utf8(static_cast<std::size_t>(length), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		utf8.data(), length, nullptr, nullptr);
	return utf8;
}

#endif


} // namespace


std::string
NormalizeLanguage(std::string_view tag)
{
	tag = tag.substr(0, std::min(tag.find_first_of(".@"), tag.size()));
	if (tag == "C" || tag == "POSIX")
		return {};
	// a private tag (pseudo-localization) as it is
	if (tag.starts_with("x-"))
		return std::string(tag);
	std::string language(tag);
	std::ranges::replace(language, '-', '_');
	return language;
}


std::vector<std::string>
WithFallbacks(const std::vector<std::string>& languages)
{
	std::vector<std::string> result;
	const auto add = [&](std::string language) {
		if (!language.empty() && std::ranges::find(result, language) == result.end())
			result.push_back(std::move(language));
	};
	for (const std::string& tag : languages) {
		const std::string language = NormalizeLanguage(tag);
		add(language);
		if (!language.starts_with("x-"))
			add(language.substr(0, std::min(language.find('_'), language.size())));
	}
	return result;
}


std::vector<std::string>
LanguagesFromEnvironment(std::string_view ngui, std::string_view language,
	std::string_view lcAll, std::string_view lcMessages, std::string_view lang)
{
	if (!ngui.empty())
		return WithFallbacks(Split(ngui));
	std::string_view locale = !lcAll.empty() ? lcAll : !lcMessages.empty() ? lcMessages : lang;
	if (NormalizeLanguage(locale).empty() && !locale.empty())
		return {};
	if (!language.empty())
		return WithFallbacks(Split(language));
	if (!locale.empty())
		return WithFallbacks({std::string(locale)});
	return {};
}


std::vector<std::string>
SystemLanguages()
{
	std::vector<std::string> languages = LanguagesFromEnvironment(Environment("NGUI_LANGUAGE"),
		Environment("LANGUAGE"), Environment("LC_ALL"), Environment("LC_MESSAGES"),
		Environment("LANG"));
#ifdef _WIN32
	if (languages.empty()) {
		ULONG count = 0;
		ULONG size = 0;
		if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, nullptr, &size) && size > 0) {
			std::wstring buffer(size, L'\0');
			if (GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, buffer.data(), &size)) {
				// a list of names, each ending with \0
				std::vector<std::string> names;
				for (std::size_t at = 0; at < buffer.size() && buffer[at] != L'\0';) {
					const std::size_t end = buffer.find(L'\0', at);
					names.push_back(Utf8(std::wstring_view(buffer).substr(at, end - at)));
					at = end + 1;
				}
				languages = WithFallbacks(names);
			}
		}
	}
#endif
	return languages;
}


std::string
ExecutableDirectory()
{
	std::filesystem::path path;
#if defined(_WIN32)
	std::wstring buffer(MAX_PATH, L'\0');
	while (true) {
		const DWORD length = GetModuleFileNameW(nullptr, buffer.data(),
			static_cast<DWORD>(buffer.size()));
		if (length == 0)
			return {};
		if (length < buffer.size()) {
			buffer.resize(length);
			break;
		}
		buffer.resize(buffer.size() * 2);
	}
	path = buffer;
#elif defined(__linux__)
	std::error_code error;
	path = std::filesystem::read_symlink("/proc/self/exe", error);
	if (error)
		return {};
#elif defined(__APPLE__)
	std::uint32_t size = 0;
	(void)_NSGetExecutablePath(nullptr, &size);
	std::string buffer(size, '\0');
	if (_NSGetExecutablePath(buffer.data(), &size) != 0)
		return {};
	buffer.resize(std::char_traits<char>::length(buffer.c_str()));
	std::error_code error;
	path = std::filesystem::canonical(buffer, error);
	if (error)
		return {};
#else
	return {};
#endif
	const std::u8string directory = path.parent_path().u8string();
	return std::string(directory.begin(), directory.end());
}


} // namespace ngui::locale
