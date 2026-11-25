/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <span>
#include <string>
#include <string_view>


namespace ngui::locale {


/*!	How a locale writes numbers (C's lconv, Windows' LOCALE_S*). */
struct NumberConventions {
	std::string			decimal = ".";		/*!< The decimal separator. */
	std::string			group;				/*!< The grouping separator; empty: none. */
	std::string			grouping;			/*!< The sizes of the groups from the right,
												as C's lconv::grouping: each byte a size,
												the last repeated; empty or a byte <= 0 or
												CHAR_MAX: no more groups. */
	std::string			minus = "-";		/*!< The minus sign. */
};


/*!	\a number, digits with an optional leading "-" and an optional "." and
	fraction (as std::to_chars writes them), in \a conventions: its integer
	digits grouped, its separators and sign theirs. Anything else (inf,
	nan) as it is. */
std::string LocalizeNumber(std::string_view number, const NumberConventions& conventions);


} // namespace ngui::locale
