/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// A window's accessibility tree as AccessKit's tree updates, and AccessKit's
// action requests as kAccessibleAction (toolkit scope 11.3). Only for the
// bridge and its tests: AccessKit's header brings windows.h.


#include <cstdint>
#include <optional>
#include <string>

#include <ngui/Accessibility.h>
#include <ngui/Message.h>

#include "platform/sdl3/AccessKit.h"


namespace ngui::platform::sdl3 {


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


} // namespace ngui::platform::sdl3
