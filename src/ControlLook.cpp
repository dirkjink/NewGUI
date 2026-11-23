/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/ControlLook.h>

#include <ngui/Control.h>
#include <ngui/Window.h>


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


ControlStateFlags
ControlLook::Flags(const BControl& control) const
{
	// Ported from Haiku: BeControlLook::Flags() (src/add-ons/control_look/
	// BeControlLook, commit b1d497bb69; HaikuControlLook's is the same).
	ControlStateFlags flags = ControlState::IsControl;

	if (!control.IsEnabled())
		flags |= ControlState::Disabled;

	if (control.IsFocus() && control.Window() != nullptr
		&& control.Window()->IsActive()) {
		flags |= ControlState::Focused;
	}

	switch (control.Value()) {
		case kControlOn:
			flags |= ControlState::Activated;
			break;
		case kControlPartiallyOn:
			flags |= ControlState::PartiallyActivated;
			break;
	}

	if (control.Parent() != nullptr
		&& control.Parent()->Flags().Has(BViewFlag::DrawOnChildren)) {
		// In this constellation, assume we want to render the control
		// against the already existing view contents of the parent view.
		flags |= ControlState::BlendFrame;
	}

	return flags;
}


} // namespace ngui
