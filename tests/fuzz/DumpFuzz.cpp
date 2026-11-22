/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Dump.h>


namespace {


// A strict JSON parser that only answers "well-formed or not" (RFC 8259):
// objects, arrays, strings with valid escapes and UTF-8, numbers, true,
// false, null.
class JsonChecker {
public:
								JsonChecker(std::string_view text);

			bool				IsValid();

private:
			bool				_Value(int depth);
			bool				_String();
			bool				_Number();
			bool				_Literal(std::string_view word);
			bool				_Utf8();
			char				_Peek() const;

			std::string_view	fText;
			std::size_t			fAt = 0;
};


JsonChecker::JsonChecker(std::string_view text)
	:
	fText(text)
{
}


bool
JsonChecker::IsValid()
{
	return _Value(0) && fAt == fText.size();
}


char
JsonChecker::_Peek() const
{
	return fAt < fText.size() ? fText[fAt] : '\0';
}


bool
JsonChecker::_Value(int depth)
{
	if (depth > 200)
		return false;
	switch (_Peek()) {
		case '{':
			fAt++;
			if (_Peek() == '}') {
				fAt++;
				return true;
			}
			while (true) {
				if (!_String() || _Peek() != ':')
					return false;
				fAt++;
				if (!_Value(depth + 1))
					return false;
				if (_Peek() == '}') {
					fAt++;
					return true;
				}
				if (_Peek() != ',')
					return false;
				fAt++;
			}
		case '[':
			fAt++;
			if (_Peek() == ']') {
				fAt++;
				return true;
			}
			while (true) {
				if (!_Value(depth + 1))
					return false;
				if (_Peek() == ']') {
					fAt++;
					return true;
				}
				if (_Peek() != ',')
					return false;
				fAt++;
			}
		case '"':
			return _String();
		case 't':
			return _Literal("true");
		case 'f':
			return _Literal("false");
		case 'n':
			return _Literal("null");
		default:
			return _Number();
	}
}


bool
JsonChecker::_String()
{
	if (_Peek() != '"')
		return false;
	fAt++;
	while (fAt < fText.size()) {
		const auto c = static_cast<unsigned char>(fText[fAt]);
		if (c == '"') {
			fAt++;
			return true;
		}
		if (c < 0x20)
			return false;
		if (c == '\\') {
			fAt++;
			const char escape = _Peek();
			if (escape == 'u') {
				for (int i = 0; i < 4; i++) {
					fAt++;
					const char h = _Peek();
					if (!((h >= '0' && h <= '9') || (h >= 'a' && h <= 'f')
							|| (h >= 'A' && h <= 'F'))) {
						return false;
					}
				}
			} else if (std::string_view("\"\\/bfnrt").find(escape) == std::string_view::npos
				|| escape == '\0') {
				return false;
			}
			fAt++;
		} else if (c >= 0x80) {
			if (!_Utf8())
				return false;
		} else {
			fAt++;
		}
	}
	return false;
}


bool
JsonChecker::_Utf8()
{
	const auto byte = [&](std::size_t i) {
		return fAt + i < fText.size() ? static_cast<unsigned char>(fText[fAt + i]) : 0u;
	};
	const unsigned first = byte(0);
	const std::size_t length = (first & 0xe0) == 0xc0 ? 2 : (first & 0xf0) == 0xe0 ? 3
		: (first & 0xf8) == 0xf0 ? 4 : 0;
	if (length == 0)
		return false;
	for (std::size_t i = 1; i < length; i++) {
		if ((byte(i) & 0xc0) != 0x80)
			return false;
	}
	fAt += length;
	return true;
}


bool
JsonChecker::_Number()
{
	const std::size_t start = fAt;
	if (_Peek() == '-')
		fAt++;
	if (_Peek() == '0') {
		fAt++;
	} else if (_Peek() >= '1' && _Peek() <= '9') {
		while (_Peek() >= '0' && _Peek() <= '9')
			fAt++;
	} else {
		return false;
	}
	if (_Peek() == '.') {
		fAt++;
		if (!(_Peek() >= '0' && _Peek() <= '9'))
			return false;
		while (_Peek() >= '0' && _Peek() <= '9')
			fAt++;
	}
	return fAt > start;
}


bool
JsonChecker::_Literal(std::string_view word)
{
	if (fText.substr(fAt, word.size()) != word)
		return false;
	fAt += word.size();
	return true;
}


} // namespace


NGUI_FUZZ_TARGET(Dump)
{
	// What is tested: DumpText() and DumpJson() of every list Deserialize()
	// accepts from arbitrary bytes.
	// Success criteria: Neither crashes (checked by the sanitizers); the JSON
	// is well-formed according to a strict parser; the text is not empty and
	// ends with a newline.
	// Why it matters: Dumps of lists from outside are how one looks at a
	// suspicious remote-rendering stream; the JSON form is read by tools,
	// so it must stay valid whatever the list contains.
	//
	// Given
	const auto list = ngui::gfx::DisplayList::Deserialize(std::as_bytes(data));
	if (!list.has_value())
		return;

	// When
	const std::string text = ngui::gfx::DumpText(**list);
	const std::string json = ngui::gfx::DumpJson(**list);

	// Then
	NGUI_FUZZ_CHECK(!text.empty() && text.back() == '\n');
	NGUI_FUZZ_CHECK(JsonChecker(json).IsValid());
}
