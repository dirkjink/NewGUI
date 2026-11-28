/*
 * Copyright 2006-2008, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The parts of Haiku's BIconUtils (src/libs/icon/IconUtils.cpp, commit
// b1d497bb69) that NewGUI has: GetVectorIcon() and GetSystemIcon(), for SVG
// instead of HVIF. Haiku drew an icon of another color space than B_RGBA32
// into a temporary B_RGBA32 bitmap and converted it; so does NewGUI. The
// system icons are Haiku's as SVG (data/icons/haiku/): those NewGUI shows
// itself built in, the others read from the icon directory.

#include <ngui/IconUtils.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <ngui/Bitmap.h>
#include <ngui/MimeType.h>

#include "Environment.h"


namespace ngui::embedded {

extern const unsigned char kAlertInfo[];
extern const std::size_t kAlertInfoSize;
extern const unsigned char kAlertIdea[];
extern const std::size_t kAlertIdeaSize;
extern const unsigned char kAlertWarning[];
extern const std::size_t kAlertWarningSize;
extern const unsigned char kAlertStop[];
extern const std::size_t kAlertStopSize;
extern const unsigned char kNotifyNotice[];
extern const std::size_t kNotifyNoticeSize;
extern const unsigned char kNotifyAchtung[];
extern const std::size_t kNotifyAchtungSize;
extern const unsigned char kNotifyDanger[];
extern const std::size_t kNotifyDangerSize;

} // namespace ngui::embedded


namespace ngui {


namespace {


std::atomic<const VectorIconRenderer*> sDefaultRenderer{nullptr};


struct SystemIcon {
	std::string_view	name;
	const unsigned char* data;
	const std::size_t*	size;
};


// The icons NewGUI shows itself, built into the library (the list in
// src/CMakeLists.txt).
const std::array<SystemIcon, 7> kSystemIcons = {{
	{"Alert_Info", embedded::kAlertInfo, &embedded::kAlertInfoSize},
	{"Alert_Idea", embedded::kAlertIdea, &embedded::kAlertIdeaSize},
	{"Alert_Warning", embedded::kAlertWarning, &embedded::kAlertWarningSize},
	{"Alert_Stop", embedded::kAlertStop, &embedded::kAlertStopSize},
	{"Notify_Notice", embedded::kNotifyNotice, &embedded::kNotifyNoticeSize},
	{"Notify_Achtung", embedded::kNotifyAchtung, &embedded::kNotifyAchtungSize},
	{"Notify_Danger", embedded::kNotifyDanger, &embedded::kNotifyDangerSize},
}};

// Larger files are no icons (Haiku's largest SVG has some 100 KB).
constexpr std::uintmax_t kLargestIconFile = 4 * 1024 * 1024;

// The icon directory set and the icons read from it, by path: read once,
// kept for the process so their spans stay valid.
std::mutex sIconLock;
std::optional<std::filesystem::path> sIconDirectory;
std::map<std::filesystem::path, std::unique_ptr<const std::vector<std::uint8_t>>> sReadIcons;


// Whether \a name names a file below the icon directory: relative, without
// "..", "." or empty parts, without a backslash, colon or NUL.
bool
IsIconName(std::string_view name)
{
	if (name.empty() || name.find_first_of(std::string_view("\\:\0", 3)) != std::string_view::npos)
		return false;
	std::size_t start = 0;
	while (start <= name.size()) {
		std::size_t slash = name.find('/', start);
		if (slash == std::string_view::npos)
			slash = name.size();
		const std::string_view part = name.substr(start, slash - start);
		if (part.empty() || part == "." || part == "..")
			return false;
		start = slash + 1;
	}
	return true;
}


// The SVG of the icon \a name in the icon directory, empty without it.
std::span<const std::uint8_t>
ReadIcon(std::string_view name)
{
	if (!IsIconName(name))
		return {};
	const std::filesystem::path directory = BIconUtils::SystemIconDirectory();
	if (directory.empty())
		return {};
	std::u8string file(name.begin(), name.end());
	file += u8".svg";
	const std::filesystem::path path = directory / std::filesystem::path(file);

	std::scoped_lock lock(sIconLock);
	if (const auto found = sReadIcons.find(path); found != sReadIcons.end())
		return *found->second;
	std::error_code error;
	if (!std::filesystem::is_regular_file(path, error))
		return {};
	const std::uintmax_t size = std::filesystem::file_size(path, error);
	if (error || size == 0 || size > kLargestIconFile)
		return {};
	std::ifstream stream(path, std::ios::binary);
	auto data = std::make_unique<std::vector<std::uint8_t>>(
		(std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
	if (!stream.good() && !stream.eof())
		return {};
	const std::span<const std::uint8_t> read(*data);
	sReadIcons.emplace(path, std::move(data));
	return read;
}


} // namespace


const VectorIconRenderer*
VectorIconRenderer::Default()
{
	return sDefaultRenderer.load();
}


void
VectorIconRenderer::SetDefault(const VectorIconRenderer* renderer)
{
	sDefaultRenderer.store(renderer);
}


std::expected<void, Error>
BIconUtils::GetVectorIcon(std::span<const std::uint8_t> icon, BBitmap* result)
{
	if (result == nullptr || !result->IsValid())
		return Failure(ErrorCode::InvalidArgument, "no valid bitmap");
	const VectorIconRenderer* renderer = VectorIconRenderer::Default();
	if (renderer == nullptr)
		return Failure(ErrorCode::NotSupported, "no vector icon renderer");

	if (result->ColorSpace() == BColorSpace::RGBA32)
		return renderer->Render(icon, *result);

	// Draw into a B_RGBA32 bitmap of the same size, then convert.
	BBitmap temporary(result->Bounds(), BColorSpace::RGBA32);
	if (!temporary.IsValid())
		return Failure(ErrorCode::OutOfMemory, "no temporary bitmap");
	if (auto drawn = renderer->Render(icon, temporary); !drawn.has_value())
		return drawn;
	return result->ImportBits(temporary);
}


std::expected<void, Error>
BIconUtils::GetSystemIcon(std::string_view name, BBitmap* result)
{
	const std::span<const std::uint8_t> icon = SystemIconData(name);
	if (icon.empty())
		return Failure(ErrorCode::NotFound, "no system icon of that name");
	return GetVectorIcon(icon, result);
}


std::expected<void, Error>
BIconUtils::GetTypeIcon(const BMimeType& type, BBitmap* result)
{
	// BNodeInfo::GetTrackerIcon() without preferred applications: the type's
	// icon, else the supertype's
	if (type.GetIcon(result).has_value())
		return {};
	if (const auto super = type.GetSupertype(); super.has_value()) {
		if (super->GetIcon(result).has_value())
			return {};
	}
	return Failure(ErrorCode::NotFound, "neither the type nor its supertype has an icon");
}


std::expected<void, Error>
BIconUtils::GetFileIcon(std::string_view path, BBitmap* result)
{
	const std::filesystem::path file(std::u8string(path.begin(), path.end()));
	std::error_code error;
	const std::filesystem::file_status link = std::filesystem::symlink_status(file, error);
	if (error || !std::filesystem::exists(link))
		return Failure(ErrorCode::NotFound, "no such file");
	// Tracker shows a link with its target's icon
	const std::filesystem::file_status status = std::filesystem::status(file, error);
	if (std::filesystem::is_symlink(link) && (error || !std::filesystem::exists(status)))
		return GetTypeIcon(BMimeType(kSymlinkMimeType), result);
	if (std::filesystem::is_directory(status)) {
		// the root of a file system is a volume
		const std::filesystem::path absolute = std::filesystem::absolute(file, error);
		const bool root = !error && absolute == absolute.root_path();
		return GetTypeIcon(BMimeType(root ? std::string_view("application/x-vnd.Be-volume")
			: kDirectoryMimeType), result);
	}
	// a link's type by the name of what it leads to
	std::string name(path);
	if (std::filesystem::is_symlink(link)) {
		const std::u8string target = std::filesystem::canonical(file, error).u8string();
		if (!error)
			name.assign(target.begin(), target.end());
	}
	const BMimeType type = BMimeType::GuessMimeType(name);
	if (type == kFileMimeType && std::filesystem::is_regular_file(status)) {
#ifdef _WIN32
		std::u8string extension = file.extension().u8string();
		for (char8_t& c : extension) {
			if (c >= u8'A' && c <= u8'Z')
				c = static_cast<char8_t>(c - u8'A' + u8'a');
		}
		const bool runs = extension == u8".exe" || extension == u8".com"
			|| extension == u8".bat" || extension == u8".cmd";
#else
		const bool runs = (status.permissions() & std::filesystem::perms::owner_exec)
			!= std::filesystem::perms::none;
#endif
		if (runs)
			return GetTypeIcon(BMimeType(kAppMimeType), result);
	}
	return GetTypeIcon(type, result);
}


std::span<const std::uint8_t>
BIconUtils::SystemIconData(std::string_view name)
{
	for (const SystemIcon& icon : kSystemIcons) {
		if (icon.name == name)
			return {icon.data, *icon.size};
	}
	return ReadIcon(name);
}


void
BIconUtils::SetSystemIconDirectory(const std::filesystem::path& directory)
{
	std::scoped_lock lock(sIconLock);
	if (directory.empty())
		sIconDirectory.reset();
	else
		sIconDirectory = directory;
}


std::filesystem::path
BIconUtils::SystemIconDirectory()
{
	{
		std::scoped_lock lock(sIconLock);
		if (sIconDirectory.has_value())
			return *sIconDirectory;
	}
	if (const std::optional<std::string> variable = detail::EnvironmentVariable("NGUI_ICON_DIR"))
		return std::filesystem::path(std::u8string(variable->begin(), variable->end()));
	return {};
}


} // namespace ngui
