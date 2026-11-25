/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// Every drawing method of a ControlLook and the states that change the
// drawing, for the tests of the looks.


#include <functional>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/Palette.h>


namespace ngui::test {


// A drawing method by name, called on a DrawContext and a rectangle.
struct LookCall {
	const char*	name;
	std::function<void(DrawContext&, gfx::Rect&, ControlStateFlags)> draw;
};


// Every drawing method of \a look, the overloads of one method in one call,
// on a rectangle of about 100 x 40. \a look must outlive the calls.
inline std::vector<LookCall>
EveryLookCall(const ControlLook& look)
{
	const gfx::Color kPanel = Palette::Default().Get(BColorWhich::PanelBackground);
	const gfx::Color text = Palette::Default().Get(BColorWhich::ControlText);
	return {
		{"DrawButtonFrame", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawButtonFrame(c, r, kPanel, kPanel, f);
			look.DrawButtonFrame(c, r, 3, kPanel, kPanel, f);
			look.DrawButtonFrame(c, r, 1, 2, 3, 4, kPanel, kPanel, f); }},
		{"DrawButtonBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawButtonBackground(c, r, kPanel, f);
			look.DrawButtonBackground(c, r, 3, kPanel, f, kAllBorders, BOrientation::Vertical);
			look.DrawButtonBackground(c, r, 1, 2, 3, 4, kPanel, f); }},
		{"DrawMenuBarBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawMenuBarBackground(c, r, kPanel, f); }},
		{"DrawMenuFieldFrame", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawMenuFieldFrame(c, r, kPanel, kPanel, f);
			look.DrawMenuFieldFrame(c, r, 3, kPanel, kPanel, f);
			look.DrawMenuFieldFrame(c, r, 1, 2, 3, 4, kPanel, kPanel, f); }},
		{"DrawMenuFieldBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawMenuFieldBackground(c, r, kPanel, true, f);
			look.DrawMenuFieldBackground(c, r, 3, kPanel, false, f);
			look.DrawMenuFieldBackground(c, r, 1, 2, 3, 4, kPanel, true, f);
			look.DrawMenuFieldBackground(c, r, kPanel, f); }},
		{"DrawMenuBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawMenuBackground(c, r, kPanel, f); }},
		{"DrawMenuItemBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawMenuItemBackground(c, r, kPanel, f); }},
		{"DrawStatusBar", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags) {
			look.DrawStatusBar(c, r, kPanel, text, 40); }},
		{"DrawCheckBox", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawCheckBox(c, r, kPanel, f); }},
		{"DrawRadioButton", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawRadioButton(c, r, kPanel, f); }},
		{"DrawScrollBarBackground", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			gfx::Rect second{r.x0, r.y0 + 10, r.x1, r.y1};
			look.DrawScrollBarBackground(c, r, second, kPanel, f, BOrientation::Vertical);
			look.DrawScrollBarBackground(c, r, kPanel, f, BOrientation::Horizontal); }},
		{"DrawScrollViewFrame", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawScrollViewFrame(c, r, {86, 0, 100, 26}, {0, 26, 86, 40}, kPanel,
				BBorderStyle::Fancy, f);
			look.DrawScrollViewFrame(c, r, {}, {}, kPanel, BBorderStyle::Plain, f); }},
		{"DrawArrowShape", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (ArrowDirection d : {ArrowDirection::Left, ArrowDirection::Right,
					ArrowDirection::Up, ArrowDirection::Down, ArrowDirection::LeftUp,
					ArrowDirection::RightUp, ArrowDirection::RightDown,
					ArrowDirection::LeftDown}) {
				gfx::Rect arrow = r;
				look.DrawArrowShape(c, arrow, kPanel, d, f);
			} }},
		{"DrawSliderBar", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawSliderBar(c, r, kPanel, text, kPanel, 0.5f, f, BOrientation::Horizontal);
			look.DrawSliderBar(c, r, kPanel, text, f, BOrientation::Vertical); }},
		{"DrawSliderThumb", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawSliderThumb(c, r, kPanel, f, BOrientation::Horizontal); }},
		{"DrawSliderTriangle", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			gfx::Rect other = r;
			look.DrawSliderTriangle(c, r, kPanel, f, BOrientation::Horizontal);
			look.DrawSliderTriangle(c, other, kPanel, text, f, BOrientation::Vertical); }},
		{"DrawSliderHashMarks", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawSliderHashMarks(c, r, kPanel, 5, BHashMarkLocation::Both, f,
				BOrientation::Horizontal); }},
		{"DrawTabs", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (Border side : {Border::Top, Border::Bottom, Border::Left, Border::Right}) {
				gfx::Rect frame = r;
				gfx::Rect active = r;
				gfx::Rect inactive = r;
				look.DrawTabFrame(c, frame, kPanel, f, kAllBorders, BBorderStyle::Fancy,
					side);
				look.DrawActiveTab(c, active, kPanel, f, kAllBorders, side, 1, 1, 0, 2);
				look.DrawInactiveTab(c, inactive, kPanel, f, kAllBorders, side, 0, 1, 0,
					2);
			} }},
		{"DrawSplitter", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawSplitter(c, r, kPanel, BOrientation::Vertical, f); }},
		{"DrawBorder", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawBorder(c, r, kPanel, BBorderStyle::Fancy, f);
			look.DrawBorder(c, r, kPanel, BBorderStyle::Plain, f); }},
		{"DrawRaisedBorder", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawRaisedBorder(c, r, kPanel, f); }},
		{"DrawGroupFrame", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags) {
			look.DrawGroupFrame(c, r, kPanel); }},
		{"DrawTextControlBorder", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawTextControlBorder(c, r, kPanel, f); }},
		{"DrawLabel", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawLabel(c, "Label", r, kPanel, f);
			look.DrawLabel(c, "Label", kPanel, f, {2, 15});
			look.DrawLabel(c, "Label", 0, r, kPanel, f); }},
		{"DrawButtonWithPopUpBackground",
			[=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawButtonWithPopUpBackground(c, r, kPanel, f);
			look.DrawButtonWithPopUpBackground(c, r, 3, kPanel, f);
			look.DrawButtonWithPopUpBackground(c, r, 1, 2, 3, 4, kPanel, f); }},
		{"DrawScrollBarButton", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawScrollBarButton(c, r, kPanel, text, f, ArrowDirection::Up,
				BOrientation::Vertical, true); }},
		{"DrawScrollBarThumb", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			for (KnobStyle knob : {KnobStyle::None, KnobStyle::Dots, KnobStyle::Lines}) {
				gfx::Rect thumb = r;
				look.DrawScrollBarThumb(c, thumb, kPanel, f, BOrientation::Vertical, knob);
				thumb = r;
				look.DrawScrollBarThumb(c, thumb, kPanel, f, BOrientation::Horizontal,
					knob);
			} }},
		{"DrawScrollBarBorder", [=, &look](DrawContext& c, gfx::Rect& r, ControlStateFlags f) {
			look.DrawScrollBarBorder(c, r, kPanel, f, BOrientation::Horizontal); }},
	};
}


// No flags and every state that changes the drawing: disabled, activated,
// focused, default button, hover, partially activated, and a mix.
inline std::vector<ControlStateFlags>
LookStates()
{
	return {
		{},
		ControlState::Disabled,
		ControlState::Activated,
		ControlState::Focused | ControlState::IsControl,
		ControlState::DefaultButton,
		ControlState::Hover | ControlState::Flat,
		ControlState::PartiallyActivated,
		ControlState::Disabled | ControlState::Activated | ControlState::DefaultButton
	};
}


} // namespace ngui::test
