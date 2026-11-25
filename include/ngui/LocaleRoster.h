/*
 * Copyright 2003-2012, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Catalog.h>
#include <ngui/Error.h>


namespace ngui {


/*!	The languages of the user and the catalog of the application (Haiku's
	BLocaleRoster, module G7): B_TRANSLATE() translates with Catalog().

	PreferredLanguages() come from the environment: NGUI_LANGUAGE (a list
	separated by ":", e.g. "x-pseudo" for pseudo-localization), then as
	gettext does LANGUAGE (unless the locale is "C"), LC_ALL, LC_MESSAGES
	and LANG; under Windows the user's display languages. Each in gettext's
	form ("de_DE", from "de-DE.UTF-8") and followed by its language alone
	("de").

	Differences to Haiku: no BLocale, BCountry or formatting conventions
	here (they come with the next step of module G7), no catalogs of
	add-ons; an application loads its catalog with LoadCatalog(), from
	gettext's layout \<directory\>/\<language\>/LC_MESSAGES/\<domain\>.mo. A
	catalog that is replaced stays alive until the roster goes, since the
	texts of B_TRANSLATE() point into it. Thread-safe.
*/
class BLocaleRoster {
public:
	static	BLocaleRoster*		Default();
									/*!< The roster of the process. */

			std::vector<std::string> PreferredLanguages() const;
									/*!< The user's languages, the most preferred first
										(see the class), or those set. */
			void				SetPreferredLanguages(std::vector<std::string> languages);
									/*!< Uses \a languages (in gettext's form or as BCP 47
										tags) instead of the system's; empty for the
										system's again. */

			std::shared_ptr<const BCatalog> Catalog() const;
									/*!< The catalog of B_TRANSLATE(); nullptr: none, the
										texts stay as they are. */
			void				SetCatalog(std::shared_ptr<const BCatalog> catalog);
									/*!< Makes \a catalog the one of B_TRANSLATE(). */
			std::expected<void, Error> LoadCatalog(std::string_view domain,
									std::vector<std::string> directories = {});
									/*!< Makes the catalog of \a domain for the first
										preferred language that has one the catalog of
										B_TRANSLATE(): \<directory\>/\<language\>/LC_MESSAGES/
										\<domain\>.mo in \a directories (by default those of
										CatalogDirectories()). "x-pseudo" takes Pseudo(); an
										English language before any catalog takes none (the
										texts are English). NotFound, and no catalog, if
										none is there. */

	static	std::vector<std::string> CatalogDirectories();
									/*!< Where LoadCatalog() looks by default: "locale"
										beside the executable and "../share/locale" from
										it. */

private:
								BLocaleRoster() = default;

	mutable	std::mutex			fLock;
			std::optional<std::vector<std::string>> fLanguages;
			std::shared_ptr<const BCatalog> fCatalog;
			std::vector<std::shared_ptr<const BCatalog>> fReplaced;
};


} // namespace ngui
