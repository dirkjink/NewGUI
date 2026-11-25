/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/bin/hey.cpp (commit b1d497bb69, public domain, by
// Attila Mezei and others): Hey()'s verbs, add_specifier(), add_data() and
// add_with(), and the printing of replies, over std::string instead of
// argv and with the differences Hey.h lists.


#include "Hey.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <vector>

#include <ngui/AppDefs.h>


namespace ngui::remote::detail {


namespace {


bool
Equals(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
		[](char x, char y) {
			return std::tolower(static_cast<unsigned char>(x))
				== std::tolower(static_cast<unsigned char>(y));
		});
}


bool
StartsWith(std::string_view text, std::string_view prefix)
{
	return text.size() >= prefix.size() && Equals(text.substr(0, prefix.size()), prefix);
}


Error
Syntax(std::string message)
{
	return {ErrorCode::BadScriptSyntax, std::move(message)};
}


// The words of a command and where the parser is.
class Words {
public:
	explicit Words(std::span<const std::string> words)
		:
		fWords(words)
	{
	}

	// The current word, nullptr at the end.
	const std::string* Current() const
	{
		return fAt < fWords.size() ? &fWords[fAt] : nullptr;
	}

	void Next()
	{
		fAt++;
	}

	void Back()
	{
		fAt--;
	}

private:
	std::span<const std::string> fWords;
	std::size_t			fAt = 0;
};


bool
AllDigits(std::string_view text)
{
	return !text.empty() && std::ranges::all_of(text,
		[](char c) { return c >= '0' && c <= '9'; });
}


// The integer at the start of \a text, clamped to \a Integer's range
// (strtoll() stops at the first character that is no digit, as hey's
// atol() does).
template<typename Integer = std::int32_t>
Integer
Number(std::string_view text)
{
	const long long value = std::strtoll(std::string(text).c_str(), nullptr, 10);
	return static_cast<Integer>(std::clamp<long long>(value,
		std::numeric_limits<Integer>::min(), std::numeric_limits<Integer>::max()));
}


// The integer inside "type(…)".
template<typename Integer>
Integer
Inside(std::string_view text)
{
	const std::size_t open = text.find_first_of("([");
	return open == std::string_view::npos ? 0 : Number<Integer>(text.substr(open + 1));
}


// hey's add_specifier(): one specifier; false when there are no more
// (the next word is "to", "with" or the end), an error on bad syntax.
std::expected<bool, Error>
AddSpecifier(BMessage& message, Words& words)
{
	const std::string* property = words.Current();
	if (property == nullptr)
		return false;
	words.Next();
	if (Equals(*property, "to"))
		return false;
	if (Equals(*property, "with")) {
		words.Back();
		return false;
	}
	if (Equals(*property, "of") || Equals(*property, "the")) {
		const bool of = Equals(*property, "of");
		property = words.Current();
		if (property == nullptr)
			return std::unexpected(Syntax("a property after \"of\" or \"the\""));
		words.Next();
		if (of && Equals(*property, "the")) {
			property = words.Current();
			if (property == nullptr)
				return std::unexpected(Syntax("a property after \"the\""));
			words.Next();
		}
	}

	// create is always direct (a "with" after it is no name); otherwise the
	// word after the property
	const std::string* specifier = message.what == kCreateProperty ? nullptr
		: words.Current();
	if (specifier == nullptr) {
		(void)message.AddSpecifier(*property);
		return false;
	}
	words.Next();
	if (Equals(*specifier, "of")) {
		// NewGUI: an "of" at the end is an error (hey ignores it)
		if (words.Current() == nullptr)
			return std::unexpected(Syntax("a specifier after \"of\""));
		(void)message.AddSpecifier(*property);
		return true;
	}
	if (Equals(*specifier, "to") || Equals(*specifier, "with")) {
		(void)message.AddSpecifier(*property);
		if (Equals(*specifier, "with"))
			words.Back();
		return false;
	}

	if (specifier->starts_with('[')) {
		// index, reverse index or range
		if (specifier->size() > 1 && (*specifier)[1] == '-') {
			BMessage reverse(kReverseIndexSpecifier);
			(void)reverse.AddString("property", *property);
			(void)reverse.AddInt32("index", Number(std::string_view(*specifier).substr(2)));
			(void)message.AddSpecifier(reverse);
			return true;
		}
		const std::string_view inside = std::string_view(*specifier).substr(1);
		const std::int32_t first = Number(inside);
		if (inside.ends_with(']')) {
			(void)message.AddSpecifier(*property, first);
			return true;
		}
		const std::string* to = words.Current();
		if (to == nullptr) {
			(void)message.AddSpecifier(*property, first);
			return true;
		}
		words.Next();
		const std::string* end = words.Current();
		if (!Equals(*to, "to") || end == nullptr)
			return std::unexpected(Syntax("a range is [<index> to <end>]"));
		words.Next();
		const std::int32_t last = Number(*end);
		(void)message.AddSpecifier(*property, first, last - first > 0 ? last - first : 1);
		return true;
	}

	// an index if only digits (a reverse one with a minus), else a name;
	// a name of digits is quoted
	const bool reverse = specifier->starts_with('-');
	if (AllDigits(reverse ? std::string_view(*specifier).substr(1) : *specifier)) {
		if (reverse) {
			BMessage reverseIndex(kReverseIndexSpecifier);
			(void)reverseIndex.AddString("property", *property);
			(void)reverseIndex.AddInt32("index", Number(std::string_view(*specifier).substr(1)));
			(void)message.AddSpecifier(reverseIndex);
		} else
			(void)message.AddSpecifier(*property, Number(*specifier));
		return true;
	}
	std::string_view name = *specifier;
	if (name.starts_with('"')) {
		name.remove_prefix(1);
		if (name.ends_with('"'))
			name.remove_suffix(1);
	}
	(void)message.AddSpecifier(*property, name);
	return true;
}


// The numbers inside "type(a, b, …)", as floats.
std::vector<float>
Arguments(std::string_view text)
{
	std::vector<float> values;
	const std::size_t open = text.find_first_of("([");
	if (open == std::string_view::npos)
		return values;
	const std::string inside(text.substr(open + 1));
	const char* at = inside.c_str();
	while (*at != '\0') {
		char* end = nullptr;
		const float value = std::strtof(at, &end);
		if (end == at)
			break;
		values.push_back(value);
		at = end;
		while (*at == ',' || *at == ' ')
			at++;
	}
	return values;
}


// hey's add_with(): "with name=value [and name=value]*".
std::expected<void, Error>
AddWith(BMessage& message, Words& words)
{
	const std::string* with = words.Current();
	if (with == nullptr || !Equals(*with, "with"))
		return {};
	words.Next();
	while (true) {
		const std::string* value = words.Current();
		if (value == nullptr)
			return std::unexpected(Syntax("a value after \"with\" or \"and\""));
		if (auto added = AddHeyValue(message, *value); !added)
			return std::unexpected(Syntax(added.error().message));
		words.Next();
		const std::string* andWord = words.Current();
		if (andWord == nullptr || !Equals(*andWord, "and"))
			return {};
		words.Next();
	}
}


} // namespace


std::expected<void, Error>
AddHeyValue(BMessage& message, const std::string& value)
{
	if (value.empty())
		return std::unexpected(Syntax("an empty value"));
	// an integer or a float
	bool number = true;
	bool floating = false;
	for (std::size_t i = 0; i < value.size(); i++) {
		if (i == 0 && value[i] == '-')
			continue;
		if (value[i] == '.')
			floating = true;
		else if (value[i] < '0' || value[i] > '9') {
			number = false;
			break;
		}
	}
	if (number && value != "-") {
		if (floating)
			return message.AddFloat("data", std::strtof(value.c_str(), nullptr));
		return message.AddInt32("data", Number(value));
	}
	if (Equals(value, "true") || Equals(value, "false"))
		return message.AddBool("data", Equals(value, "true"));

	// "<name>=" chooses the field
	std::string name = "data";
	std::string_view text = value;
	if (const std::size_t equals = value.find('=', 1); equals != std::string::npos) {
		name = value.substr(0, equals);
		text = std::string_view(value).substr(equals + 1);
	}
	const std::vector<float> numbers = Arguments(text);
	const auto at = [&](std::size_t i) { return i < numbers.size() ? numbers[i] : 0.0f; };
	// integers as integers (hey: atol()), clamped to their type
	if (StartsWith(text, "int8("))
		return message.AddInt8(name, Inside<std::int8_t>(text));
	if (StartsWith(text, "int16("))
		return message.AddInt16(name, Inside<std::int16_t>(text));
	if (StartsWith(text, "int32("))
		return message.AddInt32(name, Inside<std::int32_t>(text));
	if (StartsWith(text, "int64("))
		return message.AddInt64(name, Inside<std::int64_t>(text));
	if (StartsWith(text, "bool(")) {
		const std::string_view inside = text.substr(5);
		return message.AddBool(name, StartsWith(inside, "true")
			|| (!StartsWith(inside, "false") && at(0) != 0));
	}
	if (StartsWith(text, "float("))
		return message.AddFloat(name, at(0));
	if (StartsWith(text, "double("))
		return message.AddDouble(name, static_cast<double>(at(0)));
	if (StartsWith(text, "BPoint"))
		return message.AddPoint(name, {at(0), at(1)});
	if (StartsWith(text, "BRect"))
		return message.AddRect(name, {at(0), at(1), at(2), at(3)});
	if (StartsWith(text, "rgb_color")) {
		return message.AddColor(name, {at(0) / 255.0f, at(1) / 255.0f, at(2) / 255.0f,
			numbers.size() > 3 ? at(3) / 255.0f : 1.0f});
	}
	// a string, quoted or not
	std::string_view string = text;
	if (string.starts_with('"')) {
		string.remove_prefix(1);
		if (string.ends_with('"'))
			string.remove_suffix(1);
	}
	return message.AddString(name, string);
}


std::expected<BMessage, Error>
ParseHeyCommand(std::span<const std::string> words)
{
	if (words.empty())
		return std::unexpected(Syntax("a verb"));
	struct Verb {
		std::string_view	word;
		std::uint32_t		what;
	};
	static constexpr Verb kVerbs[] = {{"do", kExecuteProperty}, {"get", kGetProperty},
		{"set", kSetProperty}, {"create", kCreateProperty}, {"delete", kDeleteProperty},
		{"count", kCountProperties}, {"getsuites", kGetSupportedSuites}};
	const auto verb = std::ranges::find_if(kVerbs,
		[&](const Verb& candidate) { return Equals(candidate.word, words[0]); });
	if (verb == std::end(kVerbs))
		return std::unexpected(Syntax("bad verb \"" + words[0] + "\""));

	BMessage message(verb->what);
	Words rest(words.subspan(1));
	while (true) {
		const auto more = AddSpecifier(message, rest);
		if (!more.has_value())
			return std::unexpected(more.error());
		if (!*more)
			break;
	}
	// set takes "to <value>" (the "to" was taken with the specifiers)
	if (message.what == kSetProperty) {
		const std::string* value = rest.Current();
		if (value == nullptr)
			return std::unexpected(Syntax("set needs \"to <value>\""));
		if (Equals(*value, "to")) {
			rest.Next();
			value = rest.Current();
			if (value == nullptr)
				return std::unexpected(Syntax("a value after \"to\""));
		}
		if (auto added = AddHeyValue(message, *value); !added)
			return std::unexpected(Syntax(added.error().message));
		rest.Next();
	}
	if (auto with = AddWith(message, rest); !with)
		return std::unexpected(with.error());
	if (const std::string* extra = rest.Current())
		return std::unexpected(Syntax("unexpected \"" + *extra + "\""));
	return message;
}


// #pragma mark - Printing


namespace {


std::string
CodeName(std::uint32_t code)
{
	struct Name {
		std::uint32_t		code;
		const char*			name;
	};
	static constexpr Name kNames[] = {{kReply, "B_REPLY"},
		{kMessageNotUnderstood, "B_MESSAGE_NOT_UNDERSTOOD"}, {kNoReply, "B_NO_REPLY"},
		{kGetProperty, "B_GET_PROPERTY"}, {kSetProperty, "B_SET_PROPERTY"},
		{kCreateProperty, "B_CREATE_PROPERTY"}, {kDeleteProperty, "B_DELETE_PROPERTY"},
		{kCountProperties, "B_COUNT_PROPERTIES"}, {kExecuteProperty, "B_EXECUTE_PROPERTY"},
		{kGetSupportedSuites, "B_GET_SUPPORTED_SUITES"}};
	for (const Name& name : kNames) {
		if (name.code == code)
			return name.name;
	}
	std::string text = "'";
	for (int shift = 24; shift >= 0; shift -= 8)
		text += static_cast<char>((code >> shift) & 0xff);
	return text + "'";
}


std::string
TypeName(BTypeCode type)
{
	switch (type) {
		case BTypeCode::Bool: return "B_BOOL_TYPE";
		case BTypeCode::Int8: return "B_INT8_TYPE";
		case BTypeCode::Int16: return "B_INT16_TYPE";
		case BTypeCode::Int32: return "B_INT32_TYPE";
		case BTypeCode::Int64: return "B_INT64_TYPE";
		case BTypeCode::UInt8: return "B_UINT8_TYPE";
		case BTypeCode::UInt16: return "B_UINT16_TYPE";
		case BTypeCode::UInt32: return "B_UINT32_TYPE";
		case BTypeCode::UInt64: return "B_UINT64_TYPE";
		case BTypeCode::Float: return "B_FLOAT_TYPE";
		case BTypeCode::Double: return "B_DOUBLE_TYPE";
		case BTypeCode::String: return "B_STRING_TYPE";
		case BTypeCode::Point: return "B_POINT_TYPE";
		case BTypeCode::Rect: return "B_RECT_TYPE";
		case BTypeCode::Size: return "B_SIZE_TYPE";
		case BTypeCode::Color: return "RGB_FLOAT_TYPE";
		case BTypeCode::Message: return "B_MESSAGE_TYPE";
		default: return CodeName(static_cast<std::uint32_t>(type));
	}
}


std::string
Format(const char* format, double a, double b = 0, double c = 0, double d = 0)
{
	char buffer[128];
	(void)std::snprintf(buffer, sizeof(buffer), format, a, b, c, d);
	return buffer;
}


// One item of a field, as hey's format_data() writes it.
std::string
ItemText(const BMessage& message, const BFieldInfo& field, std::int32_t index)
{
	const std::string_view name = field.name;
	switch (field.type) {
		case BTypeCode::Bool:
			return message.FindBool(name, index).value_or(false) ? "TRUE" : "FALSE";
		case BTypeCode::Int8:
			return std::to_string(message.FindInt8(name, index).value_or(0));
		case BTypeCode::Int16:
			return std::to_string(message.FindInt16(name, index).value_or(0));
		case BTypeCode::Int32:
		{
			const std::int32_t value = message.FindInt32(name, index).value_or(0);
			char buffer[48];
			(void)std::snprintf(buffer, sizeof(buffer), "%" PRId32 " (0x%08" PRIx32 ")", value,
				static_cast<std::uint32_t>(value));
			return buffer;
		}
		case BTypeCode::Int64:
			return std::to_string(message.FindInt64(name, index).value_or(0));
		case BTypeCode::UInt32:
			return std::to_string(message.FindUInt32(name, index).value_or(0));
		case BTypeCode::Float:
			return Format("%.3f", static_cast<double>(message.FindFloat(name, index).value_or(0)));
		case BTypeCode::Double:
			return Format("%.3f", message.FindDouble(name, index).value_or(0));
		case BTypeCode::String:
			return "\"" + std::string(message.FindString(name, index).value_or("")) + "\"";
		case BTypeCode::Point:
		{
			const gfx::Point point = message.FindPoint(name, index).value_or(gfx::Point{});
			return Format("BPoint(%.1f, %.1f)", static_cast<double>(point.x),
				static_cast<double>(point.y));
		}
		case BTypeCode::Rect:
		{
			const gfx::Rect rect = message.FindRect(name, index).value_or(gfx::Rect{});
			return Format("BRect(%.1f, %.1f, %.1f, %.1f)", static_cast<double>(rect.x0),
				static_cast<double>(rect.y0), static_cast<double>(rect.x1),
				static_cast<double>(rect.y1));
		}
		case BTypeCode::Color:
		{
			const gfx::Color color = message.FindColor(name, index).value_or(gfx::Color{});
			return Format("rgb_color(%.0f, %.0f, %.0f, %.0f)",
				static_cast<double>(color.r * 255), static_cast<double>(color.g * 255),
				static_cast<double>(color.b * 255), static_cast<double>(color.a * 255));
		}
		default:
			return "(" + TypeName(field.type) + ")";
	}
}


std::string
FormName(std::uint32_t form)
{
	switch (form) {
		case kDirectSpecifier: return "DIRECT";
		case kIndexSpecifier: return "INDEX";
		case kReverseIndexSpecifier: return "REV.INDEX";
		case kRangeSpecifier: return "RANGE";
		case kReverseRangeSpecifier: return "REV.RANGE";
		case kNameSpecifier: return "NAME";
		case kIdSpecifier: return "ID";
		default: return std::to_string(form);
	}
}


// A property table (BPropertyInfo::ToMessage()), as hey prints it.
std::string
PropertyTable(const BMessage& table)
{
	std::string text = "        property   commands                            specifiers\n"
		"--------------------------------------------------------------------------------\n";
	for (std::int32_t i = 0; table.HasMessage("property", i); i++) {
		const BMessage property = table.FindMessage("property", i).value_or(BMessage());
		const std::string name(property.GetString("name", ""));
		std::string line = std::string(name.size() < 16 ? 16 - name.size() : 0, ' ') + name
			+ "   ";
		std::string commands;
		for (std::int32_t c = 0; property.HasUInt32("commands", c); c++)
			commands += CodeName(property.FindUInt32("commands", c).value_or(0)) + " ";
		if (commands.empty())
			commands = "(leads on) ";
		line += commands + std::string(commands.size() < 36 ? 36 - commands.size() : 1, ' ');
		std::string forms;
		for (std::int32_t f = 0; property.HasUInt32("specifiers", f); f++)
			forms += FormName(property.FindUInt32("specifiers", f).value_or(0)) + " ";
		text += line + (forms.empty() ? "(any)" : forms) + "\n";
		if (const std::string_view usage = property.GetString("usage", ""); !usage.empty())
			text += "                   Usage: " + std::string(usage) + "\n";
	}
	return text;
}


} // namespace


std::string
DescribeReply(const BMessage& reply)
{
	std::string text = "Reply BMessage(" + CodeName(reply.what) + "):\n";
	for (const BFieldInfo& field : reply.Fields()) {
		for (std::int32_t i = 0; i < field.count; i++) {
			if (field.type == BTypeCode::Message && field.name == "messages") {
				text += "   \"messages\" (property table) :\n"
					+ PropertyTable(reply.FindMessage("messages", i).value_or(BMessage()));
				continue;
			}
			text += "   \"" + field.name + "\" (" + TypeName(field.type) + ") : "
				+ ItemText(reply, field, i) + "\n";
		}
	}
	return text;
}


std::string
ResultText(const BMessage& reply)
{
	std::string text;
	for (const BFieldInfo& field : reply.Fields()) {
		if (field.name != "result")
			continue;
		for (std::int32_t i = 0; i < field.count; i++) {
			std::string item = ItemText(reply, field, i);
			// the bare value, as hey -o prints it
			if (field.type == BTypeCode::String && item.size() >= 2)
				item = item.substr(1, item.size() - 2);
			else if (field.type == BTypeCode::Int32)
				item = item.substr(0, item.find(' '));
			text += item + "\n";
		}
	}
	return text;
}


} // namespace ngui::remote::detail
