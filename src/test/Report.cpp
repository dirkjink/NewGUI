/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "Report.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <system_error>


namespace ngui::test {


namespace {


std::vector<std::string_view>
SplitLines(std::string_view text)
{
	std::vector<std::string_view> lines;
	std::size_t start = 0;
	while (start < text.size()) {
		const std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) {
			lines.push_back(text.substr(start));
			break;
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}


const char* const kStyle =
	"<style>"
	"body{font-family:sans-serif;margin:16px;background:#fff;color:#222}"
	"h1{font-size:18px}p{font-size:14px}"
	".images{display:flex;flex-wrap:wrap;gap:16px}"
	"figure{margin:0}figcaption{font-size:13px;margin-bottom:4px}"
	"img{image-rendering:pixelated;border:1px solid #888;background:"
	"repeating-conic-gradient(#ddd 0 25%,#fff 0 50%) 0 0/16px 16px}"
	"pre{font-size:12px;line-height:1.35;overflow-x:auto}"
	".del{background:#fdd}.ins{background:#dfd}"
	"</style>";


bool
WritePage(const std::filesystem::path& page, const std::string& title,
	const std::string& body)
{
	std::ofstream file(page, std::ios::binary | std::ios::trunc);
	file << "<!doctype html>\n<html><head><meta charset=\"utf-8\"><title>"
		<< EscapeHtml(title) << "</title>" << kStyle << "</head><body>\n<h1>"
		<< EscapeHtml(title) << "</h1>\n" << body << "</body></html>\n";
	return static_cast<bool>(file);
}


// An image at 4 × its size if it is small, so that single pixels show.
std::string
Figure(const std::string& caption, const std::string& file, const Image& image)
{
	const std::int32_t zoom = image.Width() <= 256 && image.Height() <= 256 ? 4 : 1;
	return "<figure><figcaption>" + EscapeHtml(caption) + "</figcaption><img src=\""
		+ EscapeHtml(file) + "\" width=\"" + std::to_string(image.Width() * zoom)
		+ "\" height=\"" + std::to_string(image.Height() * zoom) + "\"></figure>\n";
}


} // namespace


std::string
EscapeHtml(std::string_view text)
{
	std::string escaped;
	escaped.reserve(text.size());
	for (const char c : text) {
		switch (c) {
			case '&':
				escaped += "&amp;";
				break;
			case '<':
				escaped += "&lt;";
				break;
			case '>':
				escaped += "&gt;";
				break;
			case '"':
				escaped += "&quot;";
				break;
			default:
				escaped += c;
		}
	}
	return escaped;
}


std::vector<DiffLine>
DiffLines(std::string_view reference, std::string_view actual)
{
	const std::vector<std::string_view> a = SplitLines(reference);
	const std::vector<std::string_view> b = SplitLines(actual);
	std::vector<DiffLine> diff;
	constexpr std::size_t kMaxLines = 4000;
	if (a.size() > kMaxLines || b.size() > kMaxLines) {
		for (const std::string_view line : a)
			diff.push_back({'-', std::string(line)});
		for (const std::string_view line : b)
			diff.push_back({'+', std::string(line)});
		return diff;
	}

	// common[i][j]: length of the longest common subsequence of a[i..] and b[j..].
	const std::size_t width = b.size() + 1;
	std::vector<std::uint32_t> common((a.size() + 1) * width, 0);
	for (std::size_t i = a.size(); i-- > 0;) {
		for (std::size_t j = b.size(); j-- > 0;) {
			common[i * width + j] = a[i] == b[j] ? common[(i + 1) * width + j + 1] + 1
				: std::max(common[(i + 1) * width + j], common[i * width + j + 1]);
		}
	}
	std::size_t i = 0;
	std::size_t j = 0;
	while (i < a.size() || j < b.size()) {
		if (i < a.size() && j < b.size() && a[i] == b[j]) {
			diff.push_back({' ', std::string(a[i])});
			i++;
			j++;
		} else if (i < a.size()
			&& (j == b.size() || common[(i + 1) * width + j] >= common[i * width + j + 1])) {
			// Removed lines before added ones, as diff -u shows a change.
			diff.push_back({'-', std::string(a[i])});
			i++;
		} else {
			diff.push_back({'+', std::string(b[j])});
			j++;
		}
	}
	return diff;
}


std::filesystem::path
WriteImageReport(const std::filesystem::path& directory, const std::string& stem,
	const Image* reference, const Image& actual, const ImageComparison* comparison,
	std::string_view summary)
{
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
		return {};

	std::string body = "<p>" + EscapeHtml(summary) + "</p>\n<div class=\"images\">\n";
	const auto add = [&](const char* what, const Image& image) {
		const std::string file = stem + "." + what + ".png";
		if (!image.IsEmpty() && WritePng(image, directory / file).has_value())
			body += Figure(what, file, image);
	};
	if (reference != nullptr)
		add("reference", *reference);
	add("actual", actual);
	if (comparison != nullptr)
		add("difference", comparison->difference);
	body += "</div>\n";
	if (comparison != nullptr) {
		body += "<p>Difference: light gray where the pixels are equal, orange where they"
			" differ within the tolerance of one component, red beyond it.</p>\n";
	}

	const std::filesystem::path page = directory / (stem + ".html");
	return WritePage(page, stem, body) ? page : std::filesystem::path();
}


std::filesystem::path
WriteTextReport(const std::filesystem::path& directory, const std::string& stem,
	std::string_view extension, const std::string* reference, std::string_view actual)
{
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	if (error)
		return {};

	const std::string actualFile = stem + ".actual." + std::string(extension);
	{
		std::ofstream file(directory / actualFile, std::ios::binary | std::ios::trunc);
		file.write(actual.data(), static_cast<std::streamsize>(actual.size()));
	}

	std::string body = reference == nullptr ? "<p>No reference. The result:</p>\n"
		: "<p>Line diff: <span class=\"del\">− reference</span>,"
			" <span class=\"ins\">+ result</span>.</p>\n";
	body += "<p>Result as a file: <a href=\"" + EscapeHtml(actualFile) + "\">"
		+ EscapeHtml(actualFile) + "</a></p>\n<pre>";
	for (const DiffLine& line : DiffLines(reference != nullptr ? *reference : "", actual)) {
		const char* cssClass = line.kind == '-' ? "del" : line.kind == '+' ? "ins" : nullptr;
		body += cssClass != nullptr ? std::string("<span class=\"") + cssClass + "\">" : "";
		body += line.kind;
		body += ' ';
		body += EscapeHtml(line.text);
		body += cssClass != nullptr ? "</span>\n" : "\n";
	}
	body += "</pre>\n";

	const std::filesystem::path page = directory / (stem + ".html");
	return WritePage(page, stem, body) ? page : std::filesystem::path();
}


} // namespace ngui::test
