/*
 * Copyright 2002-2006, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Haiku's BMimeType (src/kits/storage/MimeType.cpp, commit b1d497bb69): the
// validity of a type, its supertype and equality as there; the database is
// Haiku's src/data/mime_db as a table (MimeTable.cpp). See MimeType.h for
// the differences.


#include <ngui/MimeType.h>

#include <cstddef>

#include <ngui/IconUtils.h>

#include "MimeTable.h"


namespace ngui {


namespace {


// B_MIME_TYPE_LENGTH: B_ATTR_NAME_LENGTH (255) - 15
constexpr std::size_t kMimeTypeLength = 240;


// Haiku's isValidMimeChar(): no white space, controls or separators
bool
IsValidMimeChar(char c)
{
	const auto value = static_cast<unsigned char>(c);
	if (value <= 32 || value >= 127)
		return false;
	for (const char separator : std::string_view("/<>@,;:\"()[]?=\\")) {
		if (c == separator)
			return false;
	}
	return true;
}


char
Lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}


bool
EqualsIgnoringCase(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (std::size_t i = 0; i < a.size(); i++) {
		if (Lower(a[i]) != Lower(b[i]))
			return false;
	}
	return true;
}


const detail::MimeTableEntry*
Find(std::string_view type)
{
	for (const detail::MimeTableEntry& entry : detail::kMimeTable) {
		if (EqualsIgnoringCase(entry.type, type))
			return &entry;
	}
	return nullptr;
}


// The words of \a text, separated by spaces.
template<typename Each>
void
ForEachWord(std::string_view text, Each each)
{
	std::size_t start = 0;
	while (start < text.size()) {
		std::size_t end = text.find(' ', start);
		if (end == std::string_view::npos)
			end = text.size();
		if (end > start)
			each(text.substr(start, end - start));
		start = end + 1;
	}
}


} // namespace


BMimeType::BMimeType(std::string_view type)
{
	(void)SetTo(type);
}


std::expected<void, Error>
BMimeType::SetTo(std::string_view type)
{
	if (!IsValid(type)) {
		fType.clear();
		fInvalid = true;
		return Failure(ErrorCode::InvalidArgument, "not a valid MIME type");
	}
	fType = type;
	fInvalid = false;
	return {};
}


void
BMimeType::Unset()
{
	fType.clear();
	fInvalid = false;
}


std::expected<void, Error>
BMimeType::InitCheck() const
{
	if (fType.empty()) {
		return Failure(ErrorCode::InvalidArgument,
			fInvalid ? "not a valid MIME type" : "no MIME type");
	}
	return {};
}


const std::string&
BMimeType::Type() const
{
	return fType;
}


bool
BMimeType::IsValid() const
{
	return !fType.empty();
}


bool
BMimeType::IsSupertypeOnly() const
{
	return !fType.empty() && fType.find('/') == std::string::npos;
}


std::expected<BMimeType, Error>
BMimeType::GetSupertype() const
{
	const std::size_t slash = fType.find('/');
	if (fType.empty() || slash == std::string::npos)
		return Failure(ErrorCode::InvalidArgument, "no supertype");
	return BMimeType(std::string_view(fType).substr(0, slash));
}


bool
BMimeType::Contains(const BMimeType& type) const
{
	if (*this == type)
		return true;
	const auto super = type.GetSupertype();
	return super.has_value() && *this == *super;
}


bool
BMimeType::IsInstalled() const
{
	return !fType.empty() && Find(fType) != nullptr;
}


std::string
BMimeType::ShortDescription() const
{
	const detail::MimeTableEntry* entry = fType.empty() ? nullptr : Find(fType);
	return entry != nullptr ? std::string(entry->shortDescription) : std::string();
}


std::vector<std::string>
BMimeType::FileExtensions() const
{
	std::vector<std::string> extensions;
	const detail::MimeTableEntry* entry = fType.empty() ? nullptr : Find(fType);
	if (entry != nullptr) {
		ForEachWord(entry->extensions,
			[&extensions](std::string_view word) { extensions.emplace_back(word); });
	}
	return extensions;
}


std::expected<void, Error>
BMimeType::GetIcon(BBitmap* icon) const
{
	const detail::MimeTableEntry* entry = fType.empty() ? nullptr : Find(fType);
	if (entry == nullptr || entry->icon.empty())
		return Failure(ErrorCode::NotFound, "the type has no icon");
	return BIconUtils::GetSystemIcon(entry->icon, icon);
}


bool
BMimeType::IsValid(std::string_view type)
{
	if (type.empty() || type.size() >= kMimeTypeLength)
		return false;
	bool foundSlash = false;
	for (std::size_t i = 0; i < type.size(); i++) {
		if (type[i] == '/') {
			if (foundSlash || i == 0 || i == type.size() - 1)
				return false;
			foundSlash = true;
		} else if (!IsValidMimeChar(type[i])) {
			return false;
		}
	}
	return true;
}


BMimeType
BMimeType::GuessMimeType(std::string_view filename)
{
	// the extension: after the last dot of the last part of the path
	const std::size_t slash = filename.find_last_of("/\\");
	const std::string_view name
		= slash == std::string_view::npos ? filename : filename.substr(slash + 1);
	const std::size_t dot = name.rfind('.');
	if (dot != std::string_view::npos && dot + 1 < name.size()) {
		const std::string_view extension = name.substr(dot + 1);
		for (const detail::MimeTableEntry& entry : detail::kMimeTable) {
			bool found = false;
			ForEachWord(entry.extensions, [&](std::string_view word) {
				found = found || EqualsIgnoringCase(word, extension);
			});
			if (found)
				return BMimeType(entry.type);
		}
	}
	return BMimeType(kFileMimeType);
}


bool
BMimeType::operator==(const BMimeType& other) const
{
	return EqualsIgnoringCase(fType, other.fType);
}


bool
BMimeType::operator==(std::string_view type) const
{
	return *this == BMimeType(type);
}


} // namespace ngui
