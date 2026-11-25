/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>
#include <string_view>
#include <vector>


namespace ngui::locale {


/*!	\a tag in gettext's form: "de-DE.UTF-8@euro" becomes "de_DE"; empty for
	"C" and "POSIX"; a private tag ("x-pseudo") stays as it is. */
std::string NormalizeLanguage(std::string_view tag);

/*!	\a languages normalized, each followed by its language alone ("de_DE",
	"de"), without repetitions or empty ones. */
std::vector<std::string> WithFallbacks(const std::vector<std::string>& languages);

/*!	The languages of the environment variables, as gettext reads them:
	\a ngui (NGUI_LANGUAGE) first, else \a language (LANGUAGE) unless the
	locale (the first of \a lcAll, \a lcMessages, \a lang that is set) is
	"C", else that locale. Lists are separated by ":". */
std::vector<std::string> LanguagesFromEnvironment(std::string_view ngui,
	std::string_view language, std::string_view lcAll, std::string_view lcMessages,
	std::string_view lang);

/*!	The user's languages: of the environment, else under Windows the
	display languages; with fallbacks. */
std::vector<std::string> SystemLanguages();

/*!	The directory of the running executable (UTF-8), empty if unknown. */
std::string ExecutableDirectory();


} // namespace ngui::locale
