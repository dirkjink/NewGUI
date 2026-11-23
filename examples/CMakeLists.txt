# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Small programs that open real windows; built, but not run, in CI.

add_executable(ngui_example_button
    button/main.cpp)
target_link_libraries(ngui_example_button
    PRIVATE
        ngui::sdl3
        ngui::skia
        ngui::text)
ngui_configure_target(ngui_example_button)
