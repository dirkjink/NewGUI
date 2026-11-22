# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# Fuzz targets (CONVENTIONS.md 8.3): one fuzz/<Name>Fuzz.cpp per target. The
# file name is the convention the Fuzz workflow relies on as well, so the
# targets are found by it rather than listed twice.
file(GLOB fuzzTargetSources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/*Fuzz.cpp")

add_executable(ngui_tests
    ErrorTest.cpp
    FlagsTest.cpp
    SupportDefsTest.cpp
    VersionTest.cpp
    gfx/ColorTest.cpp
    gfx/IntRectTest.cpp
    gfx/LineTest.cpp
    gfx/PointTest.cpp
    gfx/RectTest.cpp
    gfx/RRectTest.cpp
    gfx/RegionTest.cpp
    gfx/SizeTest.cpp
    fuzz/FuzzRegressionTest.cpp
    ${fuzzTargetSources})

target_compile_definitions(ngui_tests
    PRIVATE
        NGUI_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")

target_link_libraries(ngui_tests
    PRIVATE
        ngui::ngui
        GTest::gtest
        GTest::gtest_main)

ngui_configure_target(ngui_tests)

gtest_discover_tests(ngui_tests DISCOVERY_MODE PRE_TEST)

# libFuzzer executables, one per target: ngui_fuzz_<Name>. The list goes to
# fuzz-targets.txt in the build directory.
if(NGUI_FUZZING)
    set(fuzzTargetNames)
    foreach(source IN LISTS fuzzTargetSources)
        get_filename_component(name "${source}" NAME_WE)
        string(REGEX REPLACE "Fuzz$" "" name "${name}")
        add_executable(ngui_fuzz_${name} "${source}" fuzz/LibFuzzerMain.cpp)
        target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::ngui)
        ngui_configure_target(ngui_fuzz_${name})
        target_link_options(ngui_fuzz_${name} PRIVATE -fsanitize=fuzzer)
        list(APPEND fuzzTargetNames ${name})
    endforeach()
    list(JOIN fuzzTargetNames "\n" fuzzTargetList)
    file(WRITE "${PROJECT_BINARY_DIR}/fuzz-targets.txt" "${fuzzTargetList}")
endif()
