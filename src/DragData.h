/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Files in drags between applications (module P4): paths as the file://
// URIs of a text/uri-list and back.


#include <string>
#include <string_view>
#include <vector>


namespace ngui::detail {


// \a path as a file:// URI, its bytes percent-encoded; a Windows path
// ("C:\\x" or "C:/x") becomes file:///C:/x.
std::string FileUri(std::string_view path);

// The paths of the file:// URIs of a text/uri-list (lines ending in CR LF
// or LF), decoded; other URIs are left out.
std::vector<std::string> PathsOf(std::string_view list);


} // namespace ngui::detail
