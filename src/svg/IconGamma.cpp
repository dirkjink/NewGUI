/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "svg/IconGamma.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>


namespace ngui::svg::detail {


namespace {


// AGG's uround()
std::uint8_t
Round(double value)
{
	return static_cast<std::uint8_t>(std::clamp(static_cast<int>(value + 0.5), 0, 255));
}


char
Lower(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}


bool
IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}


int
HexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	c = Lower(c);
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}


constexpr char kHex[] = "0123456789abcdef";


// The properties whose values are colors.
constexpr std::string_view kColorProperties[] = {"stop-color", "flood-color",
	"lighting-color", "fill", "stroke", "color"};


// "#rgb", "#rgba", "#rrggbb", "#rrggbbaa" in linear light, as long; else
// std::nullopt.
std::optional<std::string>
LinearHex(std::string_view value)
{
	if (value.empty() || value[0] != '#')
		return std::nullopt;
	const std::size_t digits = value.size() - 1;
	if (digits != 3 && digits != 4 && digits != 6 && digits != 8)
		return std::nullopt;
	for (std::size_t i = 1; i < value.size(); i++) {
		if (HexDigit(value[i]) < 0)
			return std::nullopt;
	}
	const IconGamma& gamma = IconGamma::Haiku();
	const bool shortForm = digits == 3 || digits == 4;
	std::string result = "#";
	for (std::size_t component = 0; component < (digits == 3 || digits == 6 ? 3u : 4u);
			component++) {
		int byte = 0;
		if (shortForm) {
			const int digit = HexDigit(value[1 + component]);
			byte = digit * 16 + digit;
		} else {
			byte = HexDigit(value[1 + component * 2]) * 16 + HexDigit(value[2 + component * 2]);
		}
		// the alpha stays
		const int out = component < 3 ? gamma.dir[static_cast<std::size_t>(byte)] : byte;
		result += kHex[out / 16];
		result += kHex[out % 16];
	}
	return result;
}


// "rgb(r, g, b)" or "rgba(r, g, b, a)" with numbers or percentages in
// linear light, the components as whole numbers; else std::nullopt.
std::optional<std::string>
LinearRgb(std::string_view value)
{
	std::size_t open = value.find('(');
	if (open == std::string_view::npos || value.back() != ')')
		return std::nullopt;
	std::string name;
	for (const char c : value.substr(0, open))
		name += Lower(c);
	if (name != "rgb" && name != "rgba")
		return std::nullopt;
	std::string_view inner = value.substr(open + 1, value.size() - open - 2);
	const IconGamma& gamma = IconGamma::Haiku();
	std::string result = name + "(";
	for (int component = 0; component < 3; component++) {
		while (!inner.empty() && IsSpace(inner.front()))
			inner.remove_prefix(1);
		std::size_t end = 0;
		while (end < inner.size() && ((inner[end] >= '0' && inner[end] <= '9')
				|| inner[end] == '.')) {
			end++;
		}
		if (end == 0)
			return std::nullopt;
		double number = 0;
		double fraction = 0;
		bool point = false;
		for (const char c : inner.substr(0, end)) {
			if (c == '.') {
				if (point)
					return std::nullopt;
				point = true;
				fraction = 1;
			} else if (point) {
				fraction /= 10;
				number += (c - '0') * fraction;
			} else {
				number = number * 10 + (c - '0');
			}
		}
		inner.remove_prefix(end);
		if (!inner.empty() && inner.front() == '%') {
			number = number * 255 / 100;
			inner.remove_prefix(1);
		}
		result += std::to_string(gamma.dir[Round(std::min(number, 255.0))]);
		while (!inner.empty() && IsSpace(inner.front()))
			inner.remove_prefix(1);
		if (component < 2) {
			if (inner.empty() || inner.front() != ',')
				return std::nullopt;
			inner.remove_prefix(1);
			result += ",";
		}
	}
	// the alpha, if any, stays as written
	result += std::string(inner) + ")";
	return result;
}


std::optional<std::string>
LinearColor(std::string_view value)
{
	while (!value.empty() && IsSpace(value.front()))
		value.remove_prefix(1);
	while (!value.empty() && IsSpace(value.back()))
		value.remove_suffix(1);
	if (value.empty())
		return std::nullopt;
	if (value[0] == '#')
		return LinearHex(value);
	return LinearRgb(value);
}


// Whether \a text at \a at starts the property \a name, not inside a
// longer name ("fill" in "fill-opacity" or "data-fill").
bool
NameAt(std::string_view text, std::size_t at, std::string_view name)
{
	if (at + name.size() > text.size() || text.substr(at, name.size()) != name)
		return false;
	if (at > 0) {
		const char before = text[at - 1];
		if (before == '-' || before == '_' || (before >= 'a' && before <= 'z')
				|| (before >= 'A' && before <= 'Z') || (before >= '0' && before <= '9')) {
			return false;
		}
	}
	return true;
}


} // namespace


const IconGamma&
IconGamma::Haiku()
{
	static const IconGamma table = [] {
		// agg::gamma_lut<int8u, int8u>(2.2)
		IconGamma made{};
		for (std::size_t i = 0; i < 256; i++) {
			const double value = static_cast<double>(i) / 255.0;
			made.dir[i] = Round(std::pow(value, 2.2) * 255.0);
			made.inv[i] = Round(std::pow(value, 1.0 / 2.2) * 255.0);
		}
		return made;
	}();
	return table;
}


std::string
LinearizeColors(std::string_view svg)
{
	std::string result;
	result.reserve(svg.size());
	std::size_t i = 0;
	while (i < svg.size()) {
		std::string_view property;
		for (const std::string_view name : kColorProperties) {
			if (NameAt(svg, i, name)) {
				property = name;
				break;
			}
		}
		if (property.empty()) {
			result += svg[i++];
			continue;
		}
		// the property, then = and a quoted value (an attribute) or : and a
		// value up to ; " ' } or < (a declaration)
		std::size_t at = i + property.size();
		while (at < svg.size() && IsSpace(svg[at]))
			at++;
		std::size_t start = std::string_view::npos;
		std::size_t end = std::string_view::npos;
		if (at < svg.size() && svg[at] == '=') {
			at++;
			while (at < svg.size() && IsSpace(svg[at]))
				at++;
			if (at < svg.size() && (svg[at] == '"' || svg[at] == '\'')) {
				const std::size_t close = svg.find(svg[at], at + 1);
				if (close != std::string_view::npos) {
					start = at + 1;
					end = close;
				}
			}
		} else if (at < svg.size() && svg[at] == ':') {
			start = at + 1;
			end = svg.find_first_of(";\"'}<", start);
			if (end == std::string_view::npos)
				end = svg.size();
		}
		if (start == std::string_view::npos) {
			result += svg.substr(i, property.size());
			i += property.size();
			continue;
		}
		const std::string_view value = svg.substr(start, end - start);
		result += svg.substr(i, start - i);
		// "!important" and the like stay after the color
		const std::size_t bang = value.find('!');
		const std::string_view color = value.substr(0, bang);
		if (const auto linear = LinearColor(color)) {
			result += *linear;
			if (bang != std::string_view::npos)
				result += " " + std::string(value.substr(bang));
		} else {
			result += value;
		}
		i = end;
	}
	return result;
}


} // namespace ngui::svg::detail
