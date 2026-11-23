/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ControlLook.h>


namespace ngui {


void
ControlLook::DrawLabel(DrawContext& context, std::string_view label, gfx::ImageHandle icon,
	gfx::Rect rect, const gfx::Color& base, ControlStateFlags flags,
	const gfx::Color* textColor) const
{
	DrawLabel(context, label, icon, rect, base, flags, DefaultLabelAlignment(), textColor);
}


Insets
ControlLook::FrameAndBackgroundInsets(FrameType frameType, BackgroundType backgroundType,
	ControlStateFlags flags) const
{
	return FrameInsets(frameType, flags) + BackgroundInsets(backgroundType, flags);
}


} // namespace ngui
