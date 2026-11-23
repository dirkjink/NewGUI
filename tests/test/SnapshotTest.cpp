/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Snapshot.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "test/Report.h"


namespace {


using namespace ngui::test;


// Settings with fresh reference and report directories under the temporary
// directory, removed again at the end of the test.
class TemporarySnapshots {
public:
	explicit TemporarySnapshots(const std::string& name)
		:
		fRoot(std::filesystem::temp_directory_path() / ("ngui-snapshot-test-" + name))
	{
		std::filesystem::remove_all(fRoot);
		settings.referenceDirectory = fRoot / "references";
		settings.reportDirectory = fRoot / "report";
	}

	~TemporarySnapshots()
	{
		std::error_code error;
		std::filesystem::remove_all(fRoot, error);
	}

	SnapshotSettings settings;

private:
	std::filesystem::path fRoot;
};


std::string
Contents(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}


TEST(SnapshotTest, FileNamesFollowTheSpecification)
{
	// What is tested: SnapshotKey::FileName() for full keys, fractional
	// scales and the defaults.
	// Success criteria: `<name>.<look>.<backend>.<scale>x.<extension>`:
	// "Button.be.skia.2x.png", "1.5x" for 1.5, "none" for an unset look or
	// backend.
	// Why it matters: The naming of specification 20.5 keeps one reference
	// per combination of look, backend and scale (the matrix of 18.7).
	//
	// When / Then
	EXPECT_EQ((SnapshotKey{"Button", "be", "skia", 2}).FileName("png"), "Button.be.skia.2x.png");
	EXPECT_EQ((SnapshotKey{"Button", "be", "skia", 1.5f}).FileName("png"),
		"Button.be.skia.1.5x.png");
	EXPECT_EQ((SnapshotKey{"List"}).FileName("txt"), "List.none.none.1x.txt");
}


TEST(SnapshotTest, ImageSnapshotsFailUpdateAndMatch)
{
	// What is tested: CheckImageSnapshot() through the life of a reference:
	// missing, written in update mode, matched, changed, and changed within
	// the tolerance in update mode.
	// Success criteria: Missing fails with a report that shows the result;
	// update writes the reference; the same image then matches; a changed
	// pixel fails with a report holding reference, result and difference
	// and naming --update-snapshots; a change within the tolerance matches
	// even in update mode and leaves the reference file untouched.
	// Why it matters: This is the snapshot mechanism of specification 20.5;
	// rewriting references that are within the tolerance would make every
	// update churn antialiased pixels.
	//
	// Given
	TemporarySnapshots snapshots("image");
	const SnapshotKey key{"Box", "be", "skia", 1};
	const Image image(4, 4, {255, 255, 255, 255});
	Image changed = image;
	changed.Set(1, 1, {0, 0, 0, 255});
	Image nearly = image;
	nearly.Set(1, 1, {250, 255, 255, 255});
	const std::filesystem::path reference = snapshots.settings.referenceDirectory
		/ "Box.be.skia.1x.png";
	const std::filesystem::path report = snapshots.settings.reportDirectory
		/ "Box.be.skia.1x.html";

	// When / Then: no reference yet.
	const SnapshotResult missing = CheckImageSnapshot(snapshots.settings, key, image);
	EXPECT_EQ(missing.outcome, SnapshotOutcome::Failed);
	EXPECT_EQ(missing.report, report);
	EXPECT_TRUE(std::filesystem::exists(snapshots.settings.reportDirectory
		/ "Box.be.skia.1x.actual.png"));
	EXPECT_FALSE(std::filesystem::exists(reference));

	// When / Then: update mode writes it, and afterwards it matches.
	SnapshotSettings update = snapshots.settings;
	update.update = true;
	EXPECT_EQ(CheckImageSnapshot(update, key, image).outcome, SnapshotOutcome::Updated);
	EXPECT_EQ(*ReadPng(reference), image);
	EXPECT_EQ(CheckImageSnapshot(snapshots.settings, key, image).outcome,
		SnapshotOutcome::Matched);

	// When / Then: a changed pixel fails with the full report.
	const SnapshotResult different = CheckImageSnapshot(snapshots.settings, key, changed);
	EXPECT_EQ(different.outcome, SnapshotOutcome::Failed);
	EXPECT_NE(different.message.find("1 of 16 pixels differ"), std::string::npos);
	EXPECT_NE(different.message.find("--update-snapshots"), std::string::npos);
	EXPECT_NE(different.message.find(report.string()), std::string::npos);
	const std::string page = Contents(report);
	for (const char* file : {"Box.be.skia.1x.reference.png", "Box.be.skia.1x.actual.png",
			"Box.be.skia.1x.difference.png"}) {
		EXPECT_NE(page.find(file), std::string::npos) << file;
		EXPECT_TRUE(std::filesystem::exists(snapshots.settings.reportDirectory / file)) << file;
	}

	// When / Then: within the tolerance, the reference stays as it is.
	const std::string before = Contents(reference);
	EXPECT_EQ(CheckImageSnapshot(update, key, nearly, {0.1, 5}).outcome,
		SnapshotOutcome::Matched);
	EXPECT_EQ(Contents(reference), before);
}


TEST(SnapshotTest, TextSnapshotsShowALineDiff)
{
	// What is tested: CheckTextSnapshot() with a missing reference, update,
	// a reference with CR LF line ends, and a changed text with characters
	// that HTML escapes.
	// Success criteria: Missing fails; update writes the text; a CR LF copy
	// of the same text matches; a change fails with a report whose diff
	// marks the old line "-" and the new one "+", with "<" escaped.
	// Why it matters: DisplayList snapshots are text (specification 20.5);
	// a Windows checkout may convert their line ends, and the diff is what
	// shows which operation changed.
	//
	// Given
	TemporarySnapshots snapshots("text");
	SnapshotSettings update = snapshots.settings;
	update.update = true;
	const SnapshotKey key{"List", "be", "dump", 1};
	const std::string text = "first\nsecond\nthird\n";
	const std::filesystem::path reference = snapshots.settings.referenceDirectory
		/ "List.be.dump.1x.txt";

	// When / Then
	EXPECT_EQ(CheckTextSnapshot(snapshots.settings, key, "txt", text).outcome,
		SnapshotOutcome::Failed);
	EXPECT_EQ(CheckTextSnapshot(update, key, "txt", text).outcome, SnapshotOutcome::Updated);
	EXPECT_EQ(Contents(reference), text);
	{
		std::ofstream file(reference, std::ios::binary | std::ios::trunc);
		file << "first\r\nsecond\r\nthird\r\n";
	}
	EXPECT_EQ(CheckTextSnapshot(snapshots.settings, key, "txt", text).outcome,
		SnapshotOutcome::Matched);

	const SnapshotResult changed = CheckTextSnapshot(snapshots.settings, key, "txt",
		"first\n<second>\nthird\n");
	EXPECT_EQ(changed.outcome, SnapshotOutcome::Failed);
	const std::string page = Contents(changed.report);
	EXPECT_NE(page.find("<span class=\"del\">- second</span>"), std::string::npos) << page;
	EXPECT_NE(page.find("<span class=\"ins\">+ &lt;second&gt;</span>"), std::string::npos)
		<< page;
	EXPECT_NE(page.find("<pre>  first\n"), std::string::npos) << page;
}


TEST(SnapshotTest, LineDiffIsMinimal)
{
	// What is tested: DiffLines() for an insertion, a removal and a
	// replacement in the middle of a text.
	// Success criteria: Common lines stay unmarked; only the changed lines
	// are "-" or "+", the removed line before the added one.
	// Why it matters: A diff that marks too much hides the one operation of
	// a DisplayList that changed.
	//
	// When
	const std::vector<DiffLine> diff = DiffLines("a\nb\nc\nd\n", "a\nx\nc\nd\ne\n");

	// Then
	std::string marks;
	for (const DiffLine& line : diff)
		marks += std::string(1, line.kind) + line.text + "|";
	EXPECT_EQ(marks, " a|-b|+x| c| d|+e|");
}


TEST(SnapshotTest, SettingsComeFromEnvironmentAndFlag)
{
	// What is tested: SnapshotSettings::FromEnvironment() with and without
	// the variables, and ConsumeSnapshotFlags() on a command line.
	// Success criteria: Defaults "snapshots" and "snapshot-report" without
	// update; the variables set both directories, and update only for "1";
	// the three arguments set update and both directories over the
	// environment and are removed from argv, the others keep their order.
	// Why it matters: CI and developers switch the directories and the
	// update mode this way (specification 20.5).
	//
	// Given
	using Value = std::optional<std::string>;
	const auto none = [](const char*) -> Value { return std::nullopt; };
	const auto all = [](const char* name) -> Value {
		if (std::strcmp(name, "NGUI_SNAPSHOT_DIR") == 0)
			return "/refs";
		if (std::strcmp(name, "NGUI_SNAPSHOT_REPORT_DIR") == 0)
			return "/report";
		if (std::strcmp(name, "NGUI_UPDATE_SNAPSHOTS") == 0)
			return "1";
		return std::nullopt;
	};
	const auto zero = [](const char* name) -> Value {
		if (std::strcmp(name, "NGUI_UPDATE_SNAPSHOTS") == 0)
			return "0";
		return std::nullopt;
	};
	std::string program = "tests";
	std::string filter = "--gtest_filter=X";
	std::string flag = "--update-snapshots";
	std::string directory = "--snapshot-dir=/flag/refs";
	std::string other = "extra";
	std::string report = "--snapshot-report-dir=/flag/report";
	std::vector<char*> argv = {program.data(), filter.data(), flag.data(), directory.data(),
		other.data(), report.data(), nullptr};
	int argc = 6;

	// When
	const SnapshotSettings defaults = SnapshotSettings::FromEnvironment(none);
	const SnapshotSettings set = SnapshotSettings::FromEnvironment(all);
	SnapshotSettings fromFlags = SnapshotSettings::FromEnvironment(all);
	fromFlags.update = false;
	ConsumeSnapshotFlags(argc, argv.data(), fromFlags);

	// Then
	EXPECT_EQ(defaults.referenceDirectory, "snapshots");
	EXPECT_EQ(defaults.reportDirectory, "snapshot-report");
	EXPECT_FALSE(defaults.update);
	EXPECT_EQ(set.referenceDirectory, "/refs");
	EXPECT_EQ(set.reportDirectory, "/report");
	EXPECT_TRUE(set.update);
	EXPECT_FALSE(SnapshotSettings::FromEnvironment(zero).update);
	EXPECT_TRUE(fromFlags.update);
	EXPECT_EQ(fromFlags.referenceDirectory, "/flag/refs");
	EXPECT_EQ(fromFlags.reportDirectory, "/flag/report");
	ASSERT_EQ(argc, 3);
	EXPECT_STREQ(argv[1], "--gtest_filter=X");
	EXPECT_STREQ(argv[2], "extra");
	EXPECT_EQ(argv[3], nullptr);
}


} // namespace
