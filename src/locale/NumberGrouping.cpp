/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "NumberGrouping.h"

#include <algorithm>
#include <climits>
#include <vector>


namespace ngui::locale {


std::string
LocalizeNumber(std::string_view number, const NumberConventions& conventions)
{
	const bool negative = number.starts_with('-');
	if (negative)
		number.remove_prefix(1);
	const std::size_t point = number.find('.');
	const std::string_view integer = number.substr(0, point);
	if (integer.empty() || integer.find_first_not_of("0123456789") != std::string_view::npos)
		return std::string(negative ? "-" : "") + std::string(number);

	// where the separators go, by digits from the right: C's grouping, each
	// byte a group's size; 0 or the end repeats the last size, CHAR_MAX (or
	// a negative char) ends the grouping
	std::vector<std::size_t> breaks;
	if (!conventions.group.empty()) {
		std::size_t position = 0;
		std::size_t size = 0;
		bool repeating = false;
		for (std::size_t i = 0;; i++) {
			if (!repeating && i < conventions.grouping.size()) {
				const auto byte = static_cast<unsigned char>(conventions.grouping[i]);
				if (byte == 0)
					repeating = true;
				else if (byte == CHAR_MAX || byte > 127)
					break;
				else
					size = byte;
			} else
				repeating = true;
			if (size == 0)
				break;
			position += size;
			if (position >= integer.size())
				break;
			breaks.push_back(position);
		}
	}

	std::string result = negative ? conventions.minus : std::string();
	for (std::size_t i = 0; i < integer.size(); i++) {
		const std::size_t fromRight = integer.size() - i;
		if (i > 0 && std::find(breaks.begin(), breaks.end(), fromRight) != breaks.end())
			result += conventions.group;
		result.push_back(integer[i]);
	}
	if (point != std::string_view::npos) {
		result += conventions.decimal;
		result += number.substr(point + 1);
	}
	return result;
}


} // namespace ngui::locale
