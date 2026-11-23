/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Haiku's BBitmap as far as the ported looks use it: an icon with Bounds(),
// over an ImageHandle of ImageRegistry::Default() (specification 9).


#include <optional>

#include <ngui/gfx/ImageRegistry.h>

#include "Rect.h"


namespace ngui::compat {


class BBitmap {
public:
	explicit					BBitmap(gfx::ImageHandle handle)
									:
									fHandle(handle)
								{
								}

			gfx::ImageHandle	Handle() const
								{
									return fHandle;
								}

			// Inclusive, as in Haiku: BRect(0, 0, width - 1, height - 1).
			BRect				Bounds() const
								{
									const std::optional<gfx::Size> size
										= gfx::ImageRegistry::Default().PixelSize(fHandle);
									if (!size.has_value())
										return BRect();
									return BRect(0, 0, size->w - 1, size->h - 1);
								}

private:
			gfx::ImageHandle	fHandle;
};


} // namespace ngui::compat
