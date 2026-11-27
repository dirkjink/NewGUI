/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Drag & drop with other applications on X11 (module P4), through XDND: SDL
// takes only files and text there, and drags nothing out. NewGUI handles
// the XDND messages of its windows itself (SDL's X11 event hook; SDL does
// not see them). As the target it asks the source for every type it offers
// that has a MIME type, and reports the drop as SDL does: begin and
// positions as SDL's drop events, the data as a DropPayload. As the source
// it follows the pointer while the button is down (SDL keeps it grabbed)
// and gives the data on request. Xlib is loaded with dlopen() when it is
// needed. Main thread only, X11 driver only.


#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


// Lets \a window (an X11 window of SDL) take drops with all their data; the
// data comes in an SDL event of \a eventType (DropPayload). False if
// it cannot (no Xlib, not an X11 window).
bool							AcceptX11Drops(SDL_Window* window, std::uint32_t eventType);

// Takes \a window out again, before it is destroyed.
void							ForgetX11Drops(SDL_Window* window);

// Whether a compositing manager runs on the display (it owns
// _NET_WM_CM_S<screen>): only then do transparent windows show the desktop
// behind them.
bool							X11HasCompositor();

// Starts a drag of \a items from \a window (one that takes drops) with the
// mouse button down; it ends with the release, which goes to the window
// as usual. False if there is nothing to drag.
bool							StartX11Drag(SDL_Window* window,
									const std::vector<DragItem>& items);


} // namespace ngui::platform::sdl3
