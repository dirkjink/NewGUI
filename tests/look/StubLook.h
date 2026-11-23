/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <mutex>
#include <string>
#include <vector>

#include <ngui/ControlLook.h>


namespace ngui::test {


// A look that draws nothing and records which methods were called, in
// order. Every Draw…Frame() method shrinks the rectangle by 1 on each side
// (L6). For tests of the ControlLook interface, of controls and of the look
// registry. All overrides; generated from ngui/ControlLook.h, so that the
// stub breaks the build when the interface changes.
class StubLook : public ControlLook {
public:
	std::vector<std::string> Calls() const
	{
		const std::scoped_lock lock(fLock);
		return fCalls;
	}

	LookInfo Info() const override
	{
		return {"stub", "Stub", 1, ConformanceLevel::C1, {}};
	}

	BAlignment DefaultLabelAlignment() const override
	{
		return {BHorizontalAlignment::Left, BVerticalAlignment::Middle};
	}

	float DefaultLabelSpacing(const TextSystem&) const override
	{
		return 5;
	}

	float DefaultItemSpacing(const TextSystem&) const override
	{
		return 5;
	}

	void DrawButtonFrame(DrawContext&, gfx::Rect& rect, const gfx::Color&, const gfx::Color&,
		ControlStateFlags, BorderFlags) const override
	{
		Record("DrawButtonFrame");
		rect.InsetBy(1, 1);
	}

	void DrawButtonFrame(DrawContext&, gfx::Rect& rect, float, const gfx::Color&, const gfx::Color&,
		ControlStateFlags, BorderFlags) const override
	{
		Record("DrawButtonFrame");
		rect.InsetBy(1, 1);
	}

	void DrawButtonFrame(DrawContext&, gfx::Rect& rect, float, float, float, float,
		const gfx::Color&, const gfx::Color&, ControlStateFlags, BorderFlags) const override
	{
		Record("DrawButtonFrame");
		rect.InsetBy(1, 1);
	}

	void DrawButtonBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags, BOrientation) const override
	{
		Record("DrawButtonBackground");
	}

	void DrawButtonBackground(DrawContext&, gfx::Rect&, float, const gfx::Color&, ControlStateFlags,
		BorderFlags, BOrientation) const override
	{
		Record("DrawButtonBackground");
	}

	void DrawButtonBackground(DrawContext&, gfx::Rect&, float, float, float, float,
		const gfx::Color&, ControlStateFlags, BorderFlags, BOrientation) const override
	{
		Record("DrawButtonBackground");
	}

	void DrawMenuBarBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawMenuBarBackground");
	}

	void DrawMenuFieldFrame(DrawContext&, gfx::Rect& rect, const gfx::Color&, const gfx::Color&,
		ControlStateFlags, BorderFlags) const override
	{
		Record("DrawMenuFieldFrame");
		rect.InsetBy(1, 1);
	}

	void DrawMenuFieldFrame(DrawContext&, gfx::Rect& rect, float, const gfx::Color&,
		const gfx::Color&, ControlStateFlags, BorderFlags) const override
	{
		Record("DrawMenuFieldFrame");
		rect.InsetBy(1, 1);
	}

	void DrawMenuFieldFrame(DrawContext&, gfx::Rect& rect, float, float, float, float,
		const gfx::Color&, const gfx::Color&, ControlStateFlags, BorderFlags) const override
	{
		Record("DrawMenuFieldFrame");
		rect.InsetBy(1, 1);
	}

	void DrawMenuFieldBackground(DrawContext&, gfx::Rect&, const gfx::Color&, bool,
		ControlStateFlags) const override
	{
		Record("DrawMenuFieldBackground");
	}

	void DrawMenuFieldBackground(DrawContext&, gfx::Rect&, float, const gfx::Color&, bool,
		ControlStateFlags) const override
	{
		Record("DrawMenuFieldBackground");
	}

	void DrawMenuFieldBackground(DrawContext&, gfx::Rect&, float, float, float, float,
		const gfx::Color&, bool, ControlStateFlags) const override
	{
		Record("DrawMenuFieldBackground");
	}

	void DrawMenuFieldBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawMenuFieldBackground");
	}

	void DrawMenuBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawMenuBackground");
	}

	void DrawMenuItemBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawMenuItemBackground");
	}

	void DrawStatusBar(DrawContext&, gfx::Rect&, const gfx::Color&, const gfx::Color&, float)
		const override
	{
		Record("DrawStatusBar");
	}

	void DrawCheckBox(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags) const override
	{
		Record("DrawCheckBox");
	}

	void DrawRadioButton(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags)
		const override
	{
		Record("DrawRadioButton");
	}

	void DrawScrollBarBackground(DrawContext&, gfx::Rect&, gfx::Rect&, const gfx::Color&,
		ControlStateFlags, BOrientation) const override
	{
		Record("DrawScrollBarBackground");
	}

	void DrawScrollBarBackground(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BOrientation) const override
	{
		Record("DrawScrollBarBackground");
	}

	void DrawScrollViewFrame(DrawContext&, gfx::Rect& rect, gfx::Rect, gfx::Rect, const gfx::Color&,
		BBorderStyle, ControlStateFlags, BorderFlags) const override
	{
		Record("DrawScrollViewFrame");
		rect.InsetBy(1, 1);
	}

	void DrawArrowShape(DrawContext&, gfx::Rect&, const gfx::Color&, ArrowDirection,
		ControlStateFlags, float) const override
	{
		Record("DrawArrowShape");
	}

	gfx::Color SliderBarColor(const Palette&, const gfx::Color&) const override
	{
		return {0.5f, 0.5f, 0.5f, 1};
	}

	void DrawSliderBar(DrawContext&, gfx::Rect, const gfx::Color&, gfx::Color, gfx::Color, float,
		ControlStateFlags, BOrientation) const override
	{
		Record("DrawSliderBar");
	}

	void DrawSliderBar(DrawContext&, gfx::Rect, const gfx::Color&, gfx::Color, ControlStateFlags,
		BOrientation) const override
	{
		Record("DrawSliderBar");
	}

	void DrawSliderThumb(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BOrientation) const override
	{
		Record("DrawSliderThumb");
	}

	void DrawSliderTriangle(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BOrientation) const override
	{
		Record("DrawSliderTriangle");
	}

	void DrawSliderTriangle(DrawContext&, gfx::Rect&, const gfx::Color&, const gfx::Color&,
		ControlStateFlags, BOrientation) const override
	{
		Record("DrawSliderTriangle");
	}

	void DrawSliderHashMarks(DrawContext&, gfx::Rect&, const gfx::Color&, std::int32_t,
		BHashMarkLocation, ControlStateFlags, BOrientation) const override
	{
		Record("DrawSliderHashMarks");
	}

	void DrawActiveTab(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags, BorderFlags,
		Border, std::int32_t, std::int32_t, std::int32_t, std::int32_t) const override
	{
		Record("DrawActiveTab");
	}

	void DrawInactiveTab(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags, Border, std::int32_t, std::int32_t, std::int32_t, std::int32_t) const override
	{
		Record("DrawInactiveTab");
	}

	void DrawSplitter(DrawContext&, gfx::Rect&, const gfx::Color&, BOrientation, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawSplitter");
	}

	void DrawBorder(DrawContext&, gfx::Rect&, const gfx::Color&, BBorderStyle, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawBorder");
	}

	void DrawRaisedBorder(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawRaisedBorder");
	}

	void DrawGroupFrame(DrawContext&, gfx::Rect& rect, const gfx::Color&, BorderFlags)
		const override
	{
		Record("DrawGroupFrame");
		rect.InsetBy(1, 1);
	}

	void DrawTextControlBorder(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BorderFlags) const override
	{
		Record("DrawTextControlBorder");
	}

	void DrawLabel(DrawContext&, std::string_view, gfx::Rect, const gfx::Color&, ControlStateFlags,
		const gfx::Color*) const override
	{
		Record("DrawLabel");
	}

	void DrawLabel(DrawContext&, std::string_view, gfx::Rect, const gfx::Color&, ControlStateFlags,
		const BAlignment&, const gfx::Color*) const override
	{
		Record("DrawLabel");
	}

	void DrawLabel(DrawContext&, std::string_view, const gfx::Color&, ControlStateFlags,
		const gfx::Point&, const gfx::Color*) const override
	{
		Record("DrawLabel");
	}

	void DrawLabel(DrawContext&, std::string_view, gfx::ImageHandle, gfx::Rect, const gfx::Color&,
		ControlStateFlags, const BAlignment&, const gfx::Color*) const override
	{
		Record("DrawLabel");
	}

	Insets FrameInsets(FrameType, ControlStateFlags) const override
	{
		return {1, 2, 3, 4};
	}

	Insets BackgroundInsets(BackgroundType, ControlStateFlags) const override
	{
		return {1, 2, 3, 4};
	}

	void DrawButtonWithPopUpBackground(DrawContext&, gfx::Rect&, const gfx::Color&,
		ControlStateFlags, BorderFlags, BOrientation) const override
	{
		Record("DrawButtonWithPopUpBackground");
	}

	void DrawButtonWithPopUpBackground(DrawContext&, gfx::Rect&, float, const gfx::Color&,
		ControlStateFlags, BorderFlags, BOrientation) const override
	{
		Record("DrawButtonWithPopUpBackground");
	}

	void DrawButtonWithPopUpBackground(DrawContext&, gfx::Rect&, float, float, float, float,
		const gfx::Color&, ControlStateFlags, BorderFlags, BOrientation) const override
	{
		Record("DrawButtonWithPopUpBackground");
	}

	void DrawTabFrame(DrawContext&, gfx::Rect& rect, const gfx::Color&, ControlStateFlags,
		BorderFlags, BBorderStyle, Border) const override
	{
		Record("DrawTabFrame");
		rect.InsetBy(1, 1);
	}

	void DrawScrollBarButton(DrawContext&, gfx::Rect, const gfx::Color&, const gfx::Color&,
		ControlStateFlags, ArrowDirection, BOrientation, bool) const override
	{
		Record("DrawScrollBarButton");
	}

	void DrawScrollBarThumb(DrawContext&, gfx::Rect&, const gfx::Color&, ControlStateFlags,
		BOrientation, KnobStyle) const override
	{
		Record("DrawScrollBarThumb");
	}

	void DrawScrollBarBorder(DrawContext&, gfx::Rect, const gfx::Color&, ControlStateFlags,
		BOrientation) const override
	{
		Record("DrawScrollBarBorder");
	}

	float ScrollBarWidth(const TextSystem&, BOrientation) const override
	{
		return 5;
	}

private:
	void Record(const char* method) const
	{
		const std::scoped_lock lock(fLock);
		fCalls.push_back(method);
	}

	mutable std::mutex fLock;
	mutable std::vector<std::string> fCalls;
};


} // namespace ngui::test
