/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <string>


namespace ngui::detail {


/*!	The signature of the application of the process, empty without one;
	from any thread. */
std::string AppSignature();


} // namespace ngui::detail
