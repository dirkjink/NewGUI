# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/templates/Version.h.in"
    "${PROJECT_BINARY_DIR}/include/ngui/Version.h"
    @ONLY)

add_library(ngui
    AbstractLayout.cpp
    AbstractLayoutItem.cpp
    Application.cpp
    Box.cpp
    Button.cpp
    CardLayout.cpp
    CheckBox.cpp
    Clock.cpp
    Control.cpp
    ControlLook.cpp
    Locker.cpp
    LookRegistry.cpp
    Looper.cpp
    LooperPort.cpp
    Menu.cpp
    MenuBar.cpp
    MenuField.cpp
    MenuFieldBar.cpp
    MenuItem.cpp
    MenuSymbols.cpp
    MenuTracker.cpp
    Message.cpp
    MessageFilter.cpp
    MessageRunner.cpp
    Messenger.cpp
    compat/CompatView.cpp
    compat/Point.cpp
    compat/Rect.cpp
    Error.cpp
    GridLayout.cpp
    GroupLayout.cpp
    LayoutViews.cpp
    Handler.cpp
    Invoker.cpp
    Layout.cpp
    LayoutContext.cpp
    LayoutItem.cpp
    LayoutUtils.cpp
    Palette.cpp
    PopUpMenu.cpp
    RadioButton.cpp
    ScrollBar.cpp
    ScrollView.cpp
    Slider.cpp
    StatusBar.cpp
    SpaceLayoutItem.cpp
    SplitLayout.cpp
    SplitView.cpp
    StringView.cpp
    TabView.cpp
    TextLayout.cpp
    TextSystem.cpp
    TextView.cpp
    TruncateString.cpp
    TwoDimensionalLayout.cpp
    View.cpp
    ViewLayoutItem.cpp
    Window.cpp
    Version.cpp
    layout/CollapsingLayouter.cpp
    layout/ComplexLayouter.cpp
    layout/LayoutOptimizer.cpp
    layout/Layouter.cpp
    layout/OneElementLayouter.cpp
    layout/SimpleLayouter.cpp
    gfx/Backend.cpp
    gfx/BackendRegistry.cpp
    gfx/Canvas.cpp
    gfx/DisplayList.cpp
    gfx/DisplayListBuilder.cpp
    gfx/Dump.cpp
    gfx/DumpBackend.cpp
    gfx/ImageRegistry.cpp
    gfx/LinearGradient.cpp
    gfx/RRect.cpp
    gfx/Region.cpp
    gfx/RegionSupport.cpp
    looks/be/BeControlLook.cpp
    platform/CpuPresenter.cpp
    looks/be/BeLook.cpp)
add_library(ngui::ngui ALIAS ngui)

target_include_directories(ngui
    PUBLIC
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>"
    PRIVATE
        # Internal headers: the compatibility layer and the ported looks.
        "${CMAKE_CURRENT_SOURCE_DIR}")

ngui_configure_target(ngui)

# The Skia backend (specification 13), its own library: Skia headers appear
# in its sources only (13.5).
if(NGUI_WITH_SKIA)
    find_package(unofficial-skia CONFIG REQUIRED)
    add_library(ngui_skia
        gfx/skia/SkiaBackend.cpp)
    add_library(ngui::skia ALIAS ngui_skia)
    target_link_libraries(ngui_skia
        PUBLIC
            ngui::ngui
        PRIVATE
            unofficial::skia::skia)
    ngui_configure_target(ngui_skia)
endif()

# The text subsystem (modules X1, X2, X4, X5), its own library: HarfBuzz,
# libunibreak, and the bundled fonts embedded as generated sources
# (data/fonts/).
if(NGUI_WITH_TEXT)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(libunibreak CONFIG REQUIRED)
    set(embeddedFonts)
    foreach(font IN ITEMS
            "NotoSans-Regular|kNotoSansRegular"
            "NotoSans-Bold|kNotoSansBold"
            "NotoSansMono-Regular|kNotoSansMonoRegular")
        string(REPLACE "|" ";" parts "${font}")
        list(GET parts 0 file)
        list(GET parts 1 symbol)
        set(input "${PROJECT_SOURCE_DIR}/data/fonts/${file}.ttf")
        set(output "${CMAKE_CURRENT_BINARY_DIR}/fonts/${file}.cpp")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}"
                "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/NguiEmbedFile.cmake"
            DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/NguiEmbedFile.cmake"
            COMMENT "Embedding ${file}.ttf"
            VERBATIM)
        list(APPEND embeddedFonts "${output}")
    endforeach()
    add_library(ngui_text
        text/FontTextSystem.cpp
        ${embeddedFonts})
    add_library(ngui::text ALIAS ngui_text)
    target_link_libraries(ngui_text
        PUBLIC
            ngui::ngui
        PRIVATE
            harfbuzz::harfbuzz
            libunibreak::libunibreak)
    target_include_directories(ngui_text PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ngui_configure_target(ngui_text)
endif()

# The SDL3 platform (module P2), its own library: SDL headers appear in its
# sources only.
if(NGUI_WITH_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
    add_library(ngui_sdl3
        platform/sdl3/SdlEvents.cpp
        platform/sdl3/SdlPlatform.cpp)
    add_library(ngui::sdl3 ALIAS ngui_sdl3)
    target_link_libraries(ngui_sdl3
        PUBLIC
            ngui::ngui
        PRIVATE
            SDL3::SDL3)
    ngui_configure_target(ngui_sdl3)
endif()

# The test kit (specification 20.5, 21; module M14): offscreen rendering,
# image comparison, snapshots and their reports, for the tests of NewGUI and
# later of applications. Built with the tests, whose vcpkg feature brings
# libpng and GoogleTest.
if(NGUI_BUILD_TESTS)
    find_package(PNG REQUIRED)
    add_library(ngui_testkit
        test/Image.cpp
        test/ImageCompare.cpp
        test/Render.cpp
        test/Report.cpp
        test/Snapshot.cpp)
    add_library(ngui::testkit ALIAS ngui_testkit)
    target_link_libraries(ngui_testkit
        PUBLIC
            ngui::ngui
        PRIVATE
            PNG::PNG)
    ngui_configure_target(ngui_testkit)

    # GoogleTest's main plus --update-snapshots.
    find_package(GTest CONFIG REQUIRED)
    add_library(ngui_testkit_main
        test/GTestMain.cpp)
    add_library(ngui::testkit_main ALIAS ngui_testkit_main)
    target_link_libraries(ngui_testkit_main
        PUBLIC
            ngui::testkit
            GTest::gtest)
    ngui_configure_target(ngui_testkit_main)
endif()
