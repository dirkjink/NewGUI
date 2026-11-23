/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The HTML reports of failed snapshots (specification 20.5): one page per
// snapshot in the report directory, with its files next to it, so that the
// directory can be kept as a CI artifact and opened in a browser.


#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>


namespace ngui::test {


// <stem>.html with <stem>.reference.png, <stem>.actual.png and
// <stem>.difference.png, as far as they exist. Returns the page, or an
// empty path if it cannot be written.
std::filesystem::path			WriteImageReport(const std::filesystem::path& directory,
									const std::string& stem, const Image* reference,
									const Image& actual, const ImageComparison* comparison,
									std::string_view summary);

// <stem>.html with a line diff and <stem>.actual.<extension>.
std::filesystem::path			WriteTextReport(const std::filesystem::path& directory,
									const std::string& stem, std::string_view extension,
									const std::string* reference, std::string_view actual);


// A line of a diff: ' ' in both, '-' only in the reference, '+' only in the
// result.
struct DiffLine {
	char			kind;
	std::string		text;
};

// The shortest line diff (longest common subsequence). Above 4,000 lines on
// a side it gives up and lists all of the reference, then all of the
// result.
std::vector<DiffLine>			DiffLines(std::string_view reference, std::string_view actual);

// Escapes &, <, > and " for HTML.
std::string						EscapeHtml(std::string_view text);


} // namespace ngui::test
