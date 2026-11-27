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
if(NOT NGUI_WITH_SKIA)
    list(FILTER fuzzTargetSources EXCLUDE REGEX "/PdfExportFuzz\\.cpp$")
endif()

add_executable(ngui_tests
    AccessibilityTest.cpp
    AlertTest.cpp
    ApplicationTest.cpp
    BitmapTest.cpp
    CatalogTest.cpp
    ClipboardTest.cpp
    ClockTest.cpp
    ColorSchemeTest.cpp
    ColumnListViewTest.cpp
    ColumnTypesTest.cpp
    ContainerTest.cpp
    ControlTest.cpp
    CursorTest.cpp
    DragTest.cpp
    DropTest.cpp
    ErrorTest.cpp
    FontTest.cpp
    FormatsTest.cpp
    FilePanelTest.cpp
    FlagsTest.cpp
    InvokerTest.cpp
    LockerTest.cpp
    LookPluginTest.cpp
    ListViewTest.cpp
    LookRegistryTest.cpp
    LooperTest.cpp
    MenuScrollingTest.cpp
    MenuTest.cpp
    MessageFilterTest.cpp
    MessageRunnerTest.cpp
    MessageTest.cpp
    HeyTest.cpp
    RecordingTest.cpp
    RemoteControlTest.cpp
    ScriptingPropertiesTest.cpp
    ScriptingTest.cpp
    MessengerTest.cpp
    NotificationTest.cpp
    WindowFramesTest.cpp
    OutlineListViewTest.cpp
    PaletteTest.cpp
    PrintJobTest.cpp
    ShapeTest.cpp
    SupportDefsTest.cpp
    SystemMenuBarTest.cpp
    TextControlTest.cpp
    TextLayoutTest.cpp
    TextStyleTest.cpp
    TextViewTest.cpp
    ToolTipTest.cpp
    TruncateStringTest.cpp
    VersionTest.cpp
    ViewTest.cpp
    LookSwitchTest.cpp
    WindowSnapshotTest.cpp
    WindowTest.cpp
    compat/CompatViewTest.cpp
    gfx/CanvasTest.cpp
    gfx/ColorTest.cpp
    gfx/DisplayListTest.cpp
    gfx/DumpBackendTest.cpp
    gfx/DumpTest.cpp
    gfx/SvgExportTest.cpp
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
    look/haiku/HaikuLookTest.cpp
    look/flat/FlatLookTest.cpp
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
# The end-to-end tests of ngui-hey and ngui-replay run the built tools.
if(TARGET ngui_hey)
    add_dependencies(ngui_tests ngui_hey)
    target_compile_definitions(ngui_tests PRIVATE NGUI_TEST_HEY="$<TARGET_FILE:ngui_hey>")
endif()
if(TARGET ngui_replay)
    add_dependencies(ngui_tests ngui_replay)
    target_compile_definitions(ngui_tests PRIVATE NGUI_TEST_REPLAY="$<TARGET_FILE:ngui_replay>")
endif()
target_include_directories(ngui_tests
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}")

# The look plugins of LookPluginTest (plugin/TestLookPlugin.cpp, one per
# variant), in a directory of their own with a file that is no plugin; the
# generator expression keeps multi-config generators from adding a
# subdirectory per configuration.
set(lookPluginDir "${CMAKE_CURRENT_BINARY_DIR}/lookplugins")
file(MAKE_DIRECTORY "${lookPluginDir}")
file(WRITE "${lookPluginDir}/readme.txt" "Not a look plugin.\n")
foreach(variant IN ITEMS "0;test_look_plugin" "1;test_look_plugin_old"
        "2;test_look_plugin_other_build" "3;test_look_plugin_none" "4;test_look_plugin_twin")
    list(GET variant 0 number)
    list(GET variant 1 name)
    add_library(${name} MODULE plugin/TestLookPlugin.cpp)
    ngui_configure_target(${name})
    target_compile_definitions(${name} PRIVATE NGUI_TEST_PLUGIN_VARIANT=${number})
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(${name} PRIVATE ngui::ngui)
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${lookPluginDir}$<0:>")
    add_dependencies(ngui_tests ${name})
endforeach()
target_compile_definitions(ngui_tests
    PRIVATE
        NGUI_TEST_LOOK_PLUGIN_DIR="${lookPluginDir}"
        NGUI_TEST_LOOK_PLUGIN_SUFFIX="${CMAKE_SHARED_MODULE_SUFFIX}")

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
# The fuzz target of the PDF export draws with Skia.
if(NGUI_WITH_SKIA)
    target_link_libraries(ngui_tests PRIVATE ngui::skia)
endif()

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
        if(NGUI_WITH_SKIA)
            target_link_libraries(ngui_fuzz_${name} PRIVATE ngui::skia)
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
        skia/FlatLookSkiaTest.cpp
        skia/HaikuLookSkiaTest.cpp
        skia/SkiaBackendTest.cpp
        skia/SkiaSmokeTest.cpp)
    # The GPU surface (phase G) over an OpenGL context of EGL, where there is
    # EGL (Mesa's llvmpipe in the CI; the test skips without a context).
    # FindOpenGL makes OpenGL::EGL only with glvnd's libOpenGL, which the
    # test does not need: EGL alone loads the functions of OpenGL.
    find_path(NGUI_EGL_INCLUDE_DIR EGL/egl.h)
    find_library(NGUI_EGL_LIBRARY EGL)
    if(NGUI_EGL_INCLUDE_DIR AND NGUI_EGL_LIBRARY AND NGUI_WITH_TEXT)
        target_sources(ngui_skia_tests PRIVATE skia/SkiaGpuTest.cpp)
        target_include_directories(ngui_skia_tests SYSTEM PRIVATE "${NGUI_EGL_INCLUDE_DIR}")
        target_link_libraries(ngui_skia_tests PRIVATE "${NGUI_EGL_LIBRARY}")
    endif()
    # What a PDF shows and says, read by poppler where it is installed.
    find_program(NGUI_PDFTOPPM pdftoppm)
    find_program(NGUI_PDFTOTEXT pdftotext)
    if(NGUI_PDFTOPPM)
        target_compile_definitions(ngui_skia_tests PRIVATE NGUI_TEST_PDFTOPPM="${NGUI_PDFTOPPM}")
    endif()
    if(NGUI_PDFTOTEXT)
        target_compile_definitions(ngui_skia_tests
            PRIVATE NGUI_TEST_PDFTOTEXT="${NGUI_PDFTOTEXT}")
    endif()
    target_link_libraries(ngui_skia_tests
        PRIVATE
            ngui::skia
            ngui::testkit
            ngui::testkit_main
            # Skia's headers for the smoke test; the library comes once,
            # through ngui::skia (Apple's ld warns about an archive twice).
            $<COMPILE_ONLY:unofficial::skia::skia>)
    # Text drawn with the bundled fonts (vcpkg feature text).
    if(NGUI_WITH_TEXT)
        target_sources(ngui_skia_tests PRIVATE skia/PdfDocumentTest.cpp skia/SkiaPrintTest.cpp
            skia/TextSkiaTest.cpp skia/SnapshotSkiaTest.cpp skia/WindowFrameSkiaTest.cpp)
        # A stand-in for libcups, loaded by the print service in its place.
        if(NOT WIN32)
            add_library(ngui_fake_cups MODULE print/FakeCups.cpp)
            ngui_configure_target(ngui_fake_cups)
            add_dependencies(ngui_skia_tests ngui_fake_cups)
            target_compile_definitions(ngui_skia_tests
                PRIVATE NGUI_TEST_FAKE_CUPS="$<TARGET_FILE:ngui_fake_cups>")
        endif()
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

# Blend2D (vcpkg feature blend2d): the backend's own tests, and with Skia the
# comparison of both (conformance level C1x).
if(NGUI_WITH_BLEND2D)
    add_executable(ngui_blend2d_tests
        blend2d/Blend2DBackendTest.cpp)
    target_link_libraries(ngui_blend2d_tests
        PRIVATE
            ngui::blend2d
            ngui::testkit
            ngui::testkit_main)
    if(NGUI_WITH_SKIA AND NGUI_WITH_TEXT)
        target_sources(ngui_blend2d_tests PRIVATE blend2d/CrossBackendTest.cpp)
        target_link_libraries(ngui_blend2d_tests PRIVATE ngui::skia ngui::text)
    endif()
    target_include_directories(ngui_blend2d_tests
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src")
    ngui_configure_target(ngui_blend2d_tests)
    gtest_discover_tests(ngui_blend2d_tests
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
            # SDL's headers; the library comes once, through ngui::sdl3.
            $<COMPILE_ONLY:SDL3::SDL3>)
    target_include_directories(ngui_sdl3_tests
        PRIVATE
            "${PROJECT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_SOURCE_DIR}")
    ngui_configure_target(ngui_sdl3_tests)
    # GPU windows (phase G) render with Skia
    if(NGUI_WITH_SKIA)
        target_sources(ngui_sdl3_tests PRIVATE sdl3/SdlGpuTest.cpp)
        target_link_libraries(ngui_sdl3_tests PRIVATE ngui::skia ngui::testkit)
    endif()
    # the bridge to AccessKit (vcpkg feature accessibility)
    if(NGUI_WITH_ACCESSKIT)
        find_package(unofficial-accesskit-c CONFIG REQUIRED)
        target_sources(ngui_sdl3_tests PRIVATE sdl3/AccessKitBridgeTest.cpp)
        target_link_libraries(ngui_sdl3_tests
            PRIVATE $<COMPILE_ONLY:unofficial::accesskit-c::accesskit>)
        # NSAccessibility through SDL's Cocoa windows, asked as VoiceOver asks
        # (Objective-C++, with ARC).
        if(APPLE)
            target_sources(ngui_sdl3_tests PRIVATE sdl3/SdlCocoaAccessibilityTest.mm)
            set_source_files_properties(sdl3/SdlCocoaAccessibilityTest.mm
                PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
        endif()
        target_compile_definitions(ngui_sdl3_tests PRIVATE NGUI_WITH_ACCESSKIT=1)
    endif()
    # Drag & drop with other programs on Windows (module P4), OLE's objects,
    # and the notifications there (module P5), the icon in the notification
    # area.
    if(WIN32)
        target_sources(ngui_sdl3_tests PRIVATE sdl3/OleDragDropTest.cpp
            sdl3/WinNotificationsTest.cpp)
        target_link_libraries(ngui_sdl3_tests PRIVATE ole32 shell32)
    endif()
    # ... on X11 (XDND): the test is the other program, with Xlib of its own
    # and XTest for the mouse; it needs an X server (Xvfb in the CI) and is
    # skipped without one.
    if(UNIX AND NOT APPLE)
        find_package(X11)
        if(X11_FOUND AND X11_Xtst_FOUND AND NGUI_XLIB_INCLUDE_DIR)
            target_sources(ngui_sdl3_tests PRIVATE sdl3/SdlX11DragDropTest.cpp
                sdl3/SdlX11FramesTest.cpp sdl3/SdlX11WindowTest.cpp)
            target_link_libraries(ngui_sdl3_tests PRIVATE X11::X11 X11::Xtst)
        endif()
    endif()
    # The desktop's notifications on Linux (module P5): the test is the
    # notification server, with libdbus of its own; it needs a session bus
    # (dbus-run-session in the CI) and is skipped without one.
    if(UNIX AND NOT APPLE)
        find_package(DBus1 CONFIG QUIET)
        if(TARGET dbus-1)
            target_sources(ngui_sdl3_tests PRIVATE sdl3/DbusNotificationsTest.cpp)
            target_link_libraries(ngui_sdl3_tests PRIVATE dbus-1)
        endif()
    endif()
    # The application's menu bar (module P3), drag & drop (module P4) and
    # the notifications (module P5) on macOS, with SDL's Cocoa windows.
    if(APPLE)
        target_sources(ngui_sdl3_tests PRIVATE sdl3/SdlCocoaDragDropTest.mm
            sdl3/SdlCocoaMenuBarTest.mm sdl3/SdlCocoaNotificationsTest.mm)
        set_source_files_properties(sdl3/SdlCocoaDragDropTest.mm sdl3/SdlCocoaMenuBarTest.mm
            sdl3/SdlCocoaNotificationsTest.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    endif()
    gtest_discover_tests(ngui_sdl3_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=offscreen" TIMEOUT ${testTimeout})
endif()
