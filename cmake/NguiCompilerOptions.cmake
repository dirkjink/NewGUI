# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Common compiler settings for all NewGUI targets (CONVENTIONS.md, section 1).
#
#   ngui_configure_target(<target>)
#
# Sets C++23 without compiler extensions (PUBLIC for libraries, PRIVATE for
# executables), the warning level, optionally warnings as errors, and options
# for reproducible builds.

function(ngui_configure_target target)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "EXECUTABLE")
        set(standardScope PRIVATE)
    else()
        # The public headers use C++23, so consumers need it as well.
        set(standardScope PUBLIC)
    endif()

    target_compile_features(${target} ${standardScope} cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /permissive- /utf-8 /Zc:__cplusplus)
        if(NGUI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic)
        if(NGUI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        # Reproducible builds: no absolute source paths in the objects
        # (e.g. in __FILE__ or debug information).
        target_compile_options(${target} PRIVATE
            "-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")
    endif()
endfunction()
