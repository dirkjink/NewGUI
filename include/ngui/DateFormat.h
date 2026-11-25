/*
 * Copyright 2010-2017, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <ctime>
#include <memory>
#include <string>
#include <string_view>


namespace ngui {


/*!	How long a date is written (Haiku's BDateFormatStyle). */
enum class BDateFormatStyle : std::uint8_t {
	Short,	/*!< B_SHORT_DATE_FORMAT: "25.11.2026", "11/25/2026". */
	Long	/*!< B_LONG_DATE_FORMAT: with the names of the weekday and month, as
				the system writes them ("Mi 25 Nov 2026" from the C library,
				"Mittwoch, 25. November 2026" under Windows). */
};


/*!	How long a time is written (Haiku's BTimeFormatStyle). */
enum class BTimeFormatStyle : std::uint8_t {
	Short,	/*!< B_SHORT_TIME_FORMAT: hours and minutes, "21:05", "09:05 PM". */
	Medium	/*!< B_MEDIUM_TIME_FORMAT: with seconds, "21:05:09". */
};


/*!	Dates written as a locale writes them (Haiku's BDateFormat, module G7),
	by the system: the C library's strftime_l() with the locale's patterns
	(D_FMT; for Long its date and time pattern D_T_FMT without the time),
	under Windows GetDateFormatEx(). The default is the user's locale
	(POSIX: LC_ALL, LC_TIME, LANG); a locale by name is in gettext's form or
	a BCP 47 tag; one the system lacks, and "C", write as C does
	("11/25/26", "Wed Nov 25 2026"). The time is the local one, or UTC.

	Differences to Haiku: Format() returns the text; only the short and
	long styles (Windows has no others); no parsing, no time zones but the
	local one and UTC. Immutable and thread-safe.
*/
class BDateFormat {
public:
								BDateFormat();
									/*!< The user's locale. */
	explicit					BDateFormat(std::string_view locale);
									/*!< The locale \a locale. */

			std::string			Format(std::time_t time, BDateFormatStyle style,
									bool utc = false) const;
									/*!< The date of \a time, in the local time zone or
										in UTC. */

private:
			struct Data;

			std::shared_ptr<const Data> fData;
};


/*!	Times written as a locale writes them (Haiku's BTimeFormat): the C
	library's T_FMT (Short without its seconds), under Windows
	GetTimeFormatEx(). As BDateFormat otherwise ("C": "21:05:09"). */
class BTimeFormat {
public:
								BTimeFormat();
									/*!< The user's locale. */
	explicit					BTimeFormat(std::string_view locale);
									/*!< The locale \a locale. */

			std::string			Format(std::time_t time, BTimeFormatStyle style,
									bool utc = false) const;
									/*!< The time of day of \a time. */

private:
			struct Data;

			std::shared_ptr<const Data> fData;
};


} // namespace ngui
