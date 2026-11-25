/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/HaikuLook.h>

#include "HaikuControlLook.h"
#include "looks/PortedLook.h"


namespace ngui {


std::shared_ptr<const ControlLook>
CreateHaikuLook()
{
	return std::make_shared<const detail::PortedLook<compat::HaikuControlLook>>(
		LookInfo{"haiku", "Haiku", 1, ConformanceLevel::C2, {}});
}


} // namespace ngui
