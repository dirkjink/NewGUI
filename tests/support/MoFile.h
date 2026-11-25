/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>


namespace ngui::test {


// A gettext .mo file of \a entries (msgid, msgstr; a context before its
// msgid with "\x04", a plural msgid singular "\0" plural, its msgstr the
// forms joined by "\0"; the header has the empty msgid), sorted as msgfmt
// sorts them, in little or big endian.
inline std::vector<std::byte>
MoFile(std::vector<std::pair<std::string, std::string>> entries, bool bigEndian = false)
{
	std::ranges::sort(entries);
	const auto count = static_cast<std::uint32_t>(entries.size());
	std::vector<std::byte> file;
	const auto word = [&](std::uint32_t value) {
		for (int i = 0; i < 4; i++) {
			const int shift = bigEndian ? 8 * (3 - i) : 8 * i;
			file.push_back(static_cast<std::byte>((value >> shift) & 0xff));
		}
	};
	const std::uint32_t originals = 28;
	const std::uint32_t translations = originals + 8 * count;
	std::uint32_t strings = translations + 8 * count;
	word(0x950412de);
	word(0);
	word(count);
	word(originals);
	word(translations);
	word(0);
	word(0);
	std::vector<std::string> texts;
	for (int table = 0; table < 2; table++) {
		for (const auto& entry : entries) {
			const std::string& text = table == 0 ? entry.first : entry.second;
			word(static_cast<std::uint32_t>(text.size()));
			word(strings);
			strings += static_cast<std::uint32_t>(text.size() + 1);
			texts.push_back(text);
		}
	}
	for (const std::string& text : texts) {
		for (const char c : text)
			file.push_back(static_cast<std::byte>(c));
		file.push_back(std::byte{0});
	}
	return file;
}


} // namespace ngui::test
