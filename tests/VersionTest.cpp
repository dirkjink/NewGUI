/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Version.h>

#include <string>

#include <gtest/gtest.h>


namespace {


TEST(VersionTest, LibraryMatchesHeaders)
{
	EXPECT_STREQ(ngui::LibraryVersionString(), ngui::kVersionString);
}


TEST(VersionTest, StringMatchesComponents)
{
	const std::string expected = std::to_string(ngui::kVersionMajor) + "."
		+ std::to_string(ngui::kVersionMinor) + "."
		+ std::to_string(ngui::kVersionPatch);

	EXPECT_EQ(expected, ngui::kVersionString);
}


} // namespace
