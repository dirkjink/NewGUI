# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/templates/Version.h.in"
    "${PROJECT_BINARY_DIR}/include/ngui/Version.h"
    @ONLY)

# NewGUI's system icons (data/icons/), embedded as generated sources; drawn
# by the vector icon renderer (ngui_svg).
set(embeddedIcons)
foreach(icon IN ITEMS
        "dialog-information|kDialogInformation"
        "dialog-idea|kDialogIdea"
        "dialog-warning|kDialogWarning"
        "dialog-error|kDialogError")
    string(REPLACE "|" ";" parts "${icon}")
    list(GET parts 0 file)
    list(GET parts 1 symbol)
    set(input "${PROJECT_SOURCE_DIR}/data/icons/${file}.svg")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/icons/${file}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" "-DINPUT=${input}" "-DOUTPUT=${output}"
            "-DSYMBOL=${symbol}" -P "${PROJECT_SOURCE_DIR}/cmake/NguiEmbedFile.cmake"
        DEPENDS "${input}" "${PROJECT_SOURCE_DIR}/cmake/NguiEmbedFile.cmake"
        COMMENT "Embedding ${file}.svg"
        VERBATIM)
    list(APPEND embeddedIcons "${output}")
endforeach()

add_library(ngui
    ${embeddedIcons}
    AbstractLayout.cpp
    AbstractLayoutItem.cpp
    Alert.cpp
    Bitmap.cpp
    Application.cpp
    Box.cpp
    Button.cpp
    CardLayout.cpp
    CheckBox.cpp
    ColorConversion.cpp
    Clipboard.cpp
    Clock.cpp
    Control.cpp
    Cursor.cpp
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
    PropertyInfo.cpp
    remote/Hey.cpp
    remote/LocalSocket.cpp
    remote/RemoteControl.cpp
    OutlineListView.cpp
    compat/CompatView.cpp
    compat/Point.cpp
    compat/Rect.cpp
    Environment.cpp
    Error.cpp
    FilePanel.cpp
    Font.cpp
    GridLayout.cpp
    GroupLayout.cpp
    IconUtils.cpp
    LayoutViews.cpp
    Handler.cpp
    Invoker.cpp
    Layout.cpp
    LayoutContext.cpp
    LayoutItem.cpp
    LayoutUtils.cpp
    ListItem.cpp
    ListView.cpp
    locale/Catalog.cpp
    locale/Formats.cpp
    locale/Languages.cpp
    locale/LocaleRoster.cpp
    locale/NumberGrouping.cpp
    locale/PluralForms.cpp
    Palette.cpp
    Polygon.cpp
    PopUpMenu.cpp
    RadioButton.cpp
    ScrollBar.cpp
    Shape.cpp
    ScrollView.cpp
    Slider.cpp
    StatusBar.cpp
    SpaceLayoutItem.cpp
    SplitLayout.cpp
    SplitView.cpp
    StringItem.cpp
    StringView.cpp
    TabView.cpp
    TextControl.cpp
    TextInput.cpp
    TextLayout.cpp
    TextStyleBuffer.cpp
    TextSystem.cpp
    TextView.cpp
    ToolTip.cpp
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
    gfx/Path.cpp
    gfx/PngWriter.cpp
    gfx/RRect.cpp
    gfx/Region.cpp
    gfx/RegionSupport.cpp
    gfx/SvgExportBackend.cpp
    looks/be/BeControlLook.cpp
    looks/haiku/HaikuControlLook.cpp
    looks/haiku/HaikuLook.cpp
    platform/CpuPresenter.cpp
    platform/HeadlessPlatform.cpp
    platform/PlatformRegistry.cpp
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
# remote control over AF_UNIX sockets (Winsock)
if(WIN32)
    target_link_libraries(ngui PRIVATE ws2_32)
endif()

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

# The Blend2D backend (specification 14), its own library like Skia's:
# Blend2D headers appear in its sources only.
if(NGUI_WITH_BLEND2D)
    find_package(blend2d CONFIG REQUIRED)
    add_library(ngui_blend2d
        gfx/blend2d/Blend2DBackend.cpp)
    add_library(ngui::blend2d ALIAS ngui_blend2d)
    target_link_libraries(ngui_blend2d
        PUBLIC
            ngui::ngui
        PRIVATE
            blend2d::blend2d)
    ngui_configure_target(ngui_blend2d)
endif()

# The text subsystem (modules X1 to X5), its own library: HarfBuzz, SheenBidi,
# libunibreak, and the bundled fonts embedded as generated sources
# (data/fonts/).
if(NGUI_WITH_TEXT)
    find_package(harfbuzz CONFIG REQUIRED)
    find_package(libunibreak CONFIG REQUIRED)
    find_package(SheenBidi CONFIG REQUIRED)
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
        text/DirectWriteSource.cpp
        text/FontSource.cpp
        text/FontTextSystem.cpp
        text/FontconfigSource.cpp
        ${embeddedFonts})
    add_library(ngui::text ALIAS ngui_text)
    target_link_libraries(ngui_text
        PUBLIC
            ngui::ngui
        PRIVATE
            harfbuzz::harfbuzz
            libunibreak::libunibreak
            SheenBidi::SheenBidi
            # dlopen() of the system's fontconfig (none where libc has it)
            ${CMAKE_DL_LIBS})
    if(WIN32)
        # the system's fonts (DirectWriteSource.cpp)
        target_link_libraries(ngui_text PRIVATE dwrite)
    endif()
    target_include_directories(ngui_text PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    ngui_configure_target(ngui_text)
endif()

# Vector icons (module W7): lunasvg draws SVG into bitmaps, its own library:
# lunasvg headers appear in its sources only.
if(NGUI_WITH_SVG)
    find_package(lunasvg CONFIG REQUIRED)
    add_library(ngui_svg
        svg/LunaSvgRenderer.cpp)
    add_library(ngui::svg ALIAS ngui_svg)
    target_link_libraries(ngui_svg
        PUBLIC
            ngui::ngui
        PRIVATE
            lunasvg::lunasvg)
    ngui_configure_target(ngui_svg)
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
        test/Driver.cpp
        test/Image.cpp
        test/ImageCompare.cpp
        test/Lint.cpp
        test/Render.cpp
        test/Report.cpp
        test/Snapshot.cpp)
    add_library(ngui::testkit ALIAS ngui_testkit)
    target_link_libraries(ngui_testkit
        PUBLIC
            ngui::ngui
        PRIVATE
            PNG::PNG)
    # the driver reads the loopers' ports (LooperPort.h)
    target_include_directories(ngui_testkit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
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
