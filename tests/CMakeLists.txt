# Copyright 2026, The NewGUI Authors. All rights reserved.
# Distributed under the terms of the MIT License.
# SPDX-License-Identifier: MIT

find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(ngui_tests
    SupportDefsTest.cpp
    VersionTest.cpp)

target_link_libraries(ngui_tests
    PRIVATE
        ngui::ngui
        GTest::gtest
        GTest::gtest_main)

ngui_configure_target(ngui_tests)

gtest_discover_tests(ngui_tests DISCOVERY_MODE PRE_TEST)
