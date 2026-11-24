/*
 * Copyright 2006-2008, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The parts of Haiku's BIconUtils (src/libs/icon/IconUtils.cpp, commit
// b1d497bb69) that NewGUI has: GetVectorIcon() and GetSystemIcon(), for SVG
// instead of HVIF. Haiku drew an icon of another color space than B_RGBA32
// into a temporary B_RGBA32 bitmap and converted it; so does NewGUI.

#include <ngui/IconUtils.h>

#include <array>
#include <atomic>
#include <cstddef>

#include <ngui/Bitmap.h>


namespace ngui::embedded {

extern const unsigned char kDialogInformation[];
extern const std::size_t kDialogInformationSize;
extern const unsigned char kDialogIdea[];
extern const std::size_t kDialogIdeaSize;
extern const unsigned char kDialogWarning[];
extern const std::size_t kDialogWarningSize;
extern const unsigned char kDialogError[];
extern const std::size_t kDialogErrorSize;

} // namespace ngui::embedded


namespace ngui {


namespace {


std::atomic<const VectorIconRenderer*> sDefaultRenderer{nullptr};


struct SystemIcon {
	std::string_view	name;
	const unsigned char* data;
	const std::size_t*	size;
};


// NewGUI's own icons (data/icons/), built into the library.
const std::array<SystemIcon, 4> kSystemIcons = {{
	{"dialog-information", embedded::kDialogInformation, &embedded::kDialogInformationSize},
	{"dialog-idea", embedded::kDialogIdea, &embedded::kDialogIdeaSize},
	{"dialog-warning", embedded::kDialogWarning, &embedded::kDialogWarningSize},
	{"dialog-error", embedded::kDialogError, &embedded::kDialogErrorSize},
}};


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


std::span<const std::uint8_t>
BIconUtils::SystemIconData(std::string_view name)
{
	for (const SystemIcon& icon : kSystemIcons) {
		if (icon.name == name)
			return {icon.data, *icon.size};
	}
	return {};
}


} // namespace ngui
