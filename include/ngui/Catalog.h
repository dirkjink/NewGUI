/*
 * Copyright 2003-2017, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <ngui/Error.h>


namespace ngui {


/*!	The translations of an application (Haiku's BCatalog, module G7), from a
	gettext catalog (.mo file): GetString() gives the translation of a text
	in a context, GetPluralString() the form of a plural message for a
	count, by the catalog's rule ("Plural-Forms" in its header). Texts the
	catalog does not have come back as they are.

	Differences to Haiku: the catalogs are gettext's (xgettext, msgfmt,
	Poedit), not Haiku's catkeys; a context is gettext's msgctxt, and a
	text without a translation in its context is looked up without one, so
	that catalogs made by xgettext from B_TRANSLATE() (which cannot see
	B_TRANSLATION_CONTEXT) work. Plural messages are gettext's (ngettext),
	with B_TRANSLATE_PLURAL(), instead of BStringFormat's ICU patterns.
	Pseudo() makes a catalog for pseudo-localization (specification 20.6).
	Immutable and thread-safe; the strings it returns live as long as it.
*/
class BCatalog {
public:
	static	std::expected<std::shared_ptr<const BCatalog>, Error> FromData(
									std::span<const std::byte> data);
									/*!< The catalog of the .mo file \a data (copied);
										BadData if it is none or is broken. */
	static	std::expected<std::shared_ptr<const BCatalog>, Error> Load(
									std::string_view path);
									/*!< The catalog of the .mo file at \a path (UTF-8);
										IoError if it cannot be read. */
	static	std::shared_ptr<const BCatalog> Pseudo();
									/*!< A catalog that translates every text into a
										longer, accented form of itself ("Open" becomes
										"[Ópéñ~~]"), keeping printf conversions and {...}
										placeholders, so that a layout shows where it is
										too tight or a text is not translated. Language
										"x-pseudo", English plural rule. */

								~BCatalog();
									/*!< Frees the translations. */

			const char*			GetString(const char* string, const char* context = nullptr,
									const char* comment = nullptr) const;
									/*!< The translation of \a string in \a context (or
										without one), \a string itself without one; the
										comment is for translators only. */
			const char*			GetPluralString(const char* singular, const char* plural,
									std::uint64_t count, const char* context = nullptr) const;
									/*!< The form of the plural message \a singular /
										\a plural for \a count; without a translation
										\a singular for 1, else \a plural. */

			const std::string&	Language() const;
									/*!< The language of the catalog (its header's
										"Language"), e.g. "de"; may be empty. */
			std::size_t			CountItems() const;
									/*!< The number of translated texts. */

private:
			struct Data;

								BCatalog();

			std::unique_ptr<Data> fData;
};


namespace detail {

/*!	B_TRANSLATE(): \a string translated by the default catalog. */
const char* Translate(const char* string, const char* context);

/*!	B_TRANSLATE_PLURAL(): the plural form for \a count. */
const char* TranslatePlural(const char* singular, const char* plural, std::uint64_t count,
	const char* context);

} // namespace detail


} // namespace ngui


/*!	\a string translated in the context B_TRANSLATION_CONTEXT, which the
	file defines (Haiku: #define B_TRANSLATION_CONTEXT "Name"), by the
	catalog of BLocaleRoster::Default(). */
#define B_TRANSLATE(string) ::ngui::detail::Translate((string), B_TRANSLATION_CONTEXT)

/*!	\a string translated in \a context. */
#define B_TRANSLATE_CONTEXT(string, context) ::ngui::detail::Translate((string), (context))

/*!	\a string translated, with a \a comment for translators. */
#define B_TRANSLATE_COMMENT(string, comment) \
	::ngui::detail::Translate((string), B_TRANSLATION_CONTEXT)

/*!	\a string translated in \a context, with a \a comment. */
#define B_TRANSLATE_ALL(string, context, comment) \
	::ngui::detail::Translate((string), (context))

/*!	\a string itself, marked for the catalog: translated later with
	B_TRANSLATE_NOCOLLECT(). */
#define B_TRANSLATE_MARK(string) (string)

/*!	B_TRANSLATE_MARK() in \a context. */
#define B_TRANSLATE_MARK_CONTEXT(string, context) (string)

/*!	\a string translated without being collected (a marked text in a
	variable). */
#define B_TRANSLATE_NOCOLLECT(string) \
	::ngui::detail::Translate((string), B_TRANSLATION_CONTEXT)

/*!	NewGUI: the plural message \a singular / \a plural for \a count (gettext's
	ngettext), e.g. B_TRANSLATE_PLURAL("%d file", "%d files", n). */
#define B_TRANSLATE_PLURAL(singular, plural, count) \
	::ngui::detail::TranslatePlural((singular), (plural), \
		static_cast<std::uint64_t>(count), B_TRANSLATION_CONTEXT)
