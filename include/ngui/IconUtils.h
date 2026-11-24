/*
 * Copyright 2006-2008, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

#include <ngui/Error.h>


namespace ngui {


class BBitmap;


/*!	Turns a vector icon into pixels: the interface of the icon renderer
	(NewGUI). ngui_svg brings one for SVG (svg::LunaSvgRenderer); an
	application installs it with SetDefault(), like the text system.
*/
class VectorIconRenderer {
public:
	virtual						~VectorIconRenderer() = default;
									/*!< Default. */

	virtual	std::expected<void, Error> Render(std::span<const std::uint8_t> icon,
									BBitmap& result) const = 0;
									/*!< Draws \a icon into all of \a result, a valid
										BColorSpace::RGBA32 bitmap: scaled evenly to fit,
										centered, transparent around it. BadData for an
										icon it cannot read. Thread-safe. */

	static	const VectorIconRenderer* Default();
									/*!< The renderer of the process, nullptr if none was
										set. */
	static	void				SetDefault(const VectorIconRenderer* renderer);
									/*!< Replaces the default; nullptr removes it. The
										object must outlive its use. */
};


/*!	Icons as bitmaps (Haiku's BIconUtils): vector icons drawn at the size of
	a bitmap, and the system's icons by name.

	Differences to Haiku: icons are SVG, not HVIF, drawn by
	VectorIconRenderer::Default(); there are no icon attributes of files, no
	CMAP8 and no archived bitmaps. The system icons are NewGUI's own, built
	into the library.
*/
class BIconUtils {
public:
	static	std::expected<void, Error> GetVectorIcon(std::span<const std::uint8_t> icon,
									BBitmap* result);
									/*!< Draws the vector icon \a icon into all of \a result,
										any color space (drawn in RGBA32 and converted).
										InvalidArgument for a missing or invalid bitmap,
										NotSupported without a renderer, BadData for an
										icon that cannot be read. */
	static	std::expected<void, Error> GetSystemIcon(std::string_view name,
									BBitmap* result);
									/*!< Draws the system icon \a name ("dialog-information",
										"dialog-idea", "dialog-warning", "dialog-error")
										into \a result; NotFound for another name. */
	static	std::span<const std::uint8_t> SystemIconData(std::string_view name);
									/*!< NewGUI: the SVG of the system icon \a name, empty
										for an unknown one. */
};


} // namespace ngui
