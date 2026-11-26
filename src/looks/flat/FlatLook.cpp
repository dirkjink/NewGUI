/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/FlatLook.h>

#include "FlatControlLook.h"
#include "looks/PortedLook.h"


namespace ngui {


std::shared_ptr<const ControlLook>
CreateFlatLook()
{
	return std::make_shared<const detail::PortedLook<compat::FlatControlLook>>(
		LookInfo{"flat", "Flat", 1, ConformanceLevel::C2, {}});
}


} // namespace ngui
