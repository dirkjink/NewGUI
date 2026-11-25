/*
 * Copyright 2003-2017, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Haiku's BCatalog (headers/os/locale/Catalog.h, commit b1d497bb69) over
// gettext's .mo files, read here (the format of GNU gettext's manual,
// "The Format of GNU MO Files"): a header of 32-bit words in the file's
// byte order, two tables of (length, offset) for the original and the
// translated strings. A msgctxt comes before its msgid, joined by EOT
// (\4); a plural message's msgid is singular \0 plural, its translation
// the forms joined by \0. The entry with the empty msgid is the header.

#include <ngui/Catalog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "PluralForms.h"


namespace ngui {


struct BCatalog::Data {
	std::unordered_map<std::string, std::string> entries;
									// msgid (with its context) to its translation
	std::string						language;
	locale::PluralForms				plural;
	bool							pseudo = false;
	mutable std::mutex				lock;
	mutable std::unordered_map<std::string, std::string> pseudoTexts;
									// pseudo: the texts made so far, by original
};


namespace {


constexpr std::uint32_t kMagic = 0x950412de;
constexpr char kContextSeparator = '\x04';


// A 32-bit word of the file at \a offset, in its byte order.
std::uint32_t
Word(std::span<const std::byte> data, std::size_t offset, bool bigEndian)
{
	std::uint32_t value = 0;
	for (std::size_t i = 0; i < 4; i++) {
		const auto byte = static_cast<std::uint32_t>(data[offset + i]);
		value |= bigEndian ? byte << (8 * (3 - i)) : byte << (8 * i);
	}
	return value;
}


// The value of the header field \a name ("Language"), up to its line end.
std::string_view
HeaderField(std::string_view header, std::string_view name)
{
	std::size_t at = 0;
	while (at < header.size()) {
		const std::size_t end = std::min(header.find('\n', at), header.size());
		const std::string_view line = header.substr(at, end - at);
		if (line.size() > name.size() && line.starts_with(name) && line[name.size()] == ':') {
			std::string_view value = line.substr(name.size() + 1);
			while (!value.empty() && value.front() == ' ')
				value.remove_prefix(1);
			return value;
		}
		at = end + 1;
	}
	return {};
}


// The \a index'th form of a translation (forms joined by \0), or nullptr.
const char*
Form(const std::string& translation, std::size_t index)
{
	std::size_t at = 0;
	for (std::size_t i = 0; i < index; i++) {
		at = translation.find('\0', at);
		if (at == std::string::npos)
			return nullptr;
		at++;
	}
	return at <= translation.size() ? translation.c_str() + at : nullptr;
}


// The pseudo-localized form of \a text: letters accented, printf
// conversions (%d, %1$s, %%) and {...} placeholders kept, a third longer
// and in brackets.
std::string
PseudoText(std::string_view text)
{
	static constexpr std::string_view kPlain = "aceinosuyzACEINOSUYZ";
	static constexpr std::string_view kAccented[] = {"á", "ç", "é", "í", "ñ", "ó", "š", "ú",
		"ý", "ž", "Á", "Ç", "É", "Í", "Ñ", "Ó", "Š", "Ú", "Ý", "Ž"};
	std::string result = "[";
	std::size_t letters = 0;
	for (std::size_t i = 0; i < text.size(); i++) {
		const char c = text[i];
		if (c == '%') {
			// a conversion: up to its letter (or a second %)
			const std::size_t start = i++;
			while (i < text.size() && std::string_view("0123456789$#-+ .lhqjzt'").find(text[i])
					!= std::string_view::npos) {
				i++;
			}
			result.append(text.substr(start, std::min(i + 1, text.size()) - start));
			continue;
		}
		if (c == '{') {
			const std::size_t end = text.find('}', i);
			if (end != std::string_view::npos) {
				result.append(text.substr(i, end + 1 - i));
				i = end;
				continue;
			}
		}
		const std::size_t plain = kPlain.find(c);
		if (plain != std::string_view::npos)
			result.append(kAccented[plain]);
		else
			result.push_back(c);
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
			letters++;
	}
	result.append((letters + 2) / 3, '~');
	result.push_back(']');
	return result;
}


} // namespace


BCatalog::BCatalog()
	:
	fData(std::make_unique<Data>())
{
}


BCatalog::~BCatalog() = default;


std::expected<std::shared_ptr<const BCatalog>, Error>
BCatalog::FromData(std::span<const std::byte> data)
{
	if (data.size() < 28)
		return Failure(ErrorCode::BadData, "too short for a .mo file");
	bool bigEndian = false;
	if (Word(data, 0, false) != kMagic) {
		if (Word(data, 0, true) != kMagic)
			return Failure(ErrorCode::BadData, "not a .mo file");
		bigEndian = true;
	}
	if ((Word(data, 4, bigEndian) >> 16) > 1)
		return Failure(ErrorCode::BadData, "a .mo file of an unknown revision");

	const std::uint64_t count = Word(data, 8, bigEndian);
	const std::uint64_t originals = Word(data, 12, bigEndian);
	const std::uint64_t translations = Word(data, 16, bigEndian);
	if (originals + count * 8 > data.size() || translations + count * 8 > data.size())
		return Failure(ErrorCode::BadData, "the tables of the .mo file are cut off");

	// the string the table entry at \a at points to
	const auto string = [&](std::uint64_t at, std::string_view* out) {
		const std::uint64_t length = Word(data, static_cast<std::size_t>(at), bigEndian);
		const std::uint64_t offset = Word(data, static_cast<std::size_t>(at + 4), bigEndian);
		if (offset + length > data.size())
			return false;
		*out = std::string_view(reinterpret_cast<const char*>(data.data()) + offset,
			static_cast<std::size_t>(length));
		return true;
	};

	std::shared_ptr<BCatalog> catalog(new BCatalog());
	for (std::uint64_t i = 0; i < count; i++) {
		std::string_view original;
		std::string_view translation;
		if (!string(originals + i * 8, &original) || !string(translations + i * 8, &translation))
			return Failure(ErrorCode::BadData, "a string of the .mo file is cut off");
		if (original.empty()) {
			// the header
			catalog->fData->language = HeaderField(translation, "Language");
			if (const std::string_view rule = HeaderField(translation, "Plural-Forms");
					!rule.empty()) {
				if (auto plural = locale::PluralForms::Parse(rule))
					catalog->fData->plural = std::move(*plural);
			}
			continue;
		}
		// a plural message by its singular
		const std::string_view key = original.substr(0, original.find('\0'));
		catalog->fData->entries.insert_or_assign(std::string(key), std::string(translation));
	}
	return catalog;
}


std::expected<std::shared_ptr<const BCatalog>, Error>
BCatalog::Load(std::string_view path)
{
	std::ifstream file(std::filesystem::path(std::u8string(path.begin(), path.end())),
		std::ios::binary);
	if (!file)
		return Failure(ErrorCode::IoError, "the catalog cannot be read");
	const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>());
	return FromData(std::as_bytes(std::span(bytes)));
}


std::shared_ptr<const BCatalog>
BCatalog::Pseudo()
{
	std::shared_ptr<BCatalog> catalog(new BCatalog());
	catalog->fData->pseudo = true;
	catalog->fData->language = "x-pseudo";
	return catalog;
}


const char*
BCatalog::GetString(const char* string, const char* context, const char*) const
{
	if (string == nullptr)
		return nullptr;
	if (fData->pseudo) {
		std::scoped_lock lock(fData->lock);
		auto [entry, added] = fData->pseudoTexts.try_emplace(string);
		if (added)
			entry->second = PseudoText(string);
		return entry->second.c_str();
	}
	if (context != nullptr && *context != '\0') {
		std::string key(context);
		key.push_back(kContextSeparator);
		key.append(string);
		if (const auto found = fData->entries.find(key); found != fData->entries.end())
			return found->second.c_str();
	}
	if (const auto found = fData->entries.find(string); found != fData->entries.end())
		return found->second.c_str();
	return string;
}


const char*
BCatalog::GetPluralString(const char* singular, const char* plural, std::uint64_t count,
	const char* context) const
{
	if (singular == nullptr || plural == nullptr)
		return count == 1 ? singular : plural;
	const char* source = count == 1 ? singular : plural;
	if (fData->pseudo)
		return GetString(source, context);

	const std::string* translation = nullptr;
	if (context != nullptr && *context != '\0') {
		std::string key(context);
		key.push_back(kContextSeparator);
		key.append(singular);
		if (const auto found = fData->entries.find(key); found != fData->entries.end())
			translation = &found->second;
	}
	if (translation == nullptr) {
		if (const auto found = fData->entries.find(singular); found != fData->entries.end())
			translation = &found->second;
	}
	if (translation == nullptr)
		return source;
	const char* form = Form(*translation, fData->plural.FormFor(count));
	return form != nullptr ? form : source;
}


const std::string&
BCatalog::Language() const
{
	return fData->language;
}


std::size_t
BCatalog::CountItems() const
{
	return fData->entries.size();
}


} // namespace ngui
