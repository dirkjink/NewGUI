/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/gfx/ImageRegistry.h>

#include <algorithm>
#include <utility>


namespace ngui::gfx {


ImageRegistry&
ImageRegistry::Default()
{
	// Never destroyed: DisplayLists give their references back in their
	// destructors, and a list in a static variable may outlive any registry
	// object destroyed at exit.
	static ImageRegistry* const sRegistry = new ImageRegistry();
	return *sRegistry;
}


std::expected<ImageHandle, Error>
ImageRegistry::Register(const PixelData& pixels)
{
	if (!pixels.IsValid())
		return Failure(ErrorCode::InvalidArgument, "invalid pixel data");

	// Packed copy: rows of width * 4 bytes.
	Image image = {pixels.width, pixels.height, {}, 1, {}};
	const std::size_t row = static_cast<std::size_t>(pixels.width) * 4;
	image.rgba.reserve(row * static_cast<std::size_t>(pixels.height));
	for (std::int32_t y = 0; y < pixels.height; y++) {
		const auto source = pixels.bytes.subspan(static_cast<std::size_t>(y) * pixels.stride, row);
		image.rgba.insert(image.rgba.end(), source.begin(), source.end());
	}

	const std::scoped_lock lock(fLock);
	if (fLastHandle == UINT32_MAX)
		return Failure(ErrorCode::OutOfRange, "image handles exhausted");
	const ImageHandle handle = ++fLastHandle;
	fImages.emplace(handle, std::move(image));
	return handle;
}


bool
ImageRegistry::Retain(ImageHandle image)
{
	const std::scoped_lock lock(fLock);
	const auto found = fImages.find(image);
	if (found == fImages.end())
		return false;
	found->second.references++;
	return true;
}


void
ImageRegistry::Release(ImageHandle image)
{
	const std::scoped_lock lock(fLock);
	const auto found = fImages.find(image);
	if (found == fImages.end())
		return;
	if (--found->second.references > 0)
		return;
	for (const auto& [backend, resource] : found->second.resources)
		backend->ReleaseImage(resource);
	fImages.erase(found);
}


std::uint32_t
ImageRegistry::ReferenceCount(ImageHandle image) const
{
	const std::scoped_lock lock(fLock);
	const auto found = fImages.find(image);
	return found == fImages.end() ? 0 : found->second.references;
}


std::optional<Size>
ImageRegistry::PixelSize(ImageHandle image) const
{
	const std::scoped_lock lock(fLock);
	const auto found = fImages.find(image);
	if (found == fImages.end())
		return std::nullopt;
	return Size{static_cast<float>(found->second.width),
		static_cast<float>(found->second.height)};
}


ImageHandle
ImageRegistry::BackendImage(ImageHandle image, Backend& backend)
{
	const std::scoped_lock lock(fLock);
	const auto found = fImages.find(image);
	if (found == fImages.end())
		return 0;

	Image& entry = found->second;
	const auto existing = std::ranges::find(entry.resources, &backend,
		&std::pair<Backend*, ImageHandle>::first);
	if (existing != entry.resources.end())
		return existing->second;

	const PixelData pixels = {entry.width, entry.height,
		static_cast<std::size_t>(entry.width) * 4, entry.rgba};
	const ImageHandle resource = backend.CreateImage(pixels);
	if (resource != 0)
		entry.resources.emplace_back(&backend, resource);
	return resource;
}


void
ImageRegistry::DetachBackend(Backend& backend)
{
	const std::scoped_lock lock(fLock);
	for (auto& [handle, image] : fImages) {
		std::erase_if(image.resources,
			[&](const auto& resource) { return resource.first == &backend; });
	}
}


} // namespace ngui::gfx
