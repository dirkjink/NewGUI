/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Drag & drop with other applications on Windows (module P4), through OLE:
// an IDataObject of DragItems for DoDragDrop(), an IDropSource, and an
// IDropTarget that takes the place of SDL's and reports drops with all
// their data. Main thread, which SDL has made OLE's (OleInitialize()).
// Windows only; no Windows header here, as in AccessKitBridge.h.


#include <functional>
#include <vector>

#include <ngui/Messenger.h>
#include <ngui/gfx/Point.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


// Maps a point on the screen in device pixels to the window's content in
// logical pixels.
using ScreenToContent = std::function<gfx::Point(long x, long y)>;

// Replaces the drop target of the window \a hwnd (an HWND) by one that sends
// kDropBegin, kDropPosition, kDropData and kDropComplete to \a target, at
// points of its content in logical pixels (device pixels divided by what
// \a density gives). Returns what RevokeDropTarget() takes, or nullptr if
// OLE refuses.
void*							RegisterDropTarget(void* hwnd, BMessenger target,
									std::function<float()> density);

// Takes the drop target away from \a hwnd again and releases \a handle.
void							RevokeDropTarget(void* hwnd, void* handle);

// Runs a drag of \a items to other applications until it ends (DoDragDrop()),
// with the mouse button down.
void							RunSystemDrag(const std::vector<DragItem>& items);

// For tests: an IDataObject* with \a items in Windows' formats (one
// reference), and the items an IDataObject* holds, by MIME type.
void*							MakeDataObject(const std::vector<DragItem>& items);
std::vector<DragItem>			ItemsOf(void* dataObject);

// For tests: an IDropTarget* as RegisterDropTarget() makes it, not
// registered (one reference).
void*							MakeDropTarget(BMessenger target, ScreenToContent toContent);


} // namespace ngui::platform::sdl3
