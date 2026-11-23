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
    LockerTest.cpp
    LookRegistryTest.cpp
    MessageTest.cpp
    PaletteTest.cpp
    SupportDefsTest.cpp
    VersionTest.cpp
    compat/CompatViewTest.cpp
    gfx/CanvasTest.cpp
    gfx/ColorTest.cpp
    gfx/DisplayListTest.cpp
    gfx/DumpBackendTest.cpp
    gfx/DumpTest.cpp
    gfx/ImageRegistryTest.cpp
    gfx/IntRectTest.cpp
    gfx/LinearGradientTest.cpp
    gfx/LineTest.cpp
    gfx/PaintTest.cpp
    gfx/PointTest.cpp
    gfx/RectTest.cpp
    gfx/RRectTest.cpp
    gfx/RegionTest.cpp
    gfx/SizeTest.cpp
    look/ControlLookTest.cpp
    look/be/BeLookTest.cpp
    test/ImageCompareTest.cpp
    test/ImageTest.cpp
    test/SnapshotTest.cpp
    fuzz/FuzzRegressionTest.cpp
    ${fuzzTargetSources})

# The tests of internal classes (DisplayListBuilder ...) include their
# private headers from src/; shared test helpers come from tests/support/.
target_include_directories(ngui_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(ngui_tests
    PRIVATE
        NGUI_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")

target_link_libraries(ngui_tests
    PRIVATE
        ngui::ngui
        ngui::testkit
        ngui::testkit_main)

ngui_configure_target(ngui_tests)

# Snapshots (specification 20.5): the references are checked in under
# tests/snapshots/, failed comparisons leave their HTML reports in the build
# directory (a CI artifact). As arguments to the tests rather than in the
# environment: gtest_discover_tests keeps only the first variable of a list.
# NGUI_UPDATE_SNAPSHOTS=1 passes through.
set(snapshotArguments
    "--snapshot-dir=${CMAKE_CURRENT_SOURCE_DIR}/snapshots"
    "--snapshot-report-dir=${PROJECT_BINARY_DIR}/snapshot-report")

gtest_discover_tests(ngui_tests
    DISCOVERY_MODE PRE_TEST
    EXTRA_ARGS ${snapshotArguments})

# libFuzzer executables, one per target: ngui_fuzz_<Name>. The list goes to
# fuzz-targets.txt in the build directory.
if(NGUI_FUZZING)
    set(fuzzTargetNames)
    foreach(source IN LISTS fuzzTargetSources)
        get_filename_component(name "${source}" NAME_WE)
        string(REGEX REPLACE "Fuzz$" "" name "${name}")
        add_executable(ngui_fuzz_${name} "${source}" fuzz/LibFuzzerMain.cpp)
        target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::ngui ngui::testkit)
        ngui_configure_target(ngui_fuzz_${name})
        target_link_options(ngui_fuzz_${name} PRIVATE -fsanitize=fuzzer)
        list(APPEND fuzzTargetNames ${name})
    endforeach()
    list(JOIN fuzzTargetNames "\n" fuzzTargetList)
    file(WRITE "${PROJECT_BINARY_DIR}/fuzz-targets.txt" "${fuzzTargetList}")
endif()

# Skia (vcpkg feature skia): the Skia backend and a smoke test of the port.
# Their own executable, so that the main test run does not depend on Skia.
if(NGUI_WITH_SKIA)
    # Imported targets are visible in the directory of their find_package
    # only; the smoke test uses Skia directly.
    find_package(unofficial-skia CONFIG REQUIRED)
    add_executable(ngui_skia_tests
        skia/BeLookSkiaTest.cpp
        skia/SkiaBackendTest.cpp
        skia/SkiaSmokeTest.cpp)
    target_link_libraries(ngui_skia_tests
        PRIVATE
            ngui::skia
            ngui::testkit
            ngui::testkit_main
            unofficial::skia::skia)
    target_include_directories(ngui_skia_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ngui_configure_target(ngui_skia_tests)
    gtest_discover_tests(ngui_skia_tests
        DISCOVERY_MODE PRE_TEST
        EXTRA_ARGS ${snapshotArguments})
endif()
