/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

#include <ngui/Error.h>


namespace ngui::locale {


/*!	The plural rule of a gettext catalog (its header's "Plural-Forms:
	nplurals=3; plural=...;"): which of the translations of a plural
	message a count takes. The formula is a C expression over n with
	integers, ! * / % + - < > <= >= == != && || ?: and parentheses;
	division by zero gives 0. Without a rule (or for a broken one), the
	Germanic rule: n == 1 the first, else the second.
*/
class PluralForms {
public:
	/*!	The most forms a rule may have. */
	static constexpr std::size_t kMaxForms = 16;

								PluralForms() = default;
									/*!< The Germanic rule: 2 forms, n != 1. */

	static	std::expected<PluralForms, Error> Parse(std::string_view header);
									/*!< The rule of a "Plural-Forms" value ("nplurals=2;
										plural=(n != 1);"); BadData for a broken one: no or
										too many forms, a formula that is not one, nested
										more than 64 levels or longer than 512 parts. */

			std::size_t			Count() const;
									/*!< The number of forms. */
			std::size_t			FormFor(std::uint64_t n) const;
									/*!< The form for \a n, below Count(). */

private:
	struct Node {
		enum class Kind : std::uint8_t {
			Number, Variable, Not, Multiply, Divide, Remainder, Add, Subtract, Less,
			Greater, LessEqual, GreaterEqual, Equal, NotEqual, And, Or, Choice
		};
		Kind			kind = Kind::Number;
		std::uint64_t	value = 0;
		std::uint32_t	operands[3] = {0, 0, 0};
	};
	class Parser;

			std::uint64_t		_Evaluate(std::uint32_t node, std::uint64_t n) const;

			std::size_t			fCount = 2;
			std::vector<Node>	fNodes;
									// empty: n != 1; else the root is the last
};


} // namespace ngui::locale
