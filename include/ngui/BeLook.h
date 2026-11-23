/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/ControlLook.h>


namespace ngui {


/*!	The BeOS look, id "be" (specification 18, 23.2: M13-Be): Haiku's
	BeControlLook, ported 1:1. Conformance level C1, so it runs on every
	backend and is the fallback look (L9).

	Deviations from Haiku, all documented in the port:
	- The pressed button is darkened with half transparency instead of
	  inverted (specification 16).
	- No labels on the Tracker desktop (L4).
*/
std::shared_ptr<const ControlLook> CreateBeLook();


} // namespace ngui
