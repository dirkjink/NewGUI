# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

# Common compiler settings for all NewGUI targets (CONVENTIONS.md, section 1).
#
#   ngui_configure_target(<target>)
#
# Sets C++23 without compiler extensions (PUBLIC for libraries, PRIVATE for
# executables), the warning level, optionally warnings as errors, and options
# for reproducible builds or, with NGUI_COVERAGE, for coverage measurement,
# and with NGUI_FUZZING the instrumentation for libFuzzer.

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
        # -Wdate-time: __DATE__ and __TIME__ would make the objects differ
        # from build to build (reproducible builds, CONVENTIONS.md 8.2).
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wdate-time)
        if(NGUI_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(NGUI_FUZZING)
            # Coverage feedback for libFuzzer in all code, not only in the
            # fuzz executables, plus the sanitizers that turn memory errors
            # and undefined behaviour into findings.
            set(sanitizers -fsanitize=fuzzer-no-link,address,undefined
                -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
            target_compile_options(${target} PRIVATE ${sanitizers})
            target_link_options(${target} PRIVATE ${sanitizers})
        endif()
        if(NGUI_COVERAGE)
            # Instrumented build for the coverage report (gcov/gcovr). It is
            # never shipped, so the prefix map below is left out: it also
            # rewrites the paths in the coverage notes, and gcovr could no
            # longer find the sources.
            target_compile_options(${target} PRIVATE --coverage)
            target_link_options(${target} PRIVATE --coverage)
        else()
            # Reproducible builds: no absolute source paths in the objects
            # (e.g. in __FILE__ or debug information).
            target_compile_options(${target} PRIVATE
                "-ffile-prefix-map=${PROJECT_SOURCE_DIR}/=")
        endif()
    endif()
endfunction()
