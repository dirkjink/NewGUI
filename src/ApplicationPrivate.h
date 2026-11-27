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

/*!	Makes the colors of BApplication::ColorScheme() the current palette: with
	System, the light or dark colors of the platform's choice. From any
	thread. */
void ApplyColorScheme();


} // namespace ngui::detail
