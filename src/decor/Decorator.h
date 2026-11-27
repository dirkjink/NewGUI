/*
 * Copyright 2001-2020 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The frame of a window drawn by NewGUI instead of the window system
// (specification 24, point 9): Haiku's Decorator and TabDecorator of the
// app_server, for the one tab a window has here (no stack and tile, no
// outline resizing). A look's decorator (BeDecorator ...) draws it through
// the compatibility layer, line by line as Haiku does (K3).
//
// Coordinates are the window's content coordinates: the content is
// {0, 0, width, height}, the frame lies around it at negative coordinates
// and beyond the size. Internally Haiku's inclusive BRects with the content
// at BRect(0, 0, width - 1, height - 1); outside half-open gfx::Rects.


#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>

#include "compat/CompatView.h"
#include "compat/Font.h"
#include "compat/InterfaceDefs.h"
#include "compat/Rect.h"


namespace ngui {


class Palette;
class TextSystem;


namespace detail {


class Decorator {
public:
	/*!	The parts of the frame (Haiku's Decorator::Region). */
	enum class Region : std::uint8_t {
		None,
		Tab,
		CloseButton,
		ZoomButton,
		LeftBorder,
		RightBorder,
		TopBorder,
		BottomBorder,
		LeftTopCorner,
		LeftBottomCorner,
		RightTopCorner,
		RightBottomCorner
	};

	/*!	Haiku's window_look as far as NewGUI's windows have it. */
	enum class Look : std::uint8_t {
		Titled,		/*!< B_TITLED_WINDOW_LOOK */
		Bordered,	/*!< B_BORDERED_WINDOW_LOOK */
		NoBorder	/*!< B_NO_BORDER_WINDOW_LOOK */
	};

	explicit					Decorator(const TextSystem& text);
	virtual						~Decorator();

								Decorator(const Decorator&) = delete;
			Decorator&			operator=(const Decorator&) = delete;

			void				SetLook(Look look);
			Look				GetLook() const;
			void				SetFlags(bool closable, bool zoomable, bool resizable);
			bool				IsClosable() const;
			bool				IsZoomable() const;
			bool				IsResizable() const;
			void				SetTitle(std::string_view title);
			const std::string&	Title() const;
			const std::string&	TruncatedTitle() const;
			void				SetFocus(bool focused);
			bool				IsFocus() const;
			void				SetContentSize(float width, float height);
			void				SetPressed(Region button, bool pressed);
			bool				IsPressed(Region button) const;
			bool				SetTabLocation(float location);
									// Pixels from the left of the frame, limited as
									// Haiku's _SetTabLocation(); true if it moved.
			float				TabOffset() const;
			float				TabLocation() const;
									// Haiku's tabLocation: 0 at the left, 1 at the
									// right.

			gfx::Rect			Bounds() const;
									// The frame and the content, half-open.
			std::vector<gfx::Rect> Footprint() const;
									// The areas the frame covers (borders, tab).
			gfx::Rect			TabRect() const;
			gfx::Rect			CloseRect() const;
			gfx::Rect			ZoomRect() const;
			gfx::Rect			ResizeRect() const;
									// Where the frame resizes at the bottom right
									// corner; empty if it does not.
			float				BorderWidth() const;
			float				TabHeight() const;
									// The tab's rows above the border.
			Region				RegionAt(gfx::Point where) const;

			void				Draw(DrawContext& context);
									// The whole frame, in content coordinates.

protected:
	// TabDecorator's colors of a component, by these indices.
	enum {
		COLOR_TAB_FRAME_LIGHT	= 0,
		COLOR_TAB_FRAME_DARK	= 1,
		COLOR_TAB				= 2,
		COLOR_TAB_LIGHT			= 3,
		COLOR_TAB_BEVEL			= 4,
		COLOR_TAB_SHADOW		= 5,
		COLOR_TAB_TEXT			= 6
	};

	enum {
		COLOR_BUTTON			= 0,
		COLOR_BUTTON_LIGHT		= 1
	};

	enum Component {
		COMPONENT_TAB,
		COMPONENT_CLOSE_BUTTON,
		COMPONENT_ZOOM_BUTTON,
		COMPONENT_LEFT_BORDER,
		COMPONENT_RIGHT_BORDER,
		COMPONENT_TOP_BORDER,
		COMPONENT_BOTTOM_BORDER,
		COMPONENT_RESIZE_CORNER
	};

	using ComponentColors = compat::rgb_color[7];

	// Haiku's Decorator::Tab, the one tab.
	struct Tab {
			compat::BRect		tabRect;
			compat::BRect		zoomRect;
			compat::BRect		closeRect;
			bool				closePressed = false;
			bool				zoomPressed = false;
			Look				look = Look::Titled;
			bool				closable = true;
			bool				zoomable = true;
			bool				isFocused = false;
			std::string			title;
			std::uint32_t		tabOffset = 0;
			float				tabLocation = 0.0f;
			float				textOffset = 10.0f;
			std::string			truncatedTitle;
			bool				buttonFocus = false;
			float				minTabSize = 0.0f;
			float				maxTabSize = 0.0f;
	};

	virtual	void				_DrawFrame(compat::CompatView& view, compat::BRect rect) = 0;
	virtual	void				_DrawTab(compat::CompatView& view, Tab* tab,
									compat::BRect rect) = 0;
	virtual	void				_DrawTitle(compat::CompatView& view, Tab* tab,
									compat::BRect rect) = 0;
	virtual	void				_DrawClose(compat::CompatView& view, Tab* tab,
									compat::BRect rect) = 0;
	virtual	void				_DrawZoom(compat::CompatView& view, Tab* tab,
									compat::BRect rect) = 0;
	virtual	void				_GetButtonSizeAndOffset(const compat::BRect& tabRect,
									float* offset, float* size, float* inset) const;

			void				_DrawButtons(compat::CompatView& view, Tab* tab,
									const compat::BRect& invalid);
			void				_UpdateColors(const Palette& palette);

			bool				_IsFocus(const Tab* tab) const;

	// TabDecorator's layout
			void				_DoLayout();
			void				_DoTabLayout();
			void				_LayoutTabItems(Tab* tab, const compat::BRect& tabRect);
			float				_DefaultTextOffset() const;
			float				_TitleWidth(const std::string& title) const;
			float				_SingleTabOffsetAndSize(float& tabSize);

			const TextSystem&	fText;
			compat::BFont		fFont;

			Tab					fTab;
			Tab*				fTopTab = &fTab;

			compat::BRect		fTitleBarRect;
			compat::BRect		fFrame;
			compat::BRect		fResizeRect;
			compat::BRect		fBorderRect;
			compat::BRect		fLeftBorder;
			compat::BRect		fTopBorder;
			compat::BRect		fBottomBorder;
			compat::BRect		fRightBorder;
			std::int32_t		fBorderWidth = 0;
			float				fResizeKnobSize = 0;
			float				fBorderResizeLength = 0;
			bool				fResizable = true;

	// TabDecorator's colors, from the palette
			compat::rgb_color	fFocusFrameColor;
			compat::rgb_color	fFocusTabColor;
			compat::rgb_color	fFocusTabColorLight;
			compat::rgb_color	fFocusTabColorBevel;
			compat::rgb_color	fFocusTabColorShadow;
			compat::rgb_color	fFocusTextColor;
			compat::rgb_color	fNonFocusFrameColor;
			compat::rgb_color	fNonFocusTabColor;
			compat::rgb_color	fNonFocusTabColorLight;
			compat::rgb_color	fNonFocusTabColorBevel;
			compat::rgb_color	fNonFocusTabColorShadow;
			compat::rgb_color	fNonFocusTextColor;
};


/*!	The decorator of the look \a lookId ("be" ...), nullptr for a look
	without one (the window system's frame stays). */
std::unique_ptr<Decorator>		MakeDecorator(std::string_view lookId, const TextSystem& text);


} // namespace detail
} // namespace ngui
