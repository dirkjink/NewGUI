/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Drops of other applications on X11 (module P4), through XDND: SDL takes
// only files and text there. NewGUI handles the XDND messages of its windows
// itself (SDL's X11 event hook; SDL does not see them), asks the source for
// every type it offers that has a MIME type, and reports the drop as SDL
// does: begin and positions as SDL's drop events, the data as a
// DropPayload. Xlib is loaded with dlopen() when it is needed. Main thread
// only, X11 driver only.


#include <cstdint>

#include <SDL3/SDL.h>


namespace ngui::platform::sdl3 {


// Lets \a window (an X11 window of SDL) take drops with all their data; the
// data comes in an SDL event of \a eventType (DropPayload).
void							AcceptX11Drops(SDL_Window* window, std::uint32_t eventType);

// Takes \a window out again, before it is destroyed.
void							ForgetX11Drops(SDL_Window* window);


} // namespace ngui::platform::sdl3
