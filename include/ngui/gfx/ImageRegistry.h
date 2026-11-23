/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <ngui/Error.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Size.h>


namespace ngui::gfx {


/*!	The frontend-wide images (specification 9): hands out the ImageHandle
	that Views draw with, keeps the pixels, counts references, and maps each
	handle to the resource of every backend that drew it. Switching the
	backend therefore needs no change in any View.

	- Register() copies the pixels and returns a handle with one reference,
	  which belongs to the caller. Handles are never reused.
	- Every DisplayList that draws an image holds a reference of its own
	  (taken when it is finished or deserialized, given back when it is
	  destroyed), so an image lives as long as any list uses it.
	- The last Release() frees the pixels and calls Backend::ReleaseImage()
	  on every backend that has a resource for it.

	Thread-safe. The registry calls Backend::CreateImage() and
	Backend::ReleaseImage() while holding its lock; a backend must not call
	back into the registry from them.
*/
class ImageRegistry {
public:
								ImageRegistry() = default;
									/*!< An empty registry; normally Default() is used. */
								~ImageRegistry() = default;

								ImageRegistry(const ImageRegistry&) = delete;
									/*!< Not copyable. */
			ImageRegistry&		operator=(const ImageRegistry&) = delete;
									/*!< Not copyable. */

	static	ImageRegistry&		Default();
									/*!< The registry of the process; the one DisplayLists
										use. */

			std::expected<ImageHandle, Error> Register(const PixelData& pixels);
									/*!< A new handle for a copy of \a pixels, with one
										reference; ErrorCode::InvalidArgument if \a pixels is
										not valid. */
			bool				Retain(ImageHandle image);
									/*!< Adds a reference; false (and nothing changes) for an
										unknown handle. */
			void				Release(ImageHandle image);
									/*!< Gives a reference back; the last one frees the image.
										Unknown handles are ignored. */

			std::uint32_t		ReferenceCount(ImageHandle image) const;
									/*!< References of \a image; 0 for an unknown handle. */
			std::optional<Size>	PixelSize(ImageHandle image) const;
									/*!< Width and height in pixels, if \a image is known. */

			ImageHandle			BackendImage(ImageHandle image, Backend& backend);
									/*!< The resource of \a backend for \a image, created
										with Backend::CreateImage() on first use; 0 for an
										unknown handle or if the backend fails. */
			void				DetachBackend(Backend& backend);
									/*!< Forgets all resources of \a backend, without calling
										it; for its destructor. */

private:
			struct Image {
				std::int32_t	width;
				std::int32_t	height;
				std::vector<std::uint8_t> rgba;
				std::uint32_t	references;
				std::vector<std::pair<Backend*, ImageHandle>> resources;
			};

	mutable	std::mutex			fLock;
			std::unordered_map<ImageHandle, Image> fImages;
			ImageHandle			fLastHandle = 0;
};


} // namespace ngui::gfx
