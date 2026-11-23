/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>


namespace ngui::test {


/*!	Where snapshots are kept and what happens to them (specification 20.5). */
struct SnapshotSettings {
			std::filesystem::path referenceDirectory = "snapshots";
									/*!< The checked-in references. */
			std::filesystem::path reportDirectory = "snapshot-report";
									/*!< Where a failed comparison leaves its HTML report,
										with reference, result and difference. */
			bool				update = false;
									/*!< Take the results as new references instead of
										failing (`--update-snapshots`). */

	static	SnapshotSettings	FromEnvironment(
									const std::function<std::optional<std::string>(
										const char*)>& lookup = nullptr);
									/*!< The defaults, changed by NGUI_SNAPSHOT_DIR,
										NGUI_SNAPSHOT_REPORT_DIR and NGUI_UPDATE_SNAPSHOTS=1.
										\a lookup reads a variable; nullptr reads the
										environment of the process. */
};


/*!	The process-wide settings, from the environment at first use; the main
	of the GoogleTest adapter applies the command line with
	ConsumeSnapshotFlags(). */
SnapshotSettings&				Snapshots();

/*!	Applies and removes the snapshot arguments of a command line:
	`--update-snapshots`, `--snapshot-dir=<path>` and
	`--snapshot-report-dir=<path>`. They override the environment. */
void							ConsumeSnapshotFlags(int& argc, char** argv,
									SnapshotSettings& settings);


/*!	What a snapshot is of: its file is
	`<name>.<look>.<backend>.<scale>x.<extension>` (specification 20.5),
	e.g. `Button_pressed.be.skia.2x.png`. */
struct SnapshotKey {
			std::string			name;				/*!< Unique within the directory. */
			std::string			look = "none";		/*!< Look id, e.g. "be". */
			std::string			backend = "none";	/*!< Backend id, e.g. "skia", "dump". */
			float				scale = 1;			/*!< Scale of the rendering. */

			std::string			FileName(std::string_view extension) const;
									/*!< The file name with \a extension (without dot). */
};


/*!	How a snapshot check ended. */
enum class SnapshotOutcome {
	Matched,	/*!< The result matches the reference. */
	Updated,	/*!< Update mode: the reference was written. */
	Failed		/*!< No reference, or the result differs; see the report. */
};


/*!	The result of a snapshot check. */
struct SnapshotResult {
			SnapshotOutcome		outcome = SnapshotOutcome::Failed;
									/*!< How it ended. */
			std::string			message;
									/*!< What happened, with the report's path on failure. */
			std::filesystem::path report;
									/*!< The HTML report of a failure, else empty. */

			bool				Passed() const;
									/*!< Not Failed. */
};


/*!	Compares \a actual with the PNG reference of \a key under \a tolerance.
	A missing or different reference fails with a report, or is written in
	update mode. A reference within the tolerance is never rewritten, so
	updates change only what really changed. */
SnapshotResult					CheckImageSnapshot(const SnapshotSettings& settings,
									const SnapshotKey& key, const Image& actual,
									const Tolerance& tolerance = Tolerance::Exact());

/*!	Compares \a actual with the text reference of \a key (file extension
	\a extension), exactly, after converting CR LF to LF in the reference.
	Failures report a line diff. */
SnapshotResult					CheckTextSnapshot(const SnapshotSettings& settings,
									const SnapshotKey& key, std::string_view extension,
									std::string_view actual);


} // namespace ngui::test
