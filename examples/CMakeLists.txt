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
        ngui::svg
        ngui::text)
ngui_configure_target(ngui_example_button)
# Its translations beside it, where BLocaleRoster::LoadCatalog() looks
# (button/po/*.po, compiled with msgfmt into button/locale).
add_custom_command(TARGET ngui_example_button POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/button/locale"
        "$<TARGET_FILE_DIR:ngui_example_button>/locale"
    VERBATIM)
