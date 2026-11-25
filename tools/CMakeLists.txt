# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Command-line tools for working with NewGUI applications.

# ngui-hey: scripting from the command line (specification 20.7), as
# Haiku's hey; the parser lives in the library (src/remote/Hey.cpp).
add_executable(ngui_hey hey/main.cpp)
set_target_properties(ngui_hey PROPERTIES OUTPUT_NAME ngui-hey)
target_link_libraries(ngui_hey PRIVATE ngui::ngui)
target_include_directories(ngui_hey PRIVATE "${PROJECT_SOURCE_DIR}/src")
ngui_configure_target(ngui_hey)
