/*
 * Copyright 2003-2017, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Haiku's BNumberFormat, BDateFormat and BTimeFormat (headers/os/locale,
// commit b1d497bb69) over the system's locale data instead of ICU: the C
// library's (newlocale(), nl_langinfo_l(), strftime_l()), under Windows
// GetLocaleInfoEx(), GetDateFormatEx() and GetTimeFormatEx(). The digits
// of numbers are written here (std::to_chars), grouped by the locale's
// conventions. See NumberFormat.h and DateFormat.h for the differences.

#include <ngui/DateFormat.h>
#include <ngui/NumberFormat.h>

#include <array>
#include <algorithm>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>

#include "NumberGrouping.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <langinfo.h>
#include <locale.h>
#endif


namespace ngui {


namespace {


bool
IsC(std::string_view name)
{
	return name == "C" || name == "POSIX";
}


// The broken-down \a time, local or UTC; false if it has none.
bool
BrokenDown(std::time_t time, bool utc, std::tm* out)
{
#ifdef _WIN32
	return (utc ? gmtime_s(out, &time) : localtime_s(out, &time)) == 0;
#else
	return (utc ? gmtime_r(&time, out) : localtime_r(&time, out)) != nullptr;
#endif
}


// C's formats, the same on every system: "%m/%d/%y", D_T_FMT's date
// ("%a %b %e %Y") and "%H:%M:%S".
std::string
CDate(const std::tm& time, BDateFormatStyle style)
{
	static constexpr std::array<const char*, 7> kDays = {"Sun", "Mon", "Tue", "Wed", "Thu",
		"Fri", "Sat"};
	static constexpr std::array<const char*, 12> kMonths = {"Jan", "Feb", "Mar", "Apr", "May",
		"Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
	std::array<char, 64> buffer{};
	if (style == BDateFormatStyle::Short) {
		(void)std::snprintf(buffer.data(), buffer.size(), "%02d/%02d/%02d", time.tm_mon + 1,
			time.tm_mday, (time.tm_year + 1900) % 100);
	} else {
		(void)std::snprintf(buffer.data(), buffer.size(), "%s %s %2d %d",
			kDays[static_cast<std::size_t>(time.tm_wday % 7)],
			kMonths[static_cast<std::size_t>(time.tm_mon % 12)], time.tm_mday,
			time.tm_year + 1900);
	}
	return buffer.data();
}


std::string
CTime(const std::tm& time, BTimeFormatStyle style)
{
	std::array<char, 32> buffer{};
	if (style == BTimeFormatStyle::Short)
		(void)std::snprintf(buffer.data(), buffer.size(), "%02d:%02d", time.tm_hour, time.tm_min);
	else {
		(void)std::snprintf(buffer.data(), buffer.size(), "%02d:%02d:%02d", time.tm_hour,
			time.tm_min, time.tm_sec);
	}
	return buffer.data();
}


#ifdef _WIN32


// A locale's name for the Windows API: BCP 47 ("de-DE"); empty for the
// user's.
std::wstring
WindowsName(std::string_view name)
{
	std::wstring wide;
	for (const char c : name.substr(0, std::min(name.find_first_of(".@"), name.size())))
		wide.push_back(c == '_' ? L'-' : static_cast<wchar_t>(static_cast<unsigned char>(c)));
	return wide;
}


std::string
Utf8(const wchar_t* text, int length)
{
	if (length <= 0)
		return {};
	const int size = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr,
		nullptr);
	std::string utf8(static_cast<std::size_t>(size), '\0');
	(void)WideCharToMultiByte(CP_UTF8, 0, text, length, utf8.data(), size, nullptr, nullptr);
	return utf8;
}


// The system's locale: its name (nullptr for the user's); whether
// Windows knows it.
struct SystemLocale {
	std::wstring	name;
	bool			user = true;
	bool			valid = false;

	explicit SystemLocale(std::string_view locale)
	{
		if (IsC(locale))
			return;
		user = locale.empty();
		name = WindowsName(locale);
		valid = user || IsValidLocaleName(name.c_str()) != FALSE;
	}

	const wchar_t* Name() const
	{
		return user ? LOCALE_NAME_USER_DEFAULT : name.c_str();
	}

	std::string Info(LCTYPE type) const
	{
		std::array<wchar_t, 64> buffer{};
		const int length = GetLocaleInfoEx(Name(), type, buffer.data(),
			static_cast<int>(buffer.size()));
		return length > 1 ? Utf8(buffer.data(), length - 1) : std::string();
	}
};


SYSTEMTIME
SystemTime(const std::tm& time)
{
	SYSTEMTIME system{};
	system.wYear = static_cast<WORD>(time.tm_year + 1900);
	system.wMonth = static_cast<WORD>(time.tm_mon + 1);
	system.wDayOfWeek = static_cast<WORD>(time.tm_wday);
	system.wDay = static_cast<WORD>(time.tm_mday);
	system.wHour = static_cast<WORD>(time.tm_hour);
	system.wMinute = static_cast<WORD>(time.tm_min);
	system.wSecond = static_cast<WORD>(time.tm_sec);
	return system;
}


#else


// The system's locale data for numbers and dates, or none ("C").
struct SystemLocale {
	locale_t		handle = static_cast<locale_t>(nullptr);

	explicit SystemLocale(std::string_view locale)
	{
		constexpr int kMask = LC_NUMERIC_MASK | LC_TIME_MASK;
		if (IsC(locale))
			return;
		if (locale.empty()) {
			// the categories of the environment
			handle = newlocale(kMask, "", static_cast<locale_t>(nullptr));
			return;
		}
		std::string name(locale.substr(0, std::min(locale.find_first_of(".@"), locale.size())));
		for (char& c : name) {
			if (c == '-')
				c = '_';
		}
		for (const char* suffix : {".UTF-8", ".utf8", ""}) {
			handle = newlocale(kMask, (name + suffix).c_str(), static_cast<locale_t>(nullptr));
			if (handle != static_cast<locale_t>(nullptr))
				return;
		}
	}

	~SystemLocale()
	{
		if (handle != static_cast<locale_t>(nullptr))
			freelocale(handle);
	}

	SystemLocale(const SystemLocale&) = delete;
	SystemLocale& operator=(const SystemLocale&) = delete;

	bool Valid() const
	{
		return handle != static_cast<locale_t>(nullptr);
	}

	std::string Info(nl_item item) const
	{
		const char* value = nl_langinfo_l(item, handle);
		return value != nullptr ? value : "";
	}

	std::string Format(std::string_view pattern, const std::tm& time) const
	{
		std::array<char, 256> buffer{};
		const std::size_t length = strftime_l(buffer.data(), buffer.size(),
			std::string(pattern).c_str(), &time, handle);
		return std::string(buffer.data(), length);
	}
};


// \a pattern without the conversions of a time of day and the spaces they
// leave: D_T_FMT's date.
std::string
WithoutTime(std::string_view pattern)
{
	std::string result;
	for (std::size_t i = 0; i < pattern.size(); i++) {
		if (pattern[i] == '%' && i + 1 < pattern.size()) {
			std::size_t at = i + 1;
			while (at < pattern.size() && (pattern[at] == 'E' || pattern[at] == 'O'
					|| pattern[at] == '-' || pattern[at] == '_' || pattern[at] == '0')) {
				at++;
			}
			constexpr std::string_view kTimeConversions = "HIklMSpPrRTXZzs";
			if (at < pattern.size()
				&& kTimeConversions.find(pattern[at]) != std::string_view::npos) {
				i = at;
				continue;
			}
			result.append(pattern.substr(i, at + 1 - i));
			i = at;
			continue;
		}
		result.push_back(pattern[i]);
	}
	// the spaces the time left
	std::string tidy;
	for (const char c : result) {
		if (c == ' ' && (tidy.empty() || tidy.back() == ' '))
			continue;
		tidy.push_back(c);
	}
	while (!tidy.empty() && tidy.back() == ' ')
		tidy.pop_back();
	return tidy;
}


// T_FMT without its seconds: %T is %H:%M:%S, %r %I:%M:%S %p.
std::string
WithoutSeconds(std::string_view pattern)
{
	std::string result;
	for (std::size_t i = 0; i < pattern.size(); i++) {
		if (pattern[i] == '%' && i + 1 < pattern.size()) {
			const char conversion = pattern[i + 1];
			if (conversion == 'T')
				result += "%H:%M";
			else if (conversion == 'r')
				result += "%I:%M %p";
			else if (conversion == 'S') {
				// and the separator before it
				if (!result.empty() && (result.back() == ':' || result.back() == '.'))
					result.pop_back();
			} else
				result.append(pattern.substr(i, 2));
			i++;
			continue;
		}
		result.push_back(pattern[i]);
	}
	return result;
}


#endif


} // namespace


// #pragma mark - BNumberFormat


struct BNumberFormat::Data {
	locale::NumberConventions	conventions;
	bool						system = false;
};


BNumberFormat::BNumberFormat()
	:
	BNumberFormat(std::string_view())
{
}


BNumberFormat::BNumberFormat(std::string_view name)
{
	auto data = std::make_shared<Data>();
	const SystemLocale locale(name);
#ifdef _WIN32
	if (locale.valid) {
		data->system = true;
		data->conventions.decimal = locale.Info(LOCALE_SDECIMAL);
		data->conventions.group = locale.Info(LOCALE_STHOUSAND);
		data->conventions.minus = locale.Info(LOCALE_SNEGATIVESIGN);
		// "3;2;0": the sizes; a final 0 repeats the last (C repeats it at
		// the end), without one there are no more groups (C: CHAR_MAX)
		const std::string sizes = locale.Info(LOCALE_SGROUPING);
		std::string grouping;
		for (const char c : sizes) {
			if (c >= '1' && c <= '9')
				grouping.push_back(static_cast<char>(c - '0'));
		}
		if (!sizes.ends_with(";0"))
			grouping.push_back(static_cast<char>(CHAR_MAX));
		data->conventions.grouping = grouping;
	}
#else
	if (locale.Valid()) {
		data->system = true;
		data->conventions.decimal = locale.Info(RADIXCHAR);
		data->conventions.group = locale.Info(THOUSEP);
#ifdef __GLIBC__
		data->conventions.grouping = locale.Info(__GROUPING);
#else
		data->conventions.grouping = "\3";
#endif
	}
#endif
	if (data->conventions.decimal.empty())
		data->conventions.decimal = ".";
	if (data->conventions.minus.empty())
		data->conventions.minus = "-";
	fData = std::move(data);
}


std::string
BNumberFormat::Format(double value, int fractionDigits) const
{
	std::array<char, 400> buffer{};
	std::to_chars_result result;
	if (!std::isfinite(value))
		return std::isnan(value) ? "nan" : value > 0 ? "inf" : "-inf";
	if (fractionDigits < 0)
		result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
			std::chars_format::fixed);
	else {
		result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
			std::chars_format::fixed, std::min(fractionDigits, 50));
	}
	if (result.ec != std::errc())
		return "nan";
	return locale::LocalizeNumber(std::string_view(buffer.data(),
		static_cast<std::size_t>(result.ptr - buffer.data())), fData->conventions);
}


std::string
BNumberFormat::Format(std::int64_t value) const
{
	std::array<char, 32> buffer{};
	const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
	return locale::LocalizeNumber(std::string_view(buffer.data(),
		static_cast<std::size_t>(result.ptr - buffer.data())), fData->conventions);
}


const std::string&
BNumberFormat::DecimalSeparator() const
{
	return fData->conventions.decimal;
}


const std::string&
BNumberFormat::GroupingSeparator() const
{
	return fData->conventions.group;
}


bool
BNumberFormat::IsSystemLocale() const
{
	return fData->system;
}


// #pragma mark - BDateFormat and BTimeFormat


struct BDateFormat::Data {
	std::string		name;
};


BDateFormat::BDateFormat()
	:
	BDateFormat(std::string_view())
{
}


BDateFormat::BDateFormat(std::string_view locale)
	:
	fData(std::make_shared<Data>(Data{std::string(locale)}))
{
}


std::string
BDateFormat::Format(std::time_t time, BDateFormatStyle style, bool utc) const
{
	std::tm broken{};
	if (!BrokenDown(time, utc, &broken))
		return {};
	// the locale is opened for each call: the C library's locale objects
	// are not to be shared between threads by every implementation
	const SystemLocale locale(fData->name);
#ifdef _WIN32
	if (!locale.valid)
		return CDate(broken, style);
	const SYSTEMTIME system = SystemTime(broken);
	std::array<wchar_t, 128> buffer{};
	const int length = GetDateFormatEx(locale.Name(),
		style == BDateFormatStyle::Short ? DATE_SHORTDATE : DATE_LONGDATE, &system, nullptr,
		buffer.data(), static_cast<int>(buffer.size()), nullptr);
	return length > 1 ? Utf8(buffer.data(), length - 1) : CDate(broken, style);
#else
	if (!locale.Valid())
		return CDate(broken, style);
	const std::string pattern = style == BDateFormatStyle::Short ? locale.Info(D_FMT)
		: WithoutTime(locale.Info(D_T_FMT));
	return locale.Format(pattern, broken);
#endif
}


struct BTimeFormat::Data {
	std::string		name;
};


BTimeFormat::BTimeFormat()
	:
	BTimeFormat(std::string_view())
{
}


BTimeFormat::BTimeFormat(std::string_view locale)
	:
	fData(std::make_shared<Data>(Data{std::string(locale)}))
{
}


std::string
BTimeFormat::Format(std::time_t time, BTimeFormatStyle style, bool utc) const
{
	std::tm broken{};
	if (!BrokenDown(time, utc, &broken))
		return {};
	const SystemLocale locale(fData->name);
#ifdef _WIN32
	if (!locale.valid)
		return CTime(broken, style);
	const SYSTEMTIME system = SystemTime(broken);
	std::array<wchar_t, 64> buffer{};
	const int length = GetTimeFormatEx(locale.Name(),
		style == BTimeFormatStyle::Short ? TIME_NOSECONDS : 0, &system, nullptr, buffer.data(),
		static_cast<int>(buffer.size()));
	return length > 1 ? Utf8(buffer.data(), length - 1) : CTime(broken, style);
#else
	if (!locale.Valid())
		return CTime(broken, style);
	const std::string pattern = locale.Info(T_FMT);
	return locale.Format(style == BTimeFormatStyle::Short ? WithoutSeconds(pattern) : pattern,
		broken);
#endif
}


} // namespace ngui
