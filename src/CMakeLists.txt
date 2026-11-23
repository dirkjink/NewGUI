# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/templates/Version.h.in"
    "${PROJECT_BINARY_DIR}/include/ngui/Version.h"
    @ONLY)

add_library(ngui
    Application.cpp
    Clock.cpp
    ControlLook.cpp
    Locker.cpp
    LookRegistry.cpp
    Looper.cpp
    LooperPort.cpp
    Message.cpp
    MessageFilter.cpp
    MessageRunner.cpp
    Messenger.cpp
    compat/CompatView.cpp
    compat/Point.cpp
    compat/Rect.cpp
    Error.cpp
    Handler.cpp
    Invoker.cpp
    Palette.cpp
    TextSystem.cpp
    Version.cpp
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
