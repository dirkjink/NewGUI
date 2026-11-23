/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <string_view>

#include <ngui/Flags.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/GlyphRun.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>


namespace ngui {


// The vocabulary shared by all looks (specification 18.3, L5): the meaning
// of Haiku's BControlLook constants, with names of their own.


/*!	The state of a control, as a look sees it (Haiku's B_FOCUSED …). */
enum class ControlState : std::uint32_t {
	Focused = 1u << 0,				/*!< Has the keyboard focus. */
	Clicked = 1u << 1,				/*!< Pressed, before the mouse is released. */
	Activated = 1u << 2,			/*!< On (checked, selected). */
	Hover = 1u << 3,				/*!< Under the mouse. */
	Disabled = 1u << 4,				/*!< Not enabled. */
	DefaultButton = 1u << 5,		/*!< The default button of its window. */
	IgnoreOutline = 1u << 6,		/*!< Draw without the outer outline. */
	PartiallyActivated = 1u << 7,	/*!< The third state of a tri-state control. */
	Flat = 1u << 8,					/*!< Flat appearance (e.g. a toolbar button). */
	Invalid = 1u << 9,				/*!< Invalid value; drawn with the failure color. */
	IsControl = 1u << 10,			/*!< Use the control colors. */
	BlendFrame = 1u << 16			/*!< Blend the frame with the background. */
};
NGUI_FLAG_ENUM(ControlState)


/*!	The sides of a rectangle (Haiku's B_LEFT_BORDER …). */
enum class Border : std::uint8_t {
	Left = 1u << 0,		/*!< Left side. */
	Right = 1u << 1,	/*!< Right side. */
	Top = 1u << 2,		/*!< Top side. */
	Bottom = 1u << 3	/*!< Bottom side. */
};
NGUI_FLAG_ENUM(Border)


/*!	A set of ControlState flags. */
using ControlStateFlags = Flags<ControlState>;

/*!	A set of Border sides. */
using BorderFlags = Flags<Border>;

/*!	All four sides (Haiku's B_ALL_BORDERS). */
inline constexpr BorderFlags kAllBorders
	= Border::Left | Border::Right | Border::Top | Border::Bottom;


/*!	The frames a look draws (Haiku's frame_type). */
enum class FrameType : std::uint8_t {
	Button,			/*!< B_BUTTON_FRAME */
	Group,			/*!< B_GROUP_FRAME */
	MenuField,		/*!< B_MENU_FIELD_FRAME */
	ScrollView,		/*!< B_SCROLL_VIEW_FRAME */
	TextControl		/*!< B_TEXT_CONTROL_FRAME */
};


/*!	The backgrounds a look draws (Haiku's background_type). */
enum class BackgroundType : std::uint8_t {
	Button,					/*!< B_BUTTON_BACKGROUND */
	ButtonWithPopUp,		/*!< B_BUTTON_WITH_POP_UP_BACKGROUND */
	Menu,					/*!< B_MENU_BACKGROUND */
	MenuBar,				/*!< B_MENU_BAR_BACKGROUND */
	MenuField,				/*!< B_MENU_FIELD_BACKGROUND */
	MenuItem,				/*!< B_MENU_ITEM_BACKGROUND */
	HorizontalScrollBar,	/*!< B_HORIZONTAL_SCROLL_BAR_BACKGROUND */
	VerticalScrollBar		/*!< B_VERTICAL_SCROLL_BAR_BACKGROUND */
};


/*!	The direction of an arrow (Haiku's B_LEFT_ARROW …). */
enum class ArrowDirection : std::uint8_t {
	Left,		/*!< B_LEFT_ARROW */
	Right,		/*!< B_RIGHT_ARROW */
	Up,			/*!< B_UP_ARROW */
	Down,		/*!< B_DOWN_ARROW */
	LeftUp,		/*!< B_LEFT_UP_ARROW */
	RightUp,	/*!< B_RIGHT_UP_ARROW */
	RightDown,	/*!< B_RIGHT_DOWN_ARROW */
	LeftDown	/*!< B_LEFT_DOWN_ARROW */
};


/*!	The grip on a scroll bar thumb (Haiku's B_KNOB_*). */
enum class KnobStyle : std::uint8_t {
	None,	/*!< B_KNOB_NONE */
	Dots,	/*!< B_KNOB_DOTS */
	Lines	/*!< B_KNOB_LINES */
};


/*!	Distances from the four sides of a rectangle, in logical pixels. */
struct Insets {
			float				left;	/*!< From the left side. */
			float				top;	/*!< From the top side. */
			float				right;	/*!< From the right side. */
			float				bottom;	/*!< From the bottom side. */

			constexpr bool		operator==(const Insets& other) const = default;
									/*!< All four equal. */
			constexpr Insets	operator+(const Insets& other) const;
									/*!< Side-wise sum. */
};


/*!	The conformance a look needs from the backend (specification 21.4). */
enum class ConformanceLevel : std::uint8_t {
	C1,			/*!< Core profile without gradients. */
	C2,			/*!< Full core profile, linear gradients included. */
	Extended	/*!< Needs extended capabilities, see LookInfo::extendedCaps. */
};


/*!	Who a look is and what it needs (specification 18.3). */
struct LookInfo {
			std::string_view	id;				/*!< Stable key: "be", "haiku", "flat". */
			std::string_view	displayName;	/*!< Name for the settings. */
			std::uint32_t		version;		/*!< Version of the look. */
			ConformanceLevel	required;		/*!< Needed level; `requires` in the
													specification, a C++20 keyword. */
			gfx::Caps			extendedCaps;	/*!< With Extended: the capabilities needed. */
};


/*!	Everything a look draws with (specification 18.3, L1): the Canvas, the
	capabilities of the active backend, the palette and the text system. */
struct DrawContext {
			gfx::Canvas&		canvas;		/*!< Core profile (specification 6). */
			const gfx::Caps&	caps;		/*!< Capabilities of the active backend. */
			const Palette&		palette;	/*!< System colors. */
			const TextSystem&	text;		/*!< Metrics and shaping. */
			float				scale;		/*!< Only informative, e.g. for icon sizes. */
};


/*!	How the standard controls look and measure (specification 18): the
	successor of Haiku's BControlLook, with the same methods in the same
	order. Controls delegate all their drawing to the look and bring only
	their semantics: state, label, icon.

	Differences to BControlLook:
	- Drawing goes through a DrawContext, never to a backend (L1).
	- All methods are const; a look has no mutable state after construction
	  (L4), because several window threads draw with it at once.
	- Rectangles are half-open (specification 4.1). The Draw…Frame() methods
	  shrink the rectangle by the border they drew, so that the following
	  Draw…Background() draws into the rest (L6).
	- No update rectangle: a View records its whole DisplayList.
	- No global font or palette: the methods that measure or derive colors
	  get the TextSystem or the Palette as a parameter.
	- Icons are ImageHandles, labels UTF-8 strings drawn with the
	  TextSystem.
	- Flags(const BControl&) comes with the controls (module W2), as a
	  method with a default implementation (L8).

	Methods added later go to the end of the class with a default
	implementation, and kInterfaceVersion grows (L8, 18.6).
*/
class ControlLook {
public:
	/*!	Version of this interface, checked by look plugins (L12). */
	static constexpr std::uint32_t kInterfaceVersion = 1;

	virtual						~ControlLook() = default;

	virtual	LookInfo			Info() const = 0;
									/*!< Who this look is. */

	// Metrics: they affect the layout.

	virtual	BAlignment			DefaultLabelAlignment() const = 0;
									/*!< Where labels go by default. */
	virtual	float				DefaultLabelSpacing(const TextSystem& text) const = 0;
									/*!< Space between a label and its control. */
	virtual	float				DefaultItemSpacing(const TextSystem& text) const = 0;
									/*!< Space between items, e.g. in a layout. */

	// Drawing: it affects only the DisplayList.

	virtual	void				DrawButtonFrame(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a button, with the look's radius. */
	virtual	void				DrawButtonFrame(DrawContext& context, gfx::Rect& rect,
									float radius, const gfx::Color& base,
									const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a button with corner \a radius. */
	virtual	void				DrawButtonFrame(DrawContext& context, gfx::Rect& rect,
									float leftTopRadius, float rightTopRadius,
									float leftBottomRadius, float rightBottomRadius,
									const gfx::Color& base, const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a button with a radius per corner. */

	virtual	void				DrawButtonBackground(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< The background of a button. */
	virtual	void				DrawButtonBackground(DrawContext& context, gfx::Rect& rect,
									float radius, const gfx::Color& base,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< The background of a button with corner \a radius. */
	virtual	void				DrawButtonBackground(DrawContext& context, gfx::Rect& rect,
									float leftTopRadius, float rightTopRadius,
									float leftBottomRadius, float rightBottomRadius,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< The background of a button with a radius per corner. */

	virtual	void				DrawMenuBarBackground(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The background of a menu bar. */

	virtual	void				DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a menu field. */
	virtual	void				DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect,
									float radius, const gfx::Color& base,
									const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a menu field with corner \a radius. */
	virtual	void				DrawMenuFieldFrame(DrawContext& context, gfx::Rect& rect,
									float leftTopRadius, float rightTopRadius,
									float leftBottomRadius, float rightBottomRadius,
									const gfx::Color& base, const gfx::Color& background,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a menu field with a radius per corner. */

	virtual	void				DrawMenuFieldBackground(DrawContext& context,
									gfx::Rect& rect, const gfx::Color& base,
									bool popupIndicator, ControlStateFlags flags = {})
									const = 0;
									/*!< The background of a menu field, with a pop-up
										indicator if \a popupIndicator. */
	virtual	void				DrawMenuFieldBackground(DrawContext& context,
									gfx::Rect& rect, float radius, const gfx::Color& base,
									bool popupIndicator, ControlStateFlags flags = {})
									const = 0;
									/*!< As above, with corner \a radius. */
	virtual	void				DrawMenuFieldBackground(DrawContext& context,
									gfx::Rect& rect, float leftTopRadius,
									float rightTopRadius, float leftBottomRadius,
									float rightBottomRadius, const gfx::Color& base,
									bool popupIndicator, ControlStateFlags flags = {})
									const = 0;
									/*!< As above, with a radius per corner. */
	virtual	void				DrawMenuFieldBackground(DrawContext& context,
									gfx::Rect& rect, const gfx::Color& base,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The background of a menu field without indicator. */

	virtual	void				DrawMenuBackground(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The background of a menu. */

	virtual	void				DrawMenuItemBackground(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The background of a menu item. */

	virtual	void				DrawStatusBar(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, const gfx::Color& barColor,
									float progressPosition) const = 0;
									/*!< A progress bar filled up to \a progressPosition. */

	virtual	void				DrawCheckBox(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {})
									const = 0;
									/*!< The box of a check box. */

	virtual	void				DrawRadioButton(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {})
									const = 0;
									/*!< The circle of a radio button. */

	virtual	void				DrawScrollBarBackground(DrawContext& context,
									gfx::Rect& rect1, gfx::Rect& rect2,
									const gfx::Color& base, ControlStateFlags flags,
									BOrientation orientation) const = 0;
									/*!< The background of a scroll bar on both sides of the
										thumb. */
	virtual	void				DrawScrollBarBackground(DrawContext& context,
									gfx::Rect& rect, const gfx::Color& base,
									ControlStateFlags flags, BOrientation orientation)
									const = 0;
									/*!< The background of a scroll bar in one piece. */

	virtual	void				DrawScrollViewFrame(DrawContext& context, gfx::Rect& rect,
									gfx::Rect verticalScrollBarFrame,
									gfx::Rect horizontalScrollBarFrame,
									const gfx::Color& base, BBorderStyle borderStyle,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a scroll view around its scroll bars. */

	virtual	void				DrawArrowShape(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ArrowDirection direction,
									ControlStateFlags flags = {},
									float tint = kDarkenMaxTint) const = 0;
									/*!< An arrow pointing in \a direction. */

	virtual	gfx::Color			SliderBarColor(const Palette& palette,
									const gfx::Color& base) const = 0;
									/*!< The color of a slider bar for \a base; looks may
										derive it from \a palette, as Haiku's from
										ui_color(). */

	virtual	void				DrawSliderBar(DrawContext& context, gfx::Rect rect,
									const gfx::Color& base, gfx::Color leftFillColor,
									gfx::Color rightFillColor, float sliderScale,
									ControlStateFlags flags, BOrientation orientation)
									const = 0;
									/*!< A slider bar filled in two colors at \a sliderScale. */
	virtual	void				DrawSliderBar(DrawContext& context, gfx::Rect rect,
									const gfx::Color& base, gfx::Color fillColor,
									ControlStateFlags flags, BOrientation orientation)
									const = 0;
									/*!< A slider bar in one fill color. */

	virtual	void				DrawSliderThumb(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags,
									BOrientation orientation) const = 0;
									/*!< A block thumb. */

	virtual	void				DrawSliderTriangle(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags,
									BOrientation orientation) const = 0;
									/*!< A triangle thumb. */
	virtual	void				DrawSliderTriangle(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, const gfx::Color& fill,
									ControlStateFlags flags, BOrientation orientation)
									const = 0;
									/*!< A triangle thumb filled with \a fill. */

	virtual	void				DrawSliderHashMarks(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, std::int32_t count,
									BHashMarkLocation location, ControlStateFlags flags,
									BOrientation orientation) const = 0;
									/*!< \a count hash marks at \a location. */

	virtual	void				DrawActiveTab(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									Border side = Border::Top, std::int32_t index = 0,
									std::int32_t selected = -1, std::int32_t first = 0,
									std::int32_t last = 0) const = 0;
									/*!< The selected tab of a tab view. */
	virtual	void				DrawInactiveTab(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									Border side = Border::Top, std::int32_t index = 0,
									std::int32_t selected = -1, std::int32_t first = 0,
									std::int32_t last = 0) const = 0;
									/*!< A tab that is not selected. */

	virtual	void				DrawSplitter(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, BOrientation orientation,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The splitter between two panes. */

	// Various borders.

	virtual	void				DrawBorder(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, BBorderStyle borderStyle,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< A border in \a borderStyle. */

	virtual	void				DrawRaisedBorder(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< A raised border. */

	virtual	void				DrawGroupFrame(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base,
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The frame of a group (a box). */

	virtual	void				DrawTextControlBorder(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders) const = 0;
									/*!< The border of a text control. */

	// Aligned labels.

	virtual	void				DrawLabel(DrawContext& context, std::string_view label,
									gfx::Rect rect, const gfx::Color& base,
									ControlStateFlags flags,
									const gfx::Color* textColor = nullptr) const = 0;
									/*!< \a label in \a rect with the default alignment. */
	virtual	void				DrawLabel(DrawContext& context, std::string_view label,
									gfx::Rect rect, const gfx::Color& base,
									ControlStateFlags flags, const BAlignment& alignment,
									const gfx::Color* textColor = nullptr) const = 0;
									/*!< \a label in \a rect with \a alignment. */
	virtual	void				DrawLabel(DrawContext& context, std::string_view label,
									const gfx::Color& base, ControlStateFlags flags,
									const gfx::Point& where,
									const gfx::Color* textColor = nullptr) const = 0;
									/*!< \a label with its baseline starting at \a where. */
			void				DrawLabel(DrawContext& context, std::string_view label,
									gfx::ImageHandle icon, gfx::Rect rect,
									const gfx::Color& base, ControlStateFlags flags,
									const gfx::Color* textColor = nullptr) const;
									/*!< \a label and \a icon (0 for none) with the default
										alignment. */
	virtual	void				DrawLabel(DrawContext& context, std::string_view label,
									gfx::ImageHandle icon, gfx::Rect rect,
									const gfx::Color& base, ControlStateFlags flags,
									const BAlignment& alignment,
									const gfx::Color* textColor = nullptr) const = 0;
									/*!< \a label and \a icon with \a alignment. */

	virtual	Insets				FrameInsets(FrameType frameType,
									ControlStateFlags flags) const = 0;
									/*!< The border a Draw…Frame() method consumes. */
	virtual	Insets				BackgroundInsets(BackgroundType backgroundType,
									ControlStateFlags flags) const = 0;
									/*!< The border a Draw…Background() method consumes. */
			Insets				FrameAndBackgroundInsets(FrameType frameType,
									BackgroundType backgroundType,
									ControlStateFlags flags) const;
									/*!< Both together, as Haiku's GetInsets(). */

	virtual	void				DrawButtonWithPopUpBackground(DrawContext& context,
									gfx::Rect& rect, const gfx::Color& base,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< The background of a button with a pop-up marker. */
	virtual	void				DrawButtonWithPopUpBackground(DrawContext& context,
									gfx::Rect& rect, float radius, const gfx::Color& base,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< As above, with corner \a radius. */
	virtual	void				DrawButtonWithPopUpBackground(DrawContext& context,
									gfx::Rect& rect, float leftTopRadius,
									float rightTopRadius, float leftBottomRadius,
									float rightBottomRadius, const gfx::Color& base,
									ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BOrientation orientation = BOrientation::Horizontal)
									const = 0;
									/*!< As above, with a radius per corner. */

	virtual	void				DrawTabFrame(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags = {},
									BorderFlags borders = kAllBorders,
									BBorderStyle borderStyle = BBorderStyle::Fancy,
									Border side = Border::Top) const = 0;
									/*!< The frame around the tabs of a tab view. */

	virtual	void				DrawScrollBarButton(DrawContext& context, gfx::Rect rect,
									const gfx::Color& base, const gfx::Color& text,
									ControlStateFlags flags, ArrowDirection direction,
									BOrientation orientation, bool down = false) const = 0;
									/*!< An arrow button of a scroll bar. */
	virtual	void				DrawScrollBarThumb(DrawContext& context, gfx::Rect& rect,
									const gfx::Color& base, ControlStateFlags flags,
									BOrientation orientation,
									KnobStyle knobStyle = KnobStyle::None) const = 0;
									/*!< The thumb of a scroll bar. */
	virtual	void				DrawScrollBarBorder(DrawContext& context, gfx::Rect rect,
									const gfx::Color& base, ControlStateFlags flags,
									BOrientation orientation) const = 0;
									/*!< The border of a scroll bar. */
	virtual	float				ScrollBarWidth(const TextSystem& text,
									BOrientation orientation) const = 0;
									/*!< Width of a vertical or height of a horizontal scroll
										bar; it grows with the font, as Haiku's. */
};


constexpr Insets
Insets::operator+(const Insets& other) const
{
	return {left + other.left, top + other.top, right + other.right, bottom + other.bottom};
}


} // namespace ngui
