/*
 * Copyright 2003-2012, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Haiku's BLocaleRoster (headers/os/locale/LocaleRoster.h, commit
// b1d497bb69): the preferred languages and the application's catalog; see
// LocaleRoster.h for the differences.

#include <ngui/LocaleRoster.h>

#include <filesystem>
#include <system_error>
#include <utility>

#include "Languages.h"


namespace ngui {


BLocaleRoster*
BLocaleRoster::Default()
{
	// never destroyed: B_TRANSLATE() may be used until the end
	static BLocaleRoster* const roster = new BLocaleRoster();
	return roster;
}


std::vector<std::string>
BLocaleRoster::PreferredLanguages() const
{
	{
		std::scoped_lock lock(fLock);
		if (fLanguages.has_value())
			return *fLanguages;
	}
	return locale::SystemLanguages();
}


void
BLocaleRoster::SetPreferredLanguages(std::vector<std::string> languages)
{
	std::scoped_lock lock(fLock);
	if (languages.empty())
		fLanguages.reset();
	else
		fLanguages = locale::WithFallbacks(languages);
}


std::shared_ptr<const BCatalog>
BLocaleRoster::Catalog() const
{
	std::scoped_lock lock(fLock);
	return fCatalog;
}


void
BLocaleRoster::SetCatalog(std::shared_ptr<const BCatalog> catalog)
{
	std::scoped_lock lock(fLock);
	if (fCatalog != nullptr)
		fReplaced.push_back(std::move(fCatalog));
	fCatalog = std::move(catalog);
}


std::expected<void, Error>
BLocaleRoster::LoadCatalog(std::string_view domain, std::vector<std::string> directories)
{
	if (directories.empty())
		directories = CatalogDirectories();
	const auto utf8Path = [](std::string_view text) {
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
	};
	for (const std::string& language : PreferredLanguages()) {
		if (language == "x-pseudo") {
			SetCatalog(BCatalog::Pseudo());
			return {};
		}
		for (const std::string& directory : directories) {
			const std::filesystem::path file = utf8Path(directory) / utf8Path(language)
				/ "LC_MESSAGES" / (utf8Path(domain).native() + utf8Path(".mo").native());
			std::error_code error;
			if (!std::filesystem::is_regular_file(file, error))
				continue;
			const std::u8string path = file.u8string();
			if (auto catalog = BCatalog::Load(std::string(path.begin(), path.end()))) {
				SetCatalog(std::move(*catalog));
				return {};
			}
		}
		// the texts are English
		if (language == "en" || language.starts_with("en_")) {
			SetCatalog(nullptr);
			return {};
		}
	}
	SetCatalog(nullptr);
	return Failure(ErrorCode::NotFound, "no catalog for the preferred languages");
}


std::vector<std::string>
BLocaleRoster::CatalogDirectories()
{
	const std::string executable = locale::ExecutableDirectory();
	if (executable.empty())
		return {};
	const std::filesystem::path directory(std::u8string(executable.begin(), executable.end()));
	std::vector<std::string> directories;
	for (const std::filesystem::path& path : {directory / "locale",
			(directory / ".." / "share" / "locale").lexically_normal()}) {
		const std::u8string text = path.u8string();
		directories.emplace_back(text.begin(), text.end());
	}
	return directories;
}


const char*
detail::Translate(const char* string, const char* context)
{
	const std::shared_ptr<const BCatalog> catalog = BLocaleRoster::Default()->Catalog();
	return catalog != nullptr ? catalog->GetString(string, context) : string;
}


const char*
detail::TranslatePlural(const char* singular, const char* plural, std::uint64_t count,
	const char* context)
{
	const std::shared_ptr<const BCatalog> catalog = BLocaleRoster::Default()->Catalog();
	if (catalog == nullptr)
		return count == 1 ? singular : plural;
	return catalog->GetPluralString(singular, plural, count, context);
}


} // namespace ngui
