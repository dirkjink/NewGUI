/*
 * Copyright 2001-2015, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/InterfaceDefs.cpp (commit
 * b1d497bb69), truncate_string(). The algorithm is Haiku's, line by line;
 * BString's character functions became operations on the code points of a
 * std::string.
 *
 * Authors (Haiku):
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Caz <turok2@currantbun.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Wim van der Meer <WPJvanderMeer@gmail.com>
 *		Joseph Groover <looncraz@looncraz.net>
 */


#include "TruncateString.h"

#include <cstdint>
#include <vector>


namespace ngui {


namespace {


const char* const B_UTF8_ELLIPSIS = "\xE2\x80\xA6";


// The code points of a string, as BString counts its "chars".
class Chars {
public:
	explicit Chars(std::string_view string)
		:
		fString(string)
	{
		for (std::size_t i = 0; i < string.size(); i++) {
			if ((static_cast<unsigned char>(string[i]) & 0xc0) != 0x80)
				fStarts.push_back(i);
		}
	}

	int32_t Count() const
	{
		return static_cast<int32_t>(fStarts.size());
	}

	// The code points [first, first + count).
	std::string_view Range(int32_t first, int32_t count) const
	{
		const auto begin = static_cast<std::size_t>(first);
		const auto end = static_cast<std::size_t>(first + count);
		const std::size_t from = begin < fStarts.size() ? fStarts[begin] : fString.size();
		const std::size_t to = end < fStarts.size() ? fStarts[end] : fString.size();
		return fString.substr(from, to - from);
	}

private:
	std::string_view fString;
	std::vector<std::size_t> fStarts;
};


} // namespace


std::string
TruncateString(std::string_view text, TruncationMode mode, float width,
	std::span<const float> escapementArray, float fontSize, float ellipsisWidth)
{
	const Chars chars(text);
	const int32_t charCount = chars.Count();
	if (static_cast<std::size_t>(charCount) > escapementArray.size())
		return std::string(text);
	const auto escapement = [&](int32_t i) {
		return escapementArray[static_cast<std::size_t>(i)];
	};

	// add a tiny amount to the width to make floating point inaccuracy
	// not drop chars that would actually fit exactly
	width += 1.f / 128;

	switch (mode) {
		case TruncationMode::Beginning:
		{
			float totalWidth = 0;
			for (int32_t i = charCount - 1; i >= 0; i--) {
				float charWidth = escapement(i) * fontSize;
				if (totalWidth + charWidth > width) {
					// we need to truncate
					while (totalWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (++i == charCount) {
							// we've reached the end of the string and still
							// no space, so return an empty string
							return std::string();
						}

						totalWidth -= escapement(i) * fontSize;
					}

					return B_UTF8_ELLIPSIS
						+ std::string(chars.Range(i + 1, charCount - (i + 1)));
				}

				totalWidth += charWidth;
			}

			break;
		}

		case TruncationMode::End:
		{
			float totalWidth = 0;
			for (int32_t i = 0; i < charCount; i++) {
				float charWidth = escapement(i) * fontSize;
				if (totalWidth + charWidth > width) {
					// we need to truncate
					while (totalWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (i-- == 0) {
							// we've reached the start of the string and still
							// no space, so return an empty string
							return std::string();
						}

						totalWidth -= escapement(i) * fontSize;
					}

					return std::string(chars.Range(0, i)) + B_UTF8_ELLIPSIS;
				}

				totalWidth += charWidth;
			}

			break;
		}

		case TruncationMode::Middle:
		{
			float leftWidth = 0;
			float rightWidth = 0;
			int32_t leftIndex = 0;
			int32_t rightIndex = charCount - 1;
			bool left = true;

			for (int32_t i = 0; i < charCount; i++) {
				float charWidth
					= escapement(left ? leftIndex : rightIndex) * fontSize;

				if (leftWidth + rightWidth + charWidth > width) {
					// we need to truncate
					while (leftWidth + rightWidth + ellipsisWidth > width) {
						// remove chars until there's enough space for the
						// ellipsis
						if (leftIndex == 0 && rightIndex == charCount - 1) {
							// we've reached both ends of the string and still
							// no space, so return an empty string
							return std::string();
						}

						if (leftIndex > 0 && (rightIndex == charCount - 1
								|| leftWidth > rightWidth)) {
							// remove char on the left
							leftWidth -= escapement(--leftIndex)
								* fontSize;
						} else {
							// remove char on the right
							rightWidth -= escapement(++rightIndex)
								* fontSize;
						}
					}

					return std::string(chars.Range(0, leftIndex)) + B_UTF8_ELLIPSIS
						+ std::string(chars.Range(rightIndex + 1, charCount - (rightIndex + 1)));
				}

				if (left) {
					leftIndex++;
					leftWidth += charWidth;
				} else {
					rightIndex--;
					rightWidth += charWidth;
				}

				left = rightWidth > leftWidth;
			}

			break;
		}
	}

	// we've run through without the need to truncate, leave the string as it is
	return std::string(text);
}


} // namespace ngui
