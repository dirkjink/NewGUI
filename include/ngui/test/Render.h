/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <expected>

#include <ngui/Error.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/test/Image.h>


namespace ngui::test {


/*!	Renders \a list with \a backend into a fresh offscreen CPU surface and
	returns its pixels (specification 19.1, level a). The surface covers the
	bounds of the list at \a scale, rounded to device pixels, and is cleared
	to \a clear first; the left top corner of the bounds lands on pixel
	(0, 0). Fails with NotSupported if the backend has no CPU pixels.
*/
std::expected<Image, Error>		RenderToImage(gfx::Backend& backend,
									const gfx::DisplayList& list, float scale = 1,
									const gfx::Color& clear = {1, 1, 1, 1});


} // namespace ngui::test
