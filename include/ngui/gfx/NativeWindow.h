/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>

#include <ngui/gfx/Size.h>


namespace ngui::gfx {


/*!	A window as a backend sees it (specification 12): its size and pixel
	density for CPU surfaces, and the native handles a GPU surface would
	need (phase G). Filled in by the platform layer. */
struct NativeWindow {
	/*!	The window system the handles belong to. */
	enum class Kind : std::uint8_t {
		None,		/*!< No native window (headless, tests). */
		X11,		/*!< display: Display*, window: Window */
		Wayland,	/*!< display: wl_display*, window: wl_surface* */
		Win32,		/*!< window: HWND */
		Cocoa		/*!< window: NSWindow* */
	};

			Kind				kind = Kind::None;	/*!< The window system. */
			void*				display = nullptr;	/*!< The connection, if there is one. */
			std::uintptr_t		window = 0;			/*!< The window handle. */
			Size				size = {0, 0};		/*!< The content area, logical pixels. */
			float				scale = 1;			/*!< Device pixels per logical pixel. */

	/*!	A function of OpenGL. */
	using GlFunction = void (*)();
	/*!	Looks up a function of OpenGL by name, as SDL_GL_GetProcAddress()
		does. */
	using GlProcAddress = GlFunction (*)(const char* name);

			GlProcAddress		glProcAddress = nullptr;
									/*!< For SurfaceKind::Gpu (specification 13.3): the
										window has an OpenGL context, current on the
										calling thread, whose default framebuffer shows
										the content area; this loads its functions.
										nullptr: no OpenGL. */
};


} // namespace ngui::gfx
