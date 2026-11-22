# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.25)

project(NewGUI
    VERSION 0.1.0
    DESCRIPTION "Cross-platform C++ UI toolkit inspired by the Haiku Interface Kit"
    HOMEPAGE_URL "https://github.com/dirkjink/NewGUI"
    LANGUAGES CXX)

option(NGUI_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(NGUI_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(NGUI_COVERAGE "Instrument for coverage measurement (GCC and Clang)" OFF)
option(NGUI_BUILD_API_DOCS "Add the target ngui_api_docs (requires Doxygen)" OFF)
option(NGUI_FUZZING "Build the libFuzzer executables with sanitizers (Clang)" OFF)

if(NGUI_COVERAGE AND MSVC)
    message(FATAL_ERROR "NGUI_COVERAGE requires GCC or Clang")
endif()
if(NGUI_FUZZING AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "NGUI_FUZZING requires Clang (libFuzzer)")
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(NguiCompilerOptions)

add_subdirectory(src)

if(NGUI_BUILD_API_DOCS)
    include(NguiApiDocs)
endif()

if(NGUI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
