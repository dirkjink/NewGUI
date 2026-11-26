/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/DateFormat.h>
#include <ngui/NumberFormat.h>

#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "locale/NumberGrouping.h"


namespace {


using namespace ngui;
using namespace std::chrono;


// 2026-11-25 21:05:09 UTC, a Wednesday.
const std::time_t kTime = system_clock::to_time_t(sys_days{year{2026} / 11 / 25} + hours{21}
	+ minutes{5} + seconds{9});


TEST(FormatsTest, TheCLocaleIsTheSameEverywhere)
{
	// What is tested: Numbers, dates and times in the locale "C", and in a
	// locale no system has ("xx_YY").
	// Success criteria: 1234567.5 is "1234567.5" (no grouping), -1234567
	// "-1234567", 3.14159 with 2 digits "3.14", 2.6 with none "3", NaN
	// "nan", infinity "inf"; the date short "11/25/26", long "Wed Nov 25
	// 2026", the time "21:05" and "21:05:09". The unknown locale writes as
	// "C" and says it is not the system's.
	// Why it matters: "C" is the fallback, and tests and files need text
	// that does not depend on the machine (module G7).
	//
	// Given
	const BNumberFormat numbers("C");
	const BDateFormat dates("C");
	const BTimeFormat times("C");
	const BNumberFormat unknown("xx_YY");

	// When / Then
	EXPECT_EQ(numbers.Format(1234567.5), "1234567.5");
	EXPECT_EQ(numbers.Format(std::int64_t{-1234567}), "-1234567");
	EXPECT_EQ(numbers.Format(3.14159, 2), "3.14");
	EXPECT_EQ(numbers.Format(2.6, 0), "3");
	EXPECT_EQ(numbers.Format(std::nan("")), "nan");
	EXPECT_EQ(numbers.Format(std::numeric_limits<double>::infinity()), "inf");
	EXPECT_EQ(numbers.DecimalSeparator(), ".");
	EXPECT_EQ(numbers.GroupingSeparator(), "");
	EXPECT_FALSE(numbers.IsSystemLocale());
	EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Short, true), "11/25/26");
	EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Long, true), "Wed Nov 25 2026");
	EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Short, true), "21:05");
	EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Medium, true), "21:05:09");
	EXPECT_FALSE(unknown.IsSystemLocale());
	EXPECT_EQ(unknown.Format(1234567.5), "1234567.5");
	EXPECT_EQ(BDateFormat("xx_YY").Format(kTime, BDateFormatStyle::Short, true), "11/25/26");
}


TEST(FormatsTest, TheLongDateLeavesTheTimeOutCompletely)
{
	// What is tested: The long date of 2026-11-25 (UTC) in the system's
	// locale "C.UTF-8", whose date and time pattern (D_T_FMT) is
	// "%a %b %e %H:%M:%S %Y" in the C library.
	// Success criteria: "Wed Nov 25 2026": the time and the colons between
	// its parts are gone, no spaces are left over. Skipped where the
	// system has no "C.UTF-8" (Windows).
	// Why it matters: The long date stands alone in date columns and
	// labels; leftovers of the time ("Wed Nov 25 :: 2026") read as
	// garbage (module G7).
	//
	// Given
	if (!BNumberFormat("C.UTF-8").IsSystemLocale())
		GTEST_SKIP() << "the system has no C.UTF-8";
	const BDateFormat dates("C.UTF-8");

	// When / Then
	EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Long, true), "Wed Nov 25 2026");
}


TEST(FormatsTest, DigitsAreGroupedAsCSays)
{
	// What is tested: LocalizeNumber() with German separators and groups of
	// three, with Indian groups (3, then 2), with grouping ended by CHAR_MAX
	// after one group, without grouping, with a minus sign of its own, and
	// with text that is no number.
	// Success criteria: "1.234.567,5", "-12,34,567", "1234,567", "1234567",
	// "−42", "inf"; short numbers get no separator.
	// Why it matters: The grouping of C's locales (and Windows', mapped to
	// it) decides where the separators go.
	//
	// Given
	const locale::NumberConventions german{",", ".", "\3", "-"};
	const locale::NumberConventions indian{".", ",", "\3\2", "-"};
	const locale::NumberConventions once{".", ",", std::string("\3") + char(CHAR_MAX), "-"};
	const locale::NumberConventions none{".", ",", "", "-"};
	const locale::NumberConventions minus{".", ",", "\3", "−"};

	// When / Then
	EXPECT_EQ(locale::LocalizeNumber("1234567.5", german), "1.234.567,5");
	EXPECT_EQ(locale::LocalizeNumber("123", german), "123");
	EXPECT_EQ(locale::LocalizeNumber("-1234567", indian), "-12,34,567");
	EXPECT_EQ(locale::LocalizeNumber("1234567", once), "1234,567");
	EXPECT_EQ(locale::LocalizeNumber("1234567", none), "1234567");
	EXPECT_EQ(locale::LocalizeNumber("-42", minus), "−42");
	EXPECT_EQ(locale::LocalizeNumber("inf", german), "inf");
}


TEST(FormatsTest, TheSystemsLocalesWriteTheirWay)
{
	// What is tested: Numbers, dates and times in German and American
	// English as the system writes them; each part skipped where the
	// system lacks the locale (containers often have none). Windows and the
	// C library write some differently.
	// Success criteria: German: "1.234.567,5", "25.11.2026", "21:05",
	// "21:05:09"; the long date "Mi 25 Nov 2026" from the C library,
	// "Mittwoch, 25. November 2026" under Windows. English: "1,234,567.5",
	// "11/25/2026", the short time "09:05 PM" (C library) or "9:05 PM"
	// (Windows).
	// Why it matters: The user's settings decide how numbers and dates
	// look, as decided for module G7.
	//
	// Given
	const BNumberFormat germanNumbers("de_DE");
	const BNumberFormat englishNumbers("en-US");
	if (!germanNumbers.IsSystemLocale() && !englishNumbers.IsSystemLocale())
		GTEST_SKIP() << "the system has neither de_DE nor en_US";

	// When / Then
	if (germanNumbers.IsSystemLocale()) {
		const BDateFormat dates("de_DE");
		const BTimeFormat times("de_DE");
		EXPECT_EQ(germanNumbers.Format(1234567.5), "1.234.567,5");
		EXPECT_EQ(germanNumbers.DecimalSeparator(), ",");
		EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Short, true), "25.11.2026");
#ifdef _WIN32
		EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Long, true),
			"Mittwoch, 25. November 2026");
#else
		EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Long, true), "Mi 25 Nov 2026");
#endif
		EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Short, true), "21:05");
		EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Medium, true), "21:05:09");
	}
	if (englishNumbers.IsSystemLocale()) {
		const BDateFormat dates("en-US");
		const BTimeFormat times("en-US");
		EXPECT_EQ(englishNumbers.Format(1234567.5), "1,234,567.5");
		EXPECT_EQ(englishNumbers.Format(std::int64_t{-1000}), "-1,000");
		EXPECT_EQ(dates.Format(kTime, BDateFormatStyle::Short, true), "11/25/2026");
#ifdef _WIN32
		EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Short, true), "9:05 PM");
#else
		EXPECT_EQ(times.Format(kTime, BTimeFormatStyle::Short, true), "09:05 PM");
#endif
	}
}


} // namespace
