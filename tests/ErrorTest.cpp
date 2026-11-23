/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Error.h>

#include <expected>
#include <string>

#include <gtest/gtest.h>


namespace {


std::expected<int, ngui::Error>
ParsePositive(int value)
{
	if (value <= 0)
		return ngui::Failure(ngui::ErrorCode::InvalidArgument, "not positive");
	return value;
}


TEST(ErrorTest, FailureBuildsTheUnexpectedValue)
{
	// What is tested: A function returning std::expected<T, ngui::Error> that
	// reports an error with Failure().
	// Success criteria: The success path carries the value; the error path
	// carries code and message.
	// Why it matters: This is the error model of the whole API (CONVENTIONS.md,
	// section 3, rule 2); it replaces status_t and must stay this short to use.
	//
	// When
	const auto good = ParsePositive(3);
	const auto bad = ParsePositive(-3);

	// Then
	ASSERT_TRUE(good.has_value());
	EXPECT_EQ(*good, 3);
	ASSERT_FALSE(bad.has_value());
	EXPECT_EQ(bad.error(), (ngui::Error{ngui::ErrorCode::InvalidArgument, "not positive"}));
}


TEST(ErrorTest, EveryCodeHasItsName)
{
	// What is tested: ErrorCodeName() for every code.
	// Success criteria: Each code maps to its name as written in the source.
	// Why it matters: Logs and test failures show the name; a missing case
	// would print "Unknown".
	//
	// When / Then
	using ngui::ErrorCode;
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::InvalidArgument), "InvalidArgument");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::OutOfRange), "OutOfRange");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::NotSupported), "NotSupported");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::NotFound), "NotFound");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::OutOfMemory), "OutOfMemory");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::BadData), "BadData");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::IoError), "IoError");
	EXPECT_STREQ(ngui::ErrorCodeName(ErrorCode::TimedOut), "TimedOut");
}


} // namespace
