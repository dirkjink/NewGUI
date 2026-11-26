/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <memory>

#include <ngui/ControlLook.h>


namespace ngui {


/*!	The flat look, id "flat" (specification 18, 23.2: M13-Flat): Haiku's
	FlatControlLook, an add-on there, ported 1:1. It derives from the Haiku
	look (CreateHaikuLook()) and draws flatter frames, buttons, scroll bars,
	sliders and tabs. Needs conformance level C2 for its linear gradients.

	Deviations from Haiku, all documented in the port:
	- Built into the toolkit, not loaded as an add-on.
	- No parent view: a flat button takes its base color instead of the
	  parent's low color, as in the Haiku look.
	- A slider's bar is filled left of the thumb with the left fill color,
	  as in the Haiku look (Haiku's flat look swaps the two colors).
*/
std::shared_ptr<const ControlLook> CreateFlatLook();


} // namespace ngui
