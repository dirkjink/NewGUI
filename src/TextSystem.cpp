/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/TextSystem.h>


namespace ngui {


gfx::GlyphRun
ShapedText::Run() const
{
	return {font, sizePx, glyphs, positions, text, clusters};
}


} // namespace ngui
