/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/BeLook.h>

#include "BeControlLook.h"
#include "looks/PortedLook.h"


namespace ngui {


std::shared_ptr<const ControlLook>
CreateBeLook()
{
	return std::make_shared<const detail::PortedLook<compat::BeControlLook>>(
		LookInfo{"be", "BeOS", 1, ConformanceLevel::C1, {}});
}


} // namespace ngui
