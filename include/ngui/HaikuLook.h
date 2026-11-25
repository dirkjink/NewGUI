/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/ControlLook.h>


namespace ngui {


/*!	The Haiku look, id "haiku" (specification 18, 23.2: M13-Haiku): Haiku's
	HaikuControlLook, its default look, ported 1:1. Needs conformance level
	C2 for its linear gradients, which every backend of the core profile
	draws.

	Deviations from Haiku, all documented in the port:
	- No labels on the Tracker desktop (L4).
*/
std::shared_ptr<const ControlLook> CreateHaikuLook();


} // namespace ngui
