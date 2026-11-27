/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// A drop with all its data (module P4), as an event of SDL's queue: the
// Cocoa drop target pushes it, so that it comes after the positions SDL
// queued during the drag. data1 of the event; ProcessEvents() deletes it.


#include <vector>

#include <ngui/gfx/Point.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


struct DropPayload {
	std::vector<DragItem>	items;
	gfx::Point				where;	// in the window's content, logical pixels
};


} // namespace ngui::platform::sdl3
