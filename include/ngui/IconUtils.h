/*
 * Copyright 2006-2008, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <filesystem>
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
	CMAP8 and no archived bitmaps. The system icons are Haiku's, converted to
	SVG (data/icons/haiku/): those NewGUI shows itself are built into the
	library, the others are read from the icon directory
	(SetSystemIconDirectory()), where an application installs
	data/icons/haiku/.
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
									/*!< Draws the system icon \a name, the name of Haiku's
										icon file without ".svg" ("Alert_Warning",
										"Folder_generic", "flags/DE Germany"), into
										\a result; NotFound for an icon neither built in
										nor in the icon directory. Built in: Alert_Info,
										Alert_Idea, Alert_Warning, Alert_Stop. */
	static	std::span<const std::uint8_t> SystemIconData(std::string_view name);
									/*!< NewGUI: the SVG of the system icon \a name, empty
										for an unknown one. An icon from the directory is
										read once and kept for the process; a name with
										"..", ".", an empty part, a backslash or colon is
										none. */
	static	void				SetSystemIconDirectory(
									const std::filesystem::path& directory);
									/*!< NewGUI: the directory of the system icons that are
										not built in, a copy of data/icons/haiku/;
										overrides NGUI_ICON_DIR, an empty path returns to
										it. */
	static	std::filesystem::path SystemIconDirectory();
									/*!< NewGUI: the directory SetSystemIconDirectory()
										set, else the environment variable NGUI_ICON_DIR,
										else none (empty). */
};


} // namespace ngui
