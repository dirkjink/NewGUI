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
# Targets of optional libraries are left out when those are not built.
if(NOT NGUI_WITH_TEXT)
    list(FILTER fuzzTargetSources EXCLUDE REGEX "/(Text|TextLayout)Fuzz\\.cpp$")
endif()
if(NOT NGUI_WITH_SVG)
    list(FILTER fuzzTargetSources EXCLUDE REGEX "/VectorIconFuzz\\.cpp$")
endif()

add_executable(ngui_tests
    AlertTest.cpp
    ApplicationTest.cpp
    BitmapTest.cpp
    CatalogTest.cpp
    ClipboardTest.cpp
    ClockTest.cpp
    ContainerTest.cpp
    ControlTest.cpp
    CursorTest.cpp
    DropTest.cpp
    ErrorTest.cpp
    FontTest.cpp
    FormatsTest.cpp
    FilePanelTest.cpp
    FlagsTest.cpp
    InvokerTest.cpp
    LockerTest.cpp
    ListViewTest.cpp
    LookRegistryTest.cpp
    LooperTest.cpp
    MenuScrollingTest.cpp
    MenuTest.cpp
    MessageFilterTest.cpp
    MessageRunnerTest.cpp
    MessageTest.cpp
    MessengerTest.cpp
    OutlineListViewTest.cpp
    PaletteTest.cpp
    ShapeTest.cpp
    SupportDefsTest.cpp
    TextControlTest.cpp
    TextLayoutTest.cpp
    TextStyleTest.cpp
    TextViewTest.cpp
    ToolTipTest.cpp
    TruncateStringTest.cpp
    VersionTest.cpp
    ViewTest.cpp
    WindowSnapshotTest.cpp
    WindowTest.cpp
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
    gfx/PathTest.cpp
    gfx/PointTest.cpp
    gfx/RectTest.cpp
    gfx/RRectTest.cpp
    gfx/RegionTest.cpp
    gfx/SizeTest.cpp
    layout/GroupGridTest.cpp
    layout/LayoutBuilderTest.cpp
    layout/LayoutTest.cpp
    layout/LayouterTest.cpp
    look/ControlLookTest.cpp
    look/be/BeLookTest.cpp
    platform/CpuPresenterTest.cpp
    platform/HeadlessPlatformTest.cpp
    platform/PlatformTest.cpp
    test/DriverTest.cpp
    test/ImageCompareTest.cpp
    test/ImageTest.cpp
    test/LintTest.cpp
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
        NGUI_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus"
        NGUI_TEST_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/fonts"
        NGUI_BUNDLED_FONTS_DIR="${PROJECT_SOURCE_DIR}/data/fonts"
        NGUI_DEMO_CATALOG="${PROJECT_SOURCE_DIR}/examples/button/locale/de/LC_MESSAGES/newgui-demo.mo")

target_link_libraries(ngui_tests
    PRIVATE
        ngui::ngui
        ngui::testkit
        ngui::testkit_main)

# The text subsystem with its bundled fonts (vcpkg feature text).
if(NGUI_WITH_TEXT)
    target_sources(ngui_tests PRIVATE text/FontTextSystemTest.cpp text/FontFallbackTest.cpp
        text/FontconfigSourceTest.cpp text/DirectWriteSourceTest.cpp text/BidiTest.cpp)
    target_link_libraries(ngui_tests PRIVATE ngui::text)
endif()

# The vector icon renderer (vcpkg feature svg).
if(NGUI_WITH_SVG)
    target_sources(ngui_tests PRIVATE AlertIconTest.cpp IconUtilsTest.cpp)
    target_link_libraries(ngui_tests PRIVATE ngui::svg)
endif()

ngui_configure_target(ngui_tests)

# Snapshots (specification 20.5): the references are checked in under
# tests/snapshots/, failed comparisons leave their HTML reports in the build
# directory (a CI artifact). As arguments to the tests rather than in the
# environment: gtest_discover_tests keeps only the first variable of a list.
# NGUI_UPDATE_SNAPSHOTS=1 passes through.
set(snapshotArguments
    "--snapshot-dir=${CMAKE_CURRENT_SOURCE_DIR}/snapshots"
    "--snapshot-report-dir=${PROJECT_BINARY_DIR}/snapshot-report")

# A test that hangs fails after five minutes instead of holding the CI job
# until its time limit.
set(testTimeout 300)
# Under AddressSanitizer: the global state of the system's fontconfig is no
# leak (lsan.supp); without a sanitizer the variable does nothing.
set(sanitizerEnvironment "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")
gtest_discover_tests(ngui_tests
    DISCOVERY_MODE PRE_TEST
    EXTRA_ARGS ${snapshotArguments}
    PROPERTIES TIMEOUT ${testTimeout} ENVIRONMENT "${sanitizerEnvironment}")

# libFuzzer executables, one per target: ngui_fuzz_<Name>. The list goes to
# fuzz-targets.txt in the build directory.
if(NGUI_FUZZING)
    set(fuzzTargetNames)
    foreach(source IN LISTS fuzzTargetSources)
        get_filename_component(name "${source}" NAME_WE)
        string(REGEX REPLACE "Fuzz$" "" name "${name}")
        add_executable(ngui_fuzz_${name} "${source}" fuzz/LibFuzzerMain.cpp)
        target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::ngui ngui::testkit)
        # Targets of internal classes (the layouters) include from src/.
        target_include_directories(ngui_fuzz_${name} PRIVATE "${PROJECT_SOURCE_DIR}/src")
        if(NGUI_WITH_TEXT)
            target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::text)
        endif()
        if(NGUI_WITH_SVG)
            target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::svg)
        endif()
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
        skia/CompatSkiaTest.cpp
        skia/SkiaBackendTest.cpp
        skia/SkiaSmokeTest.cpp)
    target_link_libraries(ngui_skia_tests
        PRIVATE
            ngui::skia
            ngui::testkit
            ngui::testkit_main
            unofficial::skia::skia)
    # Text drawn with the bundled fonts (vcpkg feature text).
    if(NGUI_WITH_TEXT)
        target_sources(ngui_skia_tests PRIVATE skia/TextSkiaTest.cpp skia/SnapshotSkiaTest.cpp)
        target_link_libraries(ngui_skia_tests PRIVATE ngui::text)
    endif()
    # The alert's type icon (vcpkg feature svg).
    if(NGUI_WITH_SVG)
        target_link_libraries(ngui_skia_tests PRIVATE ngui::svg)
        target_compile_definitions(ngui_skia_tests PRIVATE NGUI_TEST_WITH_SVG=1)
    endif()
    target_include_directories(ngui_skia_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
    target_compile_definitions(ngui_skia_tests
        PRIVATE NGUI_TEST_FONTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/fonts")
    ngui_configure_target(ngui_skia_tests)
    gtest_discover_tests(ngui_skia_tests
        DISCOVERY_MODE PRE_TEST
        EXTRA_ARGS ${snapshotArguments}
        PROPERTIES TIMEOUT ${testTimeout})
endif()

# SDL3 (vcpkg feature sdl3): the platform, with SDL's offscreen video driver,
# so that it runs without a display.
if(NGUI_WITH_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
    add_executable(ngui_sdl3_tests
        sdl3/SdlEventsTest.cpp
        sdl3/SdlPlatformTest.cpp)
    target_link_libraries(ngui_sdl3_tests
        PRIVATE
            ngui::sdl3
            ngui::testkit_main
            SDL3::SDL3)
    target_include_directories(ngui_sdl3_tests
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    ngui_configure_target(ngui_sdl3_tests)
    gtest_discover_tests(ngui_sdl3_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen" TIMEOUT ${testTimeout})
endif()
