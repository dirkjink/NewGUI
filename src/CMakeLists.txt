# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/templates/Version.h.in"
    "${PROJECT_BINARY_DIR}/include/ngui/Version.h"
    @ONLY)

add_library(ngui
    Version.cpp)
add_library(ngui::ngui ALIAS ngui)

target_include_directories(ngui
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>")

ngui_configure_target(ngui)
