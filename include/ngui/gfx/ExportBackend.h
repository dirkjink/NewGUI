/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <ostream>

#include <ngui/Error.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Rect.h>


namespace ngui::gfx {


/*!	How an export backend writes a DisplayList (specification 19.2). */
struct ExportOptions {
	/*!	How glyph runs are written. */
	enum class TextMode : std::uint8_t {
		Paths,	/*!< The outlines of the glyphs: looks the same everywhere. */
		Text	/*!< The text with its font: selectable and searchable; falls back
					to Paths where the glyphs do not map 1:1 to characters. */
	};

			TextMode			text = TextMode::Paths;	/*!< How glyph runs are written. */
			bool				embedImages = true;
									/*!< Images as data: URIs; otherwise without their
										pixels. */
			bool				pretty = false;	/*!< Indentation and line breaks. */
			float				scale = 1.0f;
									/*!< Factor for the size of the result; its coordinates
										stay logical. */
};


/*!	Writes a DisplayList to a file format without rasterizing it
	(specification 19.2): the same lists as the render backends, under the
	same rules (R1 to R3), with the whole core profile.
*/
class ExportBackend {
public:
	virtual						~ExportBackend() = default;

	virtual	Caps				Capabilities() const = 0;
									/*!< What it can write; at least the core profile. */
	virtual	std::expected<void, Error> Export(const DisplayList& list, const Rect& bounds,
									std::ostream& out, const ExportOptions& options) = 0;
									/*!< Writes \a list, cut to \a bounds (logical pixels),
										to \a out. InvalidArgument for empty bounds or a scale
										that is not positive, IoError if \a out fails. */
};


} // namespace ngui::gfx
