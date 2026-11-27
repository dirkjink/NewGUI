/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "DragData.h"


namespace ngui {


namespace {


// The characters a URI keeps as they are (RFC 3986, unreserved, and "/").
bool
IsUriCharacter(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
		|| c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
}


} // namespace


std::string
detail::FileUri(std::string_view path)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	std::string uri = "file://";
	// Windows paths ("C:\\x") become file:///C:/x
	if (path.size() >= 2 && path[1] == ':')
		uri += '/';
	for (const char raw : path) {
		const auto c = static_cast<unsigned char>(raw == '\\' ? '/' : raw);
		if (IsUriCharacter(c) || (c == ':' && uri.size() == 9)) {
			uri += static_cast<char>(c);
			continue;
		}
		uri += '%';
		uri += kHex[c >> 4];
		uri += kHex[c & 15];
	}
	return uri;
}


// The paths of the file:// URIs of a text/uri-list, decoded.
std::vector<std::string>
detail::PathsOf(std::string_view list)
{
	const auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};
	std::vector<std::string> paths;
	while (!list.empty()) {
		const std::size_t end = list.find('\n');
		std::string_view line = list.substr(0, end);
		list = end == std::string_view::npos ? std::string_view() : list.substr(end + 1);
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		if (!line.starts_with("file://"))
			continue;
		line.remove_prefix(7);
		// "file://host/path": the path from the first slash
		if (const std::size_t slash = line.find('/'); slash != std::string_view::npos)
			line.remove_prefix(slash);
		// "/C:/x" is a Windows path
		if (line.size() >= 3 && line[0] == '/' && line[2] == ':')
			line.remove_prefix(1);
		std::string path;
		for (std::size_t i = 0; i < line.size(); i++) {
			if (line[i] == '%' && i + 2 < line.size() && hex(line[i + 1]) >= 0
				&& hex(line[i + 2]) >= 0) {
				path += static_cast<char>(hex(line[i + 1]) * 16 + hex(line[i + 2]));
				i += 2;
			} else
				path += line[i];
		}
		paths.push_back(std::move(path));
	}
	return paths;
}


} // namespace ngui
