/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The bridge between a window's accessibility tree (ngui/Accessibility.h)
// and AccessKit's adapter of the platform (toolkit scope 11.3): AT-SPI on
// Linux, UI Automation on Windows. Not yet on macOS, which has no build in
// the CI.


#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <SDL3/SDL.h>

#include <ngui/Accessibility.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>

#include "platform/sdl3/AccessKit.h"


namespace ngui::platform::sdl3 {


#if defined(_WIN32) || defined(__linux__)
inline constexpr bool kHasAccessKitAdapter = true;
#else
inline constexpr bool kHasAccessKitAdapter = false;
#endif


// The node of \a node as AccessKit has it; the window's node (the tree's
// first) scales logical to device pixels by \a scale.
accesskit_node* MakeNode(const AccessibleNode& node, bool root, double scale);
// The whole of \a tree as an update; the caller owns it.
accesskit_tree_update* MakeTreeUpdate(const AccessibleTree& tree, double scale);
// A tree of the window alone (node \a id, \a title), before the window
// passed its own on.
accesskit_tree_update* MakePlaceholderUpdate(std::uint64_t id, const std::string& title);
// \a request as kAccessibleAction for the window; nullopt for an action
// NewGUI does not take.
std::optional<BMessage> ActionMessage(const accesskit_action_request& request);


// The adapter of one window, made on the main thread before the window is
// shown first, freed there before the window goes.
class AccessKitBridge {
public:
	// nullptr where there is no adapter.
	static	std::unique_ptr<AccessKitBridge> Create(SDL_Window* window, BMessenger target,
									std::string title);
								~AccessKitBridge();

								AccessKitBridge(const AccessKitBridge&) = delete;
			AccessKitBridge&	operator=(const AccessKitBridge&) = delete;

	// Whether assistive technology listens (any thread).
			bool				IsActive() const;
	// Keeps \a tree for the next Push() (any thread).
			void				SetTree(const AccessibleTree& tree, double scale);
	// Passes the kept tree to the adapter (main thread).
			void				Push();
	// The window got or lost the focus (main thread).
			void				SetFocused(bool focused);
	// The window's frame on the screen in device pixels (main thread; for
	// X11, Wayland keeps it from programs).
			void				SetBounds(double x0, double y0, double x1, double y1);

private:
								AccessKitBridge(BMessenger target, std::string title);

	static	accesskit_tree_update* _Activated(void* userdata);
	static	void				_Action(accesskit_action_request* request, void* userdata);
	static	void				_Deactivated(void* userdata);
	static	accesskit_tree_update* _Update(void* userdata);

			accesskit_tree_update* _CurrentUpdate();

	mutable	std::mutex			fLock;
			BMessenger			fTarget;
			std::string			fTitle;
			std::optional<AccessibleTree> fTree;
			double				fScale = 1;
			std::atomic<bool>	fActive = false;
#if defined(_WIN32)
			accesskit_windows_subclassing_adapter* fAdapter = nullptr;
#elif defined(__linux__)
			accesskit_unix_adapter* fAdapter = nullptr;
#endif
};


} // namespace ngui::platform::sdl3
