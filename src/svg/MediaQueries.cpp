/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/svg/MediaQueries.h>

#include <cstddef>
#include <optional>
#include <vector>


namespace ngui::svg {


namespace {


constexpr std::string_view kMedia = "@media";
constexpr std::size_t kNone = std::string_view::npos;


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


bool
IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}


std::string_view
Trimmed(std::string_view text)
{
	while (!text.empty() && IsSpace(text.front()))
		text.remove_prefix(1);
	while (!text.empty() && IsSpace(text.back()))
		text.remove_suffix(1);
	return text;
}


// The next word or parenthesized feature of \a text, removed from it.
std::string_view
NextTerm(std::string_view& text)
{
	text = Trimmed(text);
	std::size_t end = 0;
	if (!text.empty() && text.front() == '(') {
		end = text.find(')');
		end = end == kNone ? text.size() : end + 1;
	} else {
		while (end < text.size() && !IsSpace(text[end]) && text[end] != '(')
			end++;
	}
	const std::string_view term = text.substr(0, end);
	text.remove_prefix(end);
	return term;
}


// A CSS number without sign or exponent ("21.07", ".5"); std::nullopt for
// anything else. By hand: libc++ before 20 has no from_chars() for float.
std::optional<float>
Number(std::string_view text)
{
	double value = 0;
	double fraction = 1;
	bool point = false;
	bool digits = false;
	for (const char c : text) {
		if (c == '.' && !point) {
			point = true;
		} else if (c >= '0' && c <= '9') {
			digits = true;
			if (point) {
				fraction /= 10;
				value += (c - '0') * fraction;
			} else {
				value = value * 10 + (c - '0');
			}
		} else {
			return std::nullopt;
		}
	}
	if (!digits)
		return std::nullopt;
	return static_cast<float>(value);
}


// Whether the feature "(name:value)" holds for \a width.
bool
FeatureHolds(std::string_view feature, float width)
{
	feature = Trimmed(feature.substr(1, feature.size() >= 2 ? feature.size() - 2 : 0));
	const std::size_t colon = feature.find(':');
	if (colon == kNone)
		return false;
	const std::string_view name = Trimmed(feature.substr(0, colon));
	std::string_view value = Trimmed(feature.substr(colon + 1));
	if (value.size() < 2 || !EqualsIgnoringCase(value.substr(value.size() - 2), "px"))
		return false;
	value.remove_suffix(2);
	const std::optional<float> limit = Number(value);
	if (!limit.has_value())
		return false;
	if (EqualsIgnoringCase(name, "min-width"))
		return width >= *limit;
	if (EqualsIgnoringCase(name, "max-width"))
		return width <= *limit;
	return false;
}


// Whether one query of the list ("only screen and (max-width:20px)") holds.
bool
QueryHolds(std::string_view query, float width)
{
	bool expectTerm = true;
	bool any = false;
	bool first = true;
	while (!(query = Trimmed(query)).empty()) {
		const std::string_view term = NextTerm(query);
		if (term.empty())
			return false;
		if (!expectTerm) {
			if (!EqualsIgnoringCase(term, "and"))
				return false;
			expectTerm = true;
			continue;
		}
		if (term.front() == '(') {
			if (!FeatureHolds(term, width))
				return false;
		} else if (first && EqualsIgnoringCase(term, "only")) {
			first = false;
			continue;
		} else if (!EqualsIgnoringCase(term, "all") && !EqualsIgnoringCase(term, "screen")) {
			return false;
		}
		first = false;
		any = true;
		expectTerm = false;
	}
	return any && !expectTerm;
}


bool
ConditionHolds(std::string_view condition, float width)
{
	std::size_t start = 0;
	while (start <= condition.size()) {
		std::size_t comma = condition.find(',', start);
		if (comma == kNone)
			comma = condition.size();
		if (QueryHolds(condition.substr(start, comma - start), width))
			return true;
		start = comma + 1;
	}
	return false;
}


bool
StartsMedia(std::string_view text, std::size_t at)
{
	return at + kMedia.size() <= text.size()
		&& EqualsIgnoringCase(text.substr(at, kMedia.size()), kMedia);
}


} // namespace


std::string
ResolveMediaQueries(std::string_view svg, float width)
{
	// the closing brace of each opening one, in one pass (kNone for one
	// left open)
	std::vector<std::size_t> closing(svg.size(), kNone);
	{
		std::vector<std::size_t> open;
		for (std::size_t i = 0; i < svg.size(); i++) {
			if (svg[i] == '{') {
				open.push_back(i);
			} else if (svg[i] == '}' && !open.empty()) {
				closing[open.back()] = i;
				open.pop_back();
			}
		}
	}

	std::string result;
	result.reserve(svg.size());
	// the closing braces of the rules kept, innermost last
	std::vector<std::size_t> dropBrace;
	std::size_t i = 0;
	while (i < svg.size()) {
		// a brace passed over (by a rule whose block began past it) stays
		while (!dropBrace.empty() && dropBrace.back() < i)
			dropBrace.pop_back();
		if (!dropBrace.empty() && i == dropBrace.back()) {
			dropBrace.pop_back();
			i++;
			continue;
		}
		if (!StartsMedia(svg, i)) {
			result += svg[i];
			i++;
			continue;
		}
		const std::size_t brace = svg.find('{', i + kMedia.size());
		// a rule without a block or its end takes the rest of the text
		if (brace == kNone)
			break;
		const std::size_t end = closing[brace];
		const bool holds = ConditionHolds(svg.substr(i + kMedia.size(),
			brace - i - kMedia.size()), width);
		if (end == kNone) {
			if (holds) {
				i = brace + 1;
				continue;
			}
			break;
		}
		if (holds) {
			dropBrace.push_back(end);
			i = brace + 1;
		} else {
			i = end + 1;
		}
	}
	return result;
}


} // namespace ngui::svg
