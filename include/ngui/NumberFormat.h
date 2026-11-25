/*
 * Copyright 2003-2017, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <string>
#include <string_view>


namespace ngui {


/*!	Numbers written as a locale writes them (Haiku's BNumberFormat, module
	G7): its decimal and grouping separators, its grouping and minus sign,
	from the system (the C library's locale data, under Windows
	GetLocaleInfoEx()).

	The default is the user's locale (POSIX: LC_ALL, LC_NUMERIC, LANG;
	Windows: the user's format settings). A locale by name is in gettext's
	form or as a BCP 47 tag ("de_DE", "de-DE"); one the system does not have
	writes numbers as "C" does. "C" and "POSIX" are the same everywhere: a
	"." and no grouping.

	Differences to Haiku: Format() returns the text (Haiku: into a BString
	with a status); no currency and percent formats and no parsing yet.
	Immutable and thread-safe.
*/
class BNumberFormat {
public:
								BNumberFormat();
									/*!< The user's locale. */
	explicit					BNumberFormat(std::string_view locale);
									/*!< The locale \a locale. */

			std::string			Format(double value, int fractionDigits = -1) const;
									/*!< \a value with \a fractionDigits digits after the
										separator (rounded), or as few as it needs (-1);
										"inf" and "nan" as they are. */
			std::string			Format(std::int64_t value) const;
									/*!< \a value, grouped. */

			const std::string&	DecimalSeparator() const;
									/*!< E.g. "," in German. */
			const std::string&	GroupingSeparator() const;
									/*!< E.g. "." in German; empty if the locale does not
										group. */
			bool				IsSystemLocale() const;
									/*!< Whether the system had the locale (otherwise it
										writes as "C"). */

private:
			struct Data;

			std::shared_ptr<const Data> fData;
};


} // namespace ngui
