/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The adapter of a ported look (specification 18.3, L7): every method makes
// a CompatView over the DrawContext and calls the port of a Haiku control
// look (BeControlLook, HaikuControlLook) with Haiku's types. Rectangles go
// in as inclusive BRect and come back half-open (L6), flags as Haiku's
// bits, colors as rgb_color. The ported looks share Haiku's BControlLook
// interface, so one template serves them all.


#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include <ngui/ControlLook.h>
#include <ngui/Palette.h>

#include "compat/ControlLook.h"
#include "compat/CompatView.h"


namespace ngui::detail {


using compat::BRect;
using compat::CompatView;
using compat::rgb_color;



// Everything is drawn: a View records its whole DisplayList (18.3).
inline const BRect kUpdateAll(std::numeric_limits<float>::lowest(),
	std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
	std::numeric_limits<float>::max());


inline BRect
ToBRect(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


inline rgb_color
ToRgb(const gfx::Color& color)
{
	return compat::ToRgbColor(color);
}


inline uint32
ToHaiku(ControlStateFlags flags)
{
	struct Bit {
		ControlState	state;
		uint32			haiku;
	};
	static constexpr Bit kBits[] = {
		{ControlState::Focused, compat::BControlLook::B_FOCUSED},
		{ControlState::Clicked, compat::BControlLook::B_CLICKED},
		{ControlState::Activated, compat::BControlLook::B_ACTIVATED},
		{ControlState::Hover, compat::BControlLook::B_HOVER},
		{ControlState::Disabled, compat::BControlLook::B_DISABLED},
		{ControlState::DefaultButton, compat::BControlLook::B_DEFAULT_BUTTON},
		{ControlState::IgnoreOutline, compat::BControlLook::B_IGNORE_OUTLINE},
		{ControlState::PartiallyActivated, compat::BControlLook::B_PARTIALLY_ACTIVATED},
		{ControlState::Flat, compat::BControlLook::B_FLAT},
		{ControlState::Invalid, compat::BControlLook::B_INVALID},
		{ControlState::IsControl, compat::BControlLook::B_IS_CONTROL},
		{ControlState::BlendFrame, compat::BControlLook::B_BLEND_FRAME}
	};
	uint32 haiku = 0;
	for (const Bit& bit : kBits) {
		if (flags.Has(bit.state))
			haiku |= bit.haiku;
	}
	return haiku;
}


inline uint32
ToHaiku(BorderFlags borders)
{
	uint32 haiku = 0;
	if (borders.Has(Border::Left))
		haiku |= compat::BControlLook::B_LEFT_BORDER;
	if (borders.Has(Border::Right))
		haiku |= compat::BControlLook::B_RIGHT_BORDER;
	if (borders.Has(Border::Top))
		haiku |= compat::BControlLook::B_TOP_BORDER;
	if (borders.Has(Border::Bottom))
		haiku |= compat::BControlLook::B_BOTTOM_BORDER;
	return haiku;
}


// A tab side is one border (BTabView::tab_side has the same bits).
inline uint32
ToHaiku(Border side)
{
	return ToHaiku(BorderFlags(side));
}


inline uint32
ToHaiku(ArrowDirection direction)
{
	switch (direction) {
		case ArrowDirection::Left:
			return compat::BControlLook::B_LEFT_ARROW;
		case ArrowDirection::Right:
			return compat::BControlLook::B_RIGHT_ARROW;
		case ArrowDirection::Up:
			return compat::BControlLook::B_UP_ARROW;
		case ArrowDirection::Down:
			return compat::BControlLook::B_DOWN_ARROW;
		case ArrowDirection::LeftUp:
			return compat::BControlLook::B_LEFT_UP_ARROW;
		case ArrowDirection::RightUp:
			return compat::BControlLook::B_RIGHT_UP_ARROW;
		case ArrowDirection::RightDown:
			return compat::BControlLook::B_RIGHT_DOWN_ARROW;
		case ArrowDirection::LeftDown:
			return compat::BControlLook::B_LEFT_DOWN_ARROW;
	}
	return compat::BControlLook::B_LEFT_ARROW;
}


inline uint32
ToHaiku(KnobStyle knobStyle)
{
	switch (knobStyle) {
		case KnobStyle::None:
			return compat::BControlLook::B_KNOB_NONE;
		case KnobStyle::Dots:
			return compat::BControlLook::B_KNOB_DOTS;
		case KnobStyle::Lines:
			return compat::BControlLook::B_KNOB_LINES;
	}
	return compat::BControlLook::B_KNOB_NONE;
}


inline compat::hash_mark_location
ToHaiku(BHashMarkLocation location)
{
	switch (location) {
		case BHashMarkLocation::None:
			return compat::B_HASH_MARKS_NONE;
		case BHashMarkLocation::TopOrLeft:
			return compat::B_HASH_MARKS_TOP;
		case BHashMarkLocation::BottomOrRight:
			return compat::B_HASH_MARKS_BOTTOM;
		case BHashMarkLocation::Both:
			return compat::B_HASH_MARKS_BOTH;
	}
	return compat::B_HASH_MARKS_NONE;
}


inline compat::BControlLook::frame_type
ToHaiku(FrameType frameType)
{
	switch (frameType) {
		case FrameType::Button:
			return compat::BControlLook::B_BUTTON_FRAME;
		case FrameType::Group:
			return compat::BControlLook::B_GROUP_FRAME;
		case FrameType::MenuField:
			return compat::BControlLook::B_MENU_FIELD_FRAME;
		case FrameType::ScrollView:
			return compat::BControlLook::B_SCROLL_VIEW_FRAME;
		case FrameType::TextControl:
			return compat::BControlLook::B_TEXT_CONTROL_FRAME;
	}
	return compat::BControlLook::B_BUTTON_FRAME;
}


inline compat::BControlLook::background_type
ToHaiku(BackgroundType backgroundType)
{
	switch (backgroundType) {
		case BackgroundType::Button:
			return compat::BControlLook::B_BUTTON_BACKGROUND;
		case BackgroundType::ButtonWithPopUp:
			return compat::BControlLook::B_BUTTON_WITH_POP_UP_BACKGROUND;
		case BackgroundType::Menu:
			return compat::BControlLook::B_MENU_BACKGROUND;
		case BackgroundType::MenuBar:
			return compat::BControlLook::B_MENU_BAR_BACKGROUND;
		case BackgroundType::MenuField:
			return compat::BControlLook::B_MENU_FIELD_BACKGROUND;
		case BackgroundType::MenuItem:
			return compat::BControlLook::B_MENU_ITEM_BACKGROUND;
		case BackgroundType::HorizontalScrollBar:
			return compat::BControlLook::B_HORIZONTAL_SCROLL_BAR_BACKGROUND;
		case BackgroundType::VerticalScrollBar:
			return compat::BControlLook::B_VERTICAL_SCROLL_BAR_BACKGROUND;
	}
	return compat::BControlLook::B_BUTTON_BACKGROUND;
}


// For the methods of the ported look that use no font: a text system
// without metrics. They use no palette either; they get the default one.
inline const TextSystem&
NoText()
{
	return compat::BFont().Text();
}


// A label as Haiku's look expects it: NULL for none.
struct Label {
	explicit Label(std::string_view label)
		:
		text(label)
	{
	}

	const char* Get() const
	{
		return text.empty() ? NULL : text.c_str();
	}

	std::string text;
};


struct TextColor {
	explicit TextColor(const gfx::Color* color)
	{
		if (color != nullptr)
			value = ToRgb(*color);
	}

	const rgb_color* Get() const
	{
		return value.has_value() ? &*value : NULL;
	}

	std::optional<rgb_color> value;
};


// The ported look and a CompatView over \a context, for one call.
template<typename Look>
struct Port {
	explicit Port(DrawContext& context)
		:
		look(context.text, context.palette),
		view(context)
	{
	}

	Look			look;
	CompatView		view;
};


// Calls \a draw with the ported look, its view and \a rect as BRect, and
// writes the rectangle back half-open (L6).
template<typename Look, typename Draw>
void
WithRect(DrawContext& context, gfx::Rect& rect, Draw draw)
{
	BRect haikuRect = ToBRect(rect);
	{
		Port<Look> port(context);
		draw(port.look, &port.view, haikuRect);
	}
	rect = compat::FromBRect(haikuRect);
}


template<typename Look>
class PortedLook final : public ControlLook {
public:
	explicit PortedLook(LookInfo info)
		:
		fInfo(info)
	{
	}

	LookInfo Info() const override
	{
		return fInfo;
	}

	BAlignment DefaultLabelAlignment() const override
	{
		return Look(NoText(), Palette::Default()).DefaultLabelAlignment();
	}

	float DefaultLabelSpacing(const TextSystem& text) const override
	{
		return Look(text, Palette::Default()).DefaultLabelSpacing();
	}

	float DefaultItemSpacing(const TextSystem& text) const override
	{
		return Look(text, Palette::Default()).DefaultItemSpacing();
	}

	void DrawButtonFrame(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonFrame(view, r, kUpdateAll, ToRgb(base), ToRgb(background),
				ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawButtonFrame(DrawContext& context, gfx::Rect& rect, float radius,
		const gfx::Color& base, const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonFrame(view, r, kUpdateAll, radius, ToRgb(base),
				ToRgb(background), ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawButtonFrame(DrawContext& context, gfx::Rect& rect, float leftTopRadius,
		float rightTopRadius, float leftBottomRadius, float rightBottomRadius,
		const gfx::Color& base, const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonFrame(view, r, kUpdateAll, leftTopRadius, rightTopRadius,
				leftBottomRadius, rightBottomRadius, ToRgb(base), ToRgb(background),
				ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawButtonBackground(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders), orientation);
		});
	}

	void DrawButtonBackground(DrawContext& context, gfx::Rect& rect, float radius,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonBackground(view, r, kUpdateAll, radius, ToRgb(base),
				ToHaiku(flags), ToHaiku(borders), orientation);
		});
	}

	void DrawButtonBackground(DrawContext& context, gfx::Rect& rect, float leftTopRadius,
		float rightTopRadius, float leftBottomRadius, float rightBottomRadius,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonBackground(view, r, kUpdateAll, leftTopRadius, rightTopRadius,
				leftBottomRadius, rightBottomRadius, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders), orientation);
		});
	}

	void DrawMenuBarBackground(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuBarBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldFrame(view, r, kUpdateAll, ToRgb(base), ToRgb(background),
				ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect, float radius,
		const gfx::Color& base, const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldFrame(view, r, kUpdateAll, radius, ToRgb(base),
				ToRgb(background), ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect, float leftTopRadius,
		float rightTopRadius, float leftBottomRadius, float rightBottomRadius,
		const gfx::Color& base, const gfx::Color& background, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldFrame(view, r, kUpdateAll, leftTopRadius, rightTopRadius,
				leftBottomRadius, rightBottomRadius, ToRgb(base), ToRgb(background),
				ToHaiku(flags), ToHaiku(borders));
		});
	}

	void DrawMenuFieldBackground(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, bool popupIndicator, ControlStateFlags flags) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldBackground(view, r, kUpdateAll, ToRgb(base), popupIndicator,
				ToHaiku(flags));
		});
	}

	void DrawMenuFieldBackground(DrawContext& context, gfx::Rect& rect, float radius,
		const gfx::Color& base, bool popupIndicator, ControlStateFlags flags) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldBackground(view, r, kUpdateAll, radius, ToRgb(base),
				popupIndicator, ToHaiku(flags));
		});
	}

	void DrawMenuFieldBackground(DrawContext& context, gfx::Rect& rect, float leftTopRadius,
		float rightTopRadius, float leftBottomRadius, float rightBottomRadius,
		const gfx::Color& base, bool popupIndicator, ControlStateFlags flags) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldBackground(view, r, kUpdateAll, leftTopRadius,
				rightTopRadius, leftBottomRadius, rightBottomRadius, ToRgb(base),
				popupIndicator, ToHaiku(flags));
		});
	}

	void DrawMenuFieldBackground(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuFieldBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawMenuBackground(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawMenuItemBackground(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawMenuItemBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawStatusBar(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		const gfx::Color& barColor, float progressPosition) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawStatusBar(view, r, kUpdateAll, ToRgb(base), ToRgb(barColor),
				progressPosition);
		});
	}

	void DrawCheckBox(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawCheckBox(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags));
		});
	}

	void DrawRadioButton(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawRadioButton(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags));
		});
	}

	void DrawScrollBarBackground(DrawContext& context, gfx::Rect& rect1, gfx::Rect& rect2,
		const gfx::Color& base, ControlStateFlags flags,
		BOrientation orientation) const override
	{
		BRect haikuRect1 = ToBRect(rect1);
		BRect haikuRect2 = ToBRect(rect2);
		{
			Port<Look> port(context);
			port.look.DrawScrollBarBackground(&port.view, haikuRect1, haikuRect2, kUpdateAll,
				ToRgb(base), ToHaiku(flags), orientation);
		}
		rect1 = compat::FromBRect(haikuRect1);
		rect2 = compat::FromBRect(haikuRect2);
	}

	void DrawScrollBarBackground(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, ControlStateFlags flags,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawScrollBarBackground(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				orientation);
		});
	}

	void DrawScrollViewFrame(DrawContext& context, gfx::Rect& rect,
		gfx::Rect verticalScrollBarFrame, gfx::Rect horizontalScrollBarFrame,
		const gfx::Color& base, BBorderStyle borderStyle, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawScrollViewFrame(view, r, kUpdateAll, ToBRect(verticalScrollBarFrame),
				ToBRect(horizontalScrollBarFrame), ToRgb(base), borderStyle, ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawArrowShape(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ArrowDirection direction, ControlStateFlags flags, float tint) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawArrowShape(view, r, kUpdateAll, ToRgb(base), ToHaiku(direction),
				ToHaiku(flags), tint);
		});
	}

	gfx::Color SliderBarColor(const Palette& palette, const gfx::Color& base) const override
	{
		return compat::ToColor(Look(NoText(), palette).SliderBarColor(ToRgb(base)));
	}

	void DrawSliderBar(DrawContext& context, gfx::Rect rect, const gfx::Color& base,
		gfx::Color leftFillColor, gfx::Color rightFillColor, float sliderScale,
		ControlStateFlags flags, BOrientation orientation) const override
	{
		Port<Look> port(context);
		port.look.DrawSliderBar(&port.view, ToBRect(rect), kUpdateAll, ToRgb(base),
			ToRgb(leftFillColor), ToRgb(rightFillColor), sliderScale, ToHaiku(flags),
			orientation);
	}

	void DrawSliderBar(DrawContext& context, gfx::Rect rect, const gfx::Color& base,
		gfx::Color fillColor, ControlStateFlags flags, BOrientation orientation) const override
	{
		Port<Look> port(context);
		port.look.DrawSliderBar(&port.view, ToBRect(rect), kUpdateAll, ToRgb(base),
			ToRgb(fillColor), ToHaiku(flags), orientation);
	}

	void DrawSliderThumb(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawSliderThumb(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				orientation);
		});
	}

	void DrawSliderTriangle(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawSliderTriangle(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				orientation);
		});
	}

	void DrawSliderTriangle(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		const gfx::Color& fill, ControlStateFlags flags,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawSliderTriangle(view, r, kUpdateAll, ToRgb(base), ToRgb(fill),
				ToHaiku(flags), orientation);
		});
	}

	void DrawSliderHashMarks(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		std::int32_t count, BHashMarkLocation location, ControlStateFlags flags,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawSliderHashMarks(view, r, kUpdateAll, ToRgb(base), count,
				ToHaiku(location), ToHaiku(flags), orientation);
		});
	}

	void DrawActiveTab(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders, Border side, std::int32_t index,
		std::int32_t selected, std::int32_t first, std::int32_t last) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawActiveTab(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders), ToHaiku(side), index, selected, first, last);
		});
	}

	void DrawInactiveTab(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders, Border side, std::int32_t index,
		std::int32_t selected, std::int32_t first, std::int32_t last) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawInactiveTab(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders), ToHaiku(side), index, selected, first, last);
		});
	}

	void DrawSplitter(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		BOrientation orientation, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawSplitter(view, r, kUpdateAll, ToRgb(base), orientation, ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawBorder(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		BBorderStyle borderStyle, ControlStateFlags flags,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawBorder(view, r, kUpdateAll, ToRgb(base), borderStyle, ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawRaisedBorder(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawRaisedBorder(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawGroupFrame(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawGroupFrame(view, r, kUpdateAll, ToRgb(base), ToHaiku(borders));
		});
	}

	void DrawTextControlBorder(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawTextControlBorder(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders));
		});
	}

	void DrawLabel(DrawContext& context, std::string_view label, gfx::Rect rect,
		const gfx::Color& base, ControlStateFlags flags,
		const gfx::Color* textColor) const override
	{
		DrawLabel(context, label, rect, base, flags, DefaultLabelAlignment(), textColor);
	}

	void DrawLabel(DrawContext& context, std::string_view label, gfx::Rect rect,
		const gfx::Color& base, ControlStateFlags flags, const BAlignment& alignment,
		const gfx::Color* textColor) const override
	{
		const Label text(label);
		const TextColor color(textColor);
		Port<Look> port(context);
		port.look.DrawLabel(&port.view, text.Get(), ToBRect(rect), kUpdateAll, ToRgb(base),
			ToHaiku(flags), alignment, color.Get());
	}

	void DrawLabel(DrawContext& context, std::string_view label, const gfx::Color& base,
		ControlStateFlags flags, const gfx::Point& where,
		const gfx::Color* textColor) const override
	{
		const Label text(label);
		const TextColor color(textColor);
		Port<Look> port(context);
		port.look.DrawLabel(&port.view, text.Get(), ToRgb(base), ToHaiku(flags),
			compat::BPoint(where.x, where.y), color.Get());
	}

	using ControlLook::DrawLabel;

	void DrawLabel(DrawContext& context, std::string_view label, gfx::ImageHandle icon,
		gfx::Rect rect, const gfx::Color& base, ControlStateFlags flags,
		const BAlignment& alignment, const gfx::Color* textColor) const override
	{
		const Label text(label);
		const TextColor color(textColor);
		const compat::BBitmap bitmap(icon);
		Port<Look> port(context);
		port.look.DrawLabel(&port.view, text.Get(), icon != 0 ? &bitmap : NULL,
			ToBRect(rect), kUpdateAll, ToRgb(base), ToHaiku(flags), alignment, color.Get());
	}

	Insets FrameInsets(const TextSystem& text, FrameType frameType,
		ControlStateFlags flags) const override
	{
		Insets insets{};
		Look(text, Palette::Default()).GetFrameInsets(ToHaiku(frameType),
			ToHaiku(flags), insets.left, insets.top, insets.right, insets.bottom);
		return insets;
	}

	Insets BackgroundInsets(const TextSystem& text, BackgroundType backgroundType,
		ControlStateFlags flags) const override
	{
		Insets insets{};
		Look(text, Palette::Default()).GetBackgroundInsets(ToHaiku(backgroundType),
			ToHaiku(flags), insets.left, insets.top, insets.right, insets.bottom);
		return insets;
	}

	void DrawButtonWithPopUpBackground(DrawContext& context, gfx::Rect& rect,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonWithPopUpBackground(view, r, kUpdateAll, ToRgb(base),
				ToHaiku(flags), ToHaiku(borders), orientation);
		});
	}

	void DrawButtonWithPopUpBackground(DrawContext& context, gfx::Rect& rect, float radius,
		const gfx::Color& base, ControlStateFlags flags, BorderFlags borders,
		BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonWithPopUpBackground(view, r, kUpdateAll, radius, ToRgb(base),
				ToHaiku(flags), ToHaiku(borders), orientation);
		});
	}

	void DrawButtonWithPopUpBackground(DrawContext& context, gfx::Rect& rect,
		float leftTopRadius, float rightTopRadius, float leftBottomRadius,
		float rightBottomRadius, const gfx::Color& base, ControlStateFlags flags,
		BorderFlags borders, BOrientation orientation) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawButtonWithPopUpBackground(view, r, kUpdateAll, leftTopRadius,
				rightTopRadius, leftBottomRadius, rightBottomRadius, ToRgb(base),
				ToHaiku(flags), ToHaiku(borders), orientation);
		});
	}

	void DrawTabFrame(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BorderFlags borders, BBorderStyle borderStyle,
		Border side) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawTabFrame(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				ToHaiku(borders), borderStyle, ToHaiku(side));
		});
	}

	void DrawScrollBarButton(DrawContext& context, gfx::Rect rect, const gfx::Color& base,
		const gfx::Color& text, ControlStateFlags flags, ArrowDirection direction,
		BOrientation orientation, bool down) const override
	{
		Port<Look> port(context);
		port.look.DrawScrollBarButton(&port.view, ToBRect(rect), kUpdateAll, ToRgb(base),
			ToRgb(text), ToHaiku(flags), static_cast<int32>(ToHaiku(direction)),
			orientation, down);
	}

	void DrawScrollBarThumb(DrawContext& context, gfx::Rect& rect, const gfx::Color& base,
		ControlStateFlags flags, BOrientation orientation,
		KnobStyle knobStyle) const override
	{
		WithRect<Look>(context, rect, [&](Look& look, CompatView* view, BRect& r) {
			look.DrawScrollBarThumb(view, r, kUpdateAll, ToRgb(base), ToHaiku(flags),
				orientation, ToHaiku(knobStyle));
		});
	}

	void DrawScrollBarBorder(DrawContext& context, gfx::Rect rect, const gfx::Color& base,
		ControlStateFlags flags, BOrientation orientation) const override
	{
		Port<Look> port(context);
		port.look.DrawScrollBarBorder(&port.view, ToBRect(rect), kUpdateAll, ToRgb(base),
			ToHaiku(flags), orientation);
	}

	float ScrollBarWidth(const TextSystem& text, BOrientation) const override
	{
		// Haiku's BControlLook::GetScrollBarWidth():
		// max(ComposeSpacing(B_USE_CORNER_SPACING), 14) with
		// ComposeSpacing(B_USE_CORNER_SPACING) = ceilf(DefaultItemSpacing() * 1.272f).
		const float itemSpacing = Look(text, Palette::Default()).DefaultItemSpacing();
		return std::max(std::ceil(itemSpacing * 1.272f), 14.0f);
	}

private:
	LookInfo			fInfo;
};


} // namespace ngui::detail
