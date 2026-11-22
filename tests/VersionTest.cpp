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
	// What is tested: The version compiled into the library against the
	// version in the headers the test is built with.
	// Success criteria: LibraryVersionString() returns exactly kVersionString.
	// Why it matters: A program built against headers of another version than
	// the library it links would misbehave silently; this is how it can tell.
	EXPECT_STREQ(ngui::LibraryVersionString(), ngui::kVersionString);
}


TEST(VersionTest, StringMatchesComponents)
{
	// What is tested: The version string generated from the CMake project
	// version against the numeric components generated alongside it.
	// Success criteria: kVersionString equals "major.minor.patch" built from
	// kVersionMajor, kVersionMinor and kVersionPatch.
	// Why it matters: Both are generated from one template; a mismatch would
	// mean the template or its substitution is broken.
	//
	// Given
	const std::string expected = std::to_string(ngui::kVersionMajor) + "."
		+ std::to_string(ngui::kVersionMinor) + "."
		+ std::to_string(ngui::kVersionPatch);

	// When / Then
	EXPECT_EQ(expected, ngui::kVersionString);
}


} // namespace
