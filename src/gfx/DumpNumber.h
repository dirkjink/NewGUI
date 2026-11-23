/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cmath>
#include <format>
#include <string>


namespace ngui::gfx {


// A number as the dumps write it (specification 15): rounded to three
// decimals, trailing zeros removed, "-0" as "0". std::format is
// locale-independent and rounds exactly on every platform.
inline std::string
DumpNumber(float value)
{
	if (!std::isfinite(value))
		return std::isnan(value) ? "nan" : (value > 0 ? "inf" : "-inf");
	std::string text = std::format("{:.3f}", value);
	while (text.back() == '0')
		text.pop_back();
	if (text.back() == '.')
		text.pop_back();
	return text == "-0" ? "0" : text;
}


} // namespace ngui::gfx
