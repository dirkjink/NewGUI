/*
 * Copyright 2006-2009, Haiku, Inc.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <span>

#include <ngui/Error.h>
#include <ngui/gfx/Point.h>
#include <ngui/platform/Platform.h>


namespace ngui {


class BBitmap;


/*!	The system's mouse cursors (Haiku's BCursorID); the values are Haiku's.
	The platform shows the nearest cursor it has: SDL has no context menu,
	copy, link, help or zoom cursor and shows the arrow for them, the
	pointing hand for FollowLink and the move cursor for Grab and Grabbing. */
enum class BCursorID : std::uint8_t {
	SystemDefault = 1,				/*!< B_CURSOR_ID_SYSTEM_DEFAULT: the arrow. */
	IBeam = 2,						/*!< B_CURSOR_ID_I_BEAM: over text. */
	ContextMenu = 3,				/*!< B_CURSOR_ID_CONTEXT_MENU */
	Copy = 4,						/*!< B_CURSOR_ID_COPY */
	CrossHair = 5,					/*!< B_CURSOR_ID_CROSS_HAIR */
	FollowLink = 6,					/*!< B_CURSOR_ID_FOLLOW_LINK: the pointing hand. */
	Grab = 7,						/*!< B_CURSOR_ID_GRAB */
	Grabbing = 8,					/*!< B_CURSOR_ID_GRABBING */
	Help = 9,						/*!< B_CURSOR_ID_HELP */
	IBeamHorizontal = 10,			/*!< B_CURSOR_ID_I_BEAM_HORIZONTAL */
	Move = 11,						/*!< B_CURSOR_ID_MOVE */
	NoCursor = 12,					/*!< B_CURSOR_ID_NO_CURSOR: none is shown. */
	NotAllowed = 13,				/*!< B_CURSOR_ID_NOT_ALLOWED */
	Progress = 14,					/*!< B_CURSOR_ID_PROGRESS */
	ResizeNorth = 15,				/*!< B_CURSOR_ID_RESIZE_NORTH */
	ResizeEast = 16,				/*!< B_CURSOR_ID_RESIZE_EAST */
	ResizeSouth = 17,				/*!< B_CURSOR_ID_RESIZE_SOUTH */
	ResizeWest = 18,				/*!< B_CURSOR_ID_RESIZE_WEST */
	ResizeNorthEast = 19,			/*!< B_CURSOR_ID_RESIZE_NORTH_EAST */
	ResizeNorthWest = 20,			/*!< B_CURSOR_ID_RESIZE_NORTH_WEST */
	ResizeSouthEast = 21,			/*!< B_CURSOR_ID_RESIZE_SOUTH_EAST */
	ResizeSouthWest = 22,			/*!< B_CURSOR_ID_RESIZE_SOUTH_WEST */
	ResizeNorthSouth = 23,			/*!< B_CURSOR_ID_RESIZE_NORTH_SOUTH */
	ResizeEastWest = 24,			/*!< B_CURSOR_ID_RESIZE_EAST_WEST */
	ResizeNorthEastSouthWest = 25,	/*!< B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST */
	ResizeNorthWestSouthEast = 26,	/*!< B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST */
	ZoomIn = 27,					/*!< B_CURSOR_ID_ZOOM_IN */
	ZoomOut = 28,					/*!< B_CURSOR_ID_ZOOM_OUT */
	CreateLink = 29					/*!< B_CURSOR_ID_CREATE_LINK */
};


/*!	A mouse cursor (Haiku's BCursor): one of the system's, or an image of
	its own with a hot spot. BView::SetViewCursor() shows it over a view,
	BApplication::SetCursor() everywhere else.

	Differences to Haiku: no archiving; a cursor is a value without a server
	token (copies share the image); an image cursor is made from a
	BBitmap's pixels at the time (Haiku: B_RGBA32 or B_CMAP8 only, here any
	color space). The R5 cursor data (68 bytes) is taken as a span.
*/
class BCursor {
public:
								BCursor(BCursorID id);
									/*!< A system cursor. */
								BCursor(const BBitmap& bitmap, gfx::Point hotspot);
									/*!< A cursor of the pixels of \a bitmap, its hot spot
										at \a hotspot (in the bitmap's pixels, clamped to
										it); InitCheck() fails for an invalid or empty
										bitmap. */
	explicit					BCursor(std::span<const std::uint8_t> cursorData);
									/*!< A cursor of R5's format: 16 × 16 pixels at 1 bit,
										size 16, depth 1, the hot spot's y and x, 32 bytes
										of colors (1 for black), 32 bytes of mask (1 for
										shown). InitCheck() fails for other data. */

			std::expected<void, Error> InitCheck() const;
									/*!< Whether the cursor was made; InvalidArgument
										otherwise. */
			bool				operator==(const BCursor& other) const;
									/*!< The same system cursor, or the same image. */

			const platform::CursorShape& Shape() const;
									/*!< NewGUI: what the platform shows. */

private:
			platform::CursorShape fShape;
			bool				fValid = true;
};


} // namespace ngui
