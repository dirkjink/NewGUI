/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/test/Snapshot.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <vector>

#include "Report.h"


namespace ngui::test {


namespace {


std::string
Stem(const SnapshotKey& key)
{
	char scale[32];
	std::snprintf(scale, sizeof(scale), "%g", static_cast<double>(key.scale));
	return key.name + "." + key.look + "." + key.backend + "." + scale + "x";
}


std::optional<std::string>
ReadText(const std::filesystem::path& path)
{
	std::ifstream file(path, std::ios::binary);
	if (!file)
		return std::nullopt;
	std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
	// A checkout may have turned LF into CR LF.
	std::string normalized;
	normalized.reserve(text.size());
	for (std::size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
			continue;
		normalized += text[i];
	}
	return normalized;
}


bool
WriteText(const std::filesystem::path& path, std::string_view text)
{
	std::ofstream file(path, std::ios::binary | std::ios::trunc);
	file.write(text.data(), static_cast<std::streamsize>(text.size()));
	return static_cast<bool>(file);
}


bool
MakeDirectory(const std::filesystem::path& directory)
{
	std::error_code error;
	std::filesystem::create_directories(directory, error);
	return !error;
}


SnapshotResult
Updated(const std::filesystem::path& reference, bool written)
{
	if (!written)
		return {SnapshotOutcome::Failed, "cannot write the reference " + reference.string(), {}};
	return {SnapshotOutcome::Updated, "reference written: " + reference.string(), {}};
}


SnapshotResult
Failed(const std::string& why, const std::filesystem::path& report)
{
	std::string message = why;
	if (report.empty())
		message += " (no report: cannot write the report directory)";
	else
		message += "; report: " + report.string();
	message += "; to take the result as the reference, run with --update-snapshots or"
		" NGUI_UPDATE_SNAPSHOTS=1";
	return {SnapshotOutcome::Failed, message, report};
}


} // namespace


SnapshotSettings
SnapshotSettings::FromEnvironment(const std::function<const char*(const char*)>& lookup)
{
	const auto get = [&](const char* name) -> const char* {
		return lookup ? lookup(name) : std::getenv(name);
	};
	SnapshotSettings settings;
	if (const char* directory = get("NGUI_SNAPSHOT_DIR"); directory != nullptr && *directory)
		settings.referenceDirectory = directory;
	if (const char* directory = get("NGUI_SNAPSHOT_REPORT_DIR");
			directory != nullptr && *directory) {
		settings.reportDirectory = directory;
	}
	if (const char* update = get("NGUI_UPDATE_SNAPSHOTS"); update != nullptr)
		settings.update = std::strcmp(update, "1") == 0;
	return settings;
}


SnapshotSettings&
Snapshots()
{
	static SnapshotSettings settings = SnapshotSettings::FromEnvironment();
	return settings;
}


void
ConsumeSnapshotFlags(int& argc, char** argv, SnapshotSettings& settings)
{
	const auto value = [](const char* argument, std::string_view prefix) -> const char* {
		return std::strncmp(argument, prefix.data(), prefix.size()) == 0
			? argument + prefix.size() : nullptr;
	};
	int kept = 0;
	for (int i = 0; i < argc; i++) {
		if (i > 0) {
			if (std::strcmp(argv[i], "--update-snapshots") == 0) {
				settings.update = true;
				continue;
			}
			if (const char* path = value(argv[i], "--snapshot-dir="); path != nullptr) {
				settings.referenceDirectory = path;
				continue;
			}
			if (const char* path = value(argv[i], "--snapshot-report-dir="); path != nullptr) {
				settings.reportDirectory = path;
				continue;
			}
		}
		argv[kept++] = argv[i];
	}
	argc = kept;
	// argv[argc] is the null pointer that ends the list.
	if (argv != nullptr)
		argv[kept] = nullptr;
}


std::string
SnapshotKey::FileName(std::string_view extension) const
{
	return Stem(*this) + "." + std::string(extension);
}


bool
SnapshotResult::Passed() const
{
	return outcome != SnapshotOutcome::Failed;
}


SnapshotResult
CheckImageSnapshot(const SnapshotSettings& settings, const SnapshotKey& key,
	const Image& actual, const Tolerance& tolerance)
{
	const std::filesystem::path reference = settings.referenceDirectory / key.FileName("png");
	std::optional<Image> expected;
	if (std::filesystem::exists(reference)) {
		std::expected<Image, Error> read = ReadPng(reference);
		if (!read.has_value() && !settings.update) {
			const std::filesystem::path report = WriteImageReport(settings.reportDirectory,
				Stem(key), nullptr, actual, nullptr, read.error().message);
			return Failed("unreadable reference " + reference.string() + ": "
				+ read.error().message, report);
		}
		if (read.has_value())
			expected = std::move(*read);
	}

	if (expected.has_value()) {
		const ImageComparison comparison = CompareImages(*expected, actual, tolerance);
		if (comparison.passed)
			return {SnapshotOutcome::Matched, comparison.Describe(), {}};
		if (!settings.update) {
			const std::filesystem::path report = WriteImageReport(settings.reportDirectory,
				Stem(key), &*expected, actual, &comparison, comparison.Describe());
			return Failed(key.FileName("png") + ": " + comparison.Describe(), report);
		}
	} else if (!settings.update) {
		const std::filesystem::path report = WriteImageReport(settings.reportDirectory,
			Stem(key), nullptr, actual, nullptr, "no reference");
		return Failed("no reference " + reference.string(), report);
	}

	return Updated(reference, MakeDirectory(settings.referenceDirectory)
		&& WritePng(actual, reference).has_value());
}


SnapshotResult
CheckTextSnapshot(const SnapshotSettings& settings, const SnapshotKey& key,
	std::string_view extension, std::string_view actual)
{
	const std::filesystem::path reference = settings.referenceDirectory
		/ key.FileName(extension);
	const std::optional<std::string> expected = ReadText(reference);
	if (expected.has_value() && *expected == actual)
		return {SnapshotOutcome::Matched, "identical", {}};
	if (!settings.update) {
		const std::filesystem::path report = WriteTextReport(settings.reportDirectory,
			Stem(key), extension, expected.has_value() ? &*expected : nullptr, actual);
		return Failed(expected.has_value() ? key.FileName(extension) + ": the text differs"
			: "no reference " + reference.string(), report);
	}
	return Updated(reference, MakeDirectory(settings.referenceDirectory)
		&& WriteText(reference, actual));
}


} // namespace ngui::test
