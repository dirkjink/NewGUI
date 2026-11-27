/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// SDL's hit test for the frames NewGUI draws (BWindowFrames::Look): what the
// window system does with a press on a point of the window, from the frame's
// layout. A window system that places windows itself (Wayland) moves and
// resizes them only this way.


#include <SDL3/SDL.h>

#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


/*!	What a press at \a where (window coordinates) does in a window with
	\a layout: the first area containing it, Normal outside them. */
inline SDL_HitTestResult
HitOf(const WindowFrameLayout& layout, gfx::Point where)
{
	for (const WindowFrameArea& area : layout.areas) {
		if (!area.rect.Contains(where))
			continue;
		switch (area.hit) {
			case WindowFrameHit::Normal:
				return SDL_HITTEST_NORMAL;
			case WindowFrameHit::Move:
				return SDL_HITTEST_DRAGGABLE;
			case WindowFrameHit::ResizeLeft:
				return SDL_HITTEST_RESIZE_LEFT;
			case WindowFrameHit::ResizeRight:
				return SDL_HITTEST_RESIZE_RIGHT;
			case WindowFrameHit::ResizeTop:
				return SDL_HITTEST_RESIZE_TOP;
			case WindowFrameHit::ResizeBottom:
				return SDL_HITTEST_RESIZE_BOTTOM;
			case WindowFrameHit::ResizeTopLeft:
				return SDL_HITTEST_RESIZE_TOPLEFT;
			case WindowFrameHit::ResizeTopRight:
				return SDL_HITTEST_RESIZE_TOPRIGHT;
			case WindowFrameHit::ResizeBottomLeft:
				return SDL_HITTEST_RESIZE_BOTTOMLEFT;
			case WindowFrameHit::ResizeBottomRight:
				return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
		}
	}
	return SDL_HITTEST_NORMAL;
}


} // namespace ngui::platform::sdl3
