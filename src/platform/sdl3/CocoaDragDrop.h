/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Drag & drop with other applications on macOS (module P4), through
// NSPasteboard: SDL's Cocoa windows take drops of all their data, and drags
// leave through an NSDraggingSession. Objective-C++ inside (CocoaDragDrop.mm),
// plain C++ outside; main thread only, Apple only.


#include <cstdint>
#include <vector>

#include <SDL3/SDL.h>

#include <ngui/Messenger.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform::sdl3 {


// Lets \a window (an SDL Cocoa window) take drops of text, files, images and
// other types: they come as a DropPayload in an SDL event of \a eventType,
// in order with SDL's own drop events. SDL's handling of the drop itself
// (only files and text) is replaced for all its windows.
void							AcceptCocoaDrops(SDL_Window* window, std::uint32_t eventType);

// Takes \a window out again, before it is destroyed.
void							ForgetCocoaDrops(SDL_Window* window);

// Starts a drag of \a items from \a window's content view with the mouse
// button down; when it ends, \a target gets kMouseUp (AppKit keeps the
// release). False if AppKit refuses.
bool							StartCocoaDrag(SDL_Window* window,
									const std::vector<DragItem>& items, BMessenger target);

// For tests: \a items written to an NSPasteboard* as other programs read
// them, and the items an NSPasteboard* holds, by MIME type.
void							WriteDragItems(const std::vector<DragItem>& items,
									void* pasteboard);
std::vector<DragItem>			ReadDragItems(void* pasteboard);


} // namespace ngui::platform::sdl3
