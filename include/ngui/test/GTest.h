/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


/*!	\file
	The snapshot macros for GoogleTest (specification 20.5, 20.9). A
	snapshot is named `<test suite>_<name>`; its directories and the update
	mode come from Snapshots(). Link `ngui::testkit_main` for a main that
	understands `--update-snapshots`, `--snapshot-dir=` and
	`--snapshot-report-dir=`.

	\code
	EXPECT_LIST_SNAPSHOT(*list, "pressed", {.look = "be"});
	EXPECT_IMAGE_SNAPSHOT(image, "pressed",
		{.look = "be", .backend = "skia", .scale = 2});
	\endcode
*/


#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <ngui/Bitmap.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Dump.h>
#include <ngui/test/Driver.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Snapshot.h>


namespace ngui::test {


/*!	What a snapshot macro compares under. */
struct SnapshotOptions {
			std::string			look = "none";		/*!< Look id for the file name. */
			std::string			backend = "none";	/*!< Backend id; "dump" for lists. */
			float				scale = 1;			/*!< Scale for the file name. */
			Tolerance			tolerance = Tolerance::Exact();
									/*!< For images (specification 21.2). */
};


/*!	The key of snapshot \a name in the running test. */
inline SnapshotKey
CurrentSnapshotKey(std::string_view name, const SnapshotOptions& options)
{
	const ::testing::TestInfo* test = ::testing::UnitTest::GetInstance()->current_test_info();
	std::string prefix = test != nullptr ? std::string(test->test_suite_name()) + "_" : "";
	return {prefix + std::string(name), options.look, options.backend, options.scale};
}


/*!	\a image against its reference, for EXPECT_IMAGE_SNAPSHOT. */
inline ::testing::AssertionResult
MatchesImageSnapshot(const Image& image, std::string_view name,
	const SnapshotOptions& options = {})
{
	const SnapshotResult result = CheckImageSnapshot(Snapshots(),
		CurrentSnapshotKey(name, options), image, options.tolerance);
	if (result.Passed())
		return ::testing::AssertionSuccess() << result.message;
	return ::testing::AssertionFailure() << result.message;
}


/*!	\a bitmap (a snapshot) against its reference, for EXPECT_IMAGE_SNAPSHOT. */
inline ::testing::AssertionResult
MatchesImageSnapshot(const BBitmap& bitmap, std::string_view name,
	const SnapshotOptions& options = {})
{
	return MatchesImageSnapshot(Image::FromBitmap(bitmap), name, options);
}


/*!	A snapshot of \a window (WindowRef::Snapshot() at the options' scale)
	against its reference, for EXPECT_IMAGE_SNAPSHOT (specification 20.5). */
inline ::testing::AssertionResult
MatchesImageSnapshot(const WindowRef& window, std::string_view name,
	const SnapshotOptions& options = {})
{
	BSnapshotOptions snapshot;
	snapshot.scale = options.scale;
	const std::expected<BBitmap, Error> bitmap = window.Snapshot(snapshot);
	if (!bitmap.has_value())
		return ::testing::AssertionFailure() << "no snapshot: " << bitmap.error().message;
	return MatchesImageSnapshot(*bitmap, name, options);
}


/*!	The dump of \a list (DumpText()) against its reference, for
	EXPECT_LIST_SNAPSHOT. The backend in the file name is "dump" unless
	\a options says otherwise. */
inline ::testing::AssertionResult
MatchesListSnapshot(const gfx::DisplayList& list, std::string_view name,
	SnapshotOptions options = {})
{
	if (options.backend == "none")
		options.backend = "dump";
	const SnapshotResult result = CheckTextSnapshot(Snapshots(),
		CurrentSnapshotKey(name, options), "txt", gfx::DumpText(list));
	if (result.Passed())
		return ::testing::AssertionSuccess() << result.message;
	return ::testing::AssertionFailure() << result.message;
}


} // namespace ngui::test


/*!	Expects \a image to match snapshot \a name; optional SnapshotOptions. */
#define EXPECT_IMAGE_SNAPSHOT(image, ...) \
	EXPECT_TRUE(::ngui::test::MatchesImageSnapshot((image), __VA_ARGS__))

/*!	As EXPECT_IMAGE_SNAPSHOT, but ends the test on failure. */
#define ASSERT_IMAGE_SNAPSHOT(image, ...) \
	ASSERT_TRUE(::ngui::test::MatchesImageSnapshot((image), __VA_ARGS__))

/*!	Expects the dump of DisplayList \a list to match snapshot \a name. */
#define EXPECT_LIST_SNAPSHOT(list, ...) \
	EXPECT_TRUE(::ngui::test::MatchesListSnapshot((list), __VA_ARGS__))

/*!	As EXPECT_LIST_SNAPSHOT, but ends the test on failure. */
#define ASSERT_LIST_SNAPSHOT(list, ...) \
	ASSERT_TRUE(::ngui::test::MatchesListSnapshot((list), __VA_ARGS__))
