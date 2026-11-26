/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// A gallery of controls drawn by a look, for the snapshot tests of the
// looks.


#include <ngui/ControlLook.h>
#include <ngui/Palette.h>
#include <ngui/gfx/Canvas.h>

#include "support/FixedTextSystem.h"


namespace ngui::test {


// Controls of \a look without labels on a panel of 240 × 136: a
// row of buttons (normal, default, pressed, disabled), check boxes and
// radio buttons (off, on, partially on), a slider and a scroll bar, a menu
// field, a text control and a group frame.
inline gfx::DisplayListRef
LookGallery(const ControlLook& look)
{
	const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);
	const FixedTextSystem text;
	const gfx::Caps caps;
	gfx::Canvas canvas({0, 0, 240, 136});
	DrawContext context{canvas, caps, Palette::Default(), text, 1};
	canvas.FillRect({0, 0, 240, 136}, {.color = kPanel});

	const auto button = [&](gfx::Rect rect, ControlStateFlags flags) {
		look.DrawButtonFrame(context, rect, kPanel, kPanel, flags);
		look.DrawButtonBackground(context, rect, kPanel, flags);
	};
	button({8, 8, 58, 32}, {});
	button({64, 5, 120, 35}, ControlState::DefaultButton);
	button({126, 8, 176, 32}, ControlState::Activated);
	button({182, 8, 232, 32}, ControlState::Disabled);

	const ControlStateFlags marks[] = {{}, ControlState::Activated,
		ControlState::PartiallyActivated};
	float x = 8;
	for (const ControlStateFlags flags : marks) {
		gfx::Rect box{x, 42, x + 14, 56};
		look.DrawCheckBox(context, box, kPanel, flags);
		gfx::Rect radio{x + 20, 42, x + 34, 56};
		look.DrawRadioButton(context, radio, kPanel, flags);
		x += 44;
	}
	gfx::Rect focused{140, 42, 154, 56};
	look.DrawCheckBox(context, focused, kPanel, ControlState::Activated
		| ControlState::Focused);

	gfx::Rect bar{8, 70, 120, 76};
	look.DrawSliderBar(context, bar, kPanel, Palette::Default().Get(
		BColorWhich::ControlHighlight), look.SliderBarColor(Palette::Default(), kPanel),
		0.4f, {}, BOrientation::Horizontal);
	gfx::Rect thumb{48, 64, 64, 82};
	look.DrawSliderThumb(context, thumb, kPanel, {}, BOrientation::Horizontal);

	gfx::Rect scroll{130, 66, 232, 80};
	gfx::Rect scrollBorder = scroll;
	look.DrawScrollBarBorder(context, scrollBorder, kPanel, {}, BOrientation::Horizontal);
	gfx::Rect track{144, 67, 218, 79};
	look.DrawScrollBarBackground(context, track, kPanel, {}, BOrientation::Horizontal);
	gfx::Rect knob{160, 67, 190, 79};
	look.DrawScrollBarThumb(context, knob, kPanel, {}, BOrientation::Horizontal,
		KnobStyle::Lines);
	look.DrawScrollBarButton(context, {131, 67, 144, 79}, kPanel,
		Palette::Default().Get(BColorWhich::ControlText), {}, ArrowDirection::Left,
		BOrientation::Horizontal, false);
	look.DrawScrollBarButton(context, {218, 67, 231, 79}, kPanel,
		Palette::Default().Get(BColorWhich::ControlText), {}, ArrowDirection::Right,
		BOrientation::Horizontal, false);

	gfx::Rect menuField{8, 92, 110, 114};
	look.DrawMenuFieldFrame(context, menuField, kPanel, kPanel);
	look.DrawMenuFieldBackground(context, menuField, kPanel, true);

	gfx::Rect textControl{118, 92, 232, 114};
	look.DrawTextControlBorder(context, textControl, kPanel);

	gfx::Rect group{4, 120, 236, 132};
	look.DrawGroupFrame(context, group, kPanel);
	return canvas.Finish();
}


} // namespace ngui::test
