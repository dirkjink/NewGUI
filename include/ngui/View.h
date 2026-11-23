/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include <ngui/ControlLook.h>
#include <ngui/Flags.h>
#include <ngui/Handler.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


class BWindow;


/*!	What a view asks of its window (Haiku's B_WILL_DRAW ...). */
enum class BViewFlag : std::uint32_t {
	WillDraw = 1u << 0,				/*!< B_WILL_DRAW: Draw() is called. */
	PulseNeeded = 1u << 1,			/*!< B_PULSE_NEEDED: Pulse() is called. */
	NavigableJump = 1u << 2,		/*!< B_NAVIGABLE_JUMP: a group in keyboard navigation. */
	FrameEvents = 1u << 3,			/*!< B_FRAME_EVENTS: FrameMoved() and FrameResized(). */
	Navigable = 1u << 4,			/*!< B_NAVIGABLE: may get the focus with Tab. */
	DrawOnChildren = 1u << 5,		/*!< B_DRAW_ON_CHILDREN: DrawAfterChildren() is called. */
	FullUpdateOnResize = 1u << 6,	/*!< B_FULL_UPDATE_ON_RESIZE: kept for ported code; a
										view is always recorded whole (11.4). */
	InputMethodAware = 1u << 7		/*!< B_INPUT_METHOD_AWARE */
};
NGUI_FLAG_ENUM(BViewFlag)

/*!	A set of BViewFlag. */
using BViewFlags = Flags<BViewFlag>;


/*!	How a side of a view follows its parent when the parent is resized
	(Haiku's B_FOLLOW_* resizing modes, for views without a layout). */
enum class BHorizontalFollow : std::uint8_t {
	Left,		/*!< B_FOLLOW_LEFT: the left distance stays. */
	Right,		/*!< B_FOLLOW_RIGHT: the right distance stays. */
	LeftRight,	/*!< B_FOLLOW_LEFT_RIGHT: both stay; the view grows. */
	Center		/*!< B_FOLLOW_H_CENTER: it moves by half the change. */
};


/*!	The vertical counterpart of BHorizontalFollow. */
enum class BVerticalFollow : std::uint8_t {
	Top,		/*!< B_FOLLOW_TOP */
	Bottom,		/*!< B_FOLLOW_BOTTOM */
	TopBottom,	/*!< B_FOLLOW_TOP_BOTTOM */
	Center		/*!< B_FOLLOW_V_CENTER */
};


/*!	A resizing mode: one rule per direction. */
struct BResizingMode {
			BHorizontalFollow	horizontal = BHorizontalFollow::Left;
									/*!< The horizontal rule. */
			BVerticalFollow		vertical = BVerticalFollow::Top;
									/*!< The vertical rule. */

			constexpr bool		operator==(const BResizingMode& other) const = default;
									/*!< Both rules equal. */
};

/*!	B_FOLLOW_LEFT_TOP, the default. */
inline constexpr BResizingMode kFollowLeftTop = {};

/*!	B_FOLLOW_ALL: the view grows with its parent. */
inline constexpr BResizingMode kFollowAll
	= {BHorizontalFollow::LeftRight, BVerticalFollow::TopBottom};


/*!	What views draw with besides their Canvas, given by the window for one
	frame (specification 18.3, L1). */
struct DrawEnvironment {
			const gfx::Caps&	caps;		/*!< Capabilities of the active backend. */
			const Palette&		palette;	/*!< System colors. */
			const TextSystem&	text;		/*!< Metrics and shaping. */
			float				scale;		/*!< Device pixels per logical pixel. */
};


/*!	A rectangle in a window that draws and handles events (module G8), as
	Haiku's BView: views form a tree, each placed by its frame in its
	parent's coordinates.

	Drawing (specification 11.4, R2): a view draws only through the Canvas
	of the DrawContext its Draw() gets. Each view keeps the DisplayList it
	recorded last; Invalidate() drops only that list. The window's list is
	put together from the views' lists for every frame, each child placed
	with DrawList() and clipped to its frame.

	Differences to Haiku:
	- Rectangles are half-open (specification 4.1): a view of frame
	  {0, 0, 100, 50} is 100 × 50 pixels.
	- Ownership is explicit: AddChild() takes the child as std::unique_ptr,
	  RemoveChild() gives it back; a view destroys its children.
	- Draw() gets a DrawContext instead of an update rectangle and draws on
	  its Canvas; there is no BView drawing state (high color, pen ...).
	- FrameMoved() and FrameResized() are called when the frame changes,
	  not later through messages.
	- The event hooks come with BWindow (G9), the layout with G10.
*/
class BView : public BHandler {
public:
								BView(gfx::Rect frame, std::string_view name,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A view at \a frame in its future parent. */
	explicit					BView(std::string_view name,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A view for a layout, with an empty frame. */
								~BView() override;
									/*!< Destroys its children; a parent must remove it
										first (RemoveChild()). */

	// The tree

			BView*				AddChild(std::unique_ptr<BView> child, BView* before = nullptr);
									/*!< Adds \a child, owned, before \a before (at the end
										if nullptr); returns it. nullptr if \a child is
										nullptr or \a before is not a child. */
			std::unique_ptr<BView> RemoveChild(BView* child);
									/*!< Takes \a child out and gives it back; nullptr if it
										is not a child. */
			std::unique_ptr<BView> RemoveSelf();
									/*!< RemoveChild() on the parent. */
			BView*				Parent() const;
									/*!< The parent, nullptr for a root. */
			std::int32_t		CountChildren() const;
									/*!< The direct children. */
			BView*				ChildAt(std::int32_t index) const;
									/*!< Child \a index, nullptr outside. */
			BView*				NextSibling() const;
									/*!< The next child of the parent, nullptr if none. */
			BView*				PreviousSibling() const;
									/*!< The previous child of the parent, nullptr if none. */
			BView*				FindView(std::string_view name) const;
									/*!< The first view called \a name in the subtree, this
										view included, depth first. */
			BWindow*			Window() const;
									/*!< The window it is in, nullptr if none. */

	// Geometry

			gfx::Rect			Frame() const;
									/*!< Its rectangle in the parent's coordinates. */
			gfx::Rect			Bounds() const;
									/*!< Its rectangle in its own coordinates: the size of
										the frame at the scroll position. */
			void				MoveBy(float dx, float dy);
									/*!< Moves the frame. */
			void				MoveTo(gfx::Point where);
									/*!< Moves the frame's left top corner to \a where. */
			void				ResizeBy(float dw, float dh);
									/*!< Changes the size; children follow their resizing
										modes. */
			void				ResizeTo(float width, float height);
									/*!< Sets the size. */
			void				ScrollBy(float dx, float dy);
									/*!< Moves the content: Bounds() moves, Frame() stays. */
			void				ScrollTo(gfx::Point where);
									/*!< Sets the left top corner of Bounds(). */

			gfx::Point			ConvertToParent(gfx::Point point) const;
									/*!< From its coordinates to the parent's. */
			gfx::Point			ConvertFromParent(gfx::Point point) const;
									/*!< From the parent's coordinates to its own. */
			gfx::Rect			ConvertToParent(const gfx::Rect& rect) const;
									/*!< From its coordinates to the parent's. */
			gfx::Rect			ConvertFromParent(const gfx::Rect& rect) const;
									/*!< From the parent's coordinates to its own. */

			BResizingMode		ResizingMode() const;
									/*!< How it follows its parent's size. */
			void				SetResizingMode(BResizingMode mode);
									/*!< Changes that. */
			BViewFlags			Flags() const;
									/*!< Its flags. */
			void				SetFlags(BViewFlags flags);
									/*!< Changes them. */

	// Visibility

			void				Show();
									/*!< Undoes one Hide(). */
			void				Hide();
									/*!< Hides it and its subtree; nested, as in Haiku. */
			bool				IsHidden() const;
									/*!< Hidden itself or through a parent. */

	// Drawing

	virtual	void				Draw(DrawContext& context);
									/*!< Draws the view in its own coordinates (Bounds()) on
										context.canvas; nothing by default. */
	virtual	void				DrawAfterChildren(DrawContext& context);
									/*!< With DrawOnChildren: draws over the children. */
			void				Invalidate();
									/*!< Drops its DisplayList; the next frame records it
										again. */
			bool				IsValid() const;
									/*!< It has a recorded DisplayList. */
			void				SetViewColor(std::optional<gfx::Color> color);
									/*!< The background filled before Draw(); std::nullopt
										for none (B_TRANSPARENT_COLOR). White by default. */
			std::optional<gfx::Color> ViewColor() const;
									/*!< The background, std::nullopt if transparent. */

			gfx::DisplayListRef	RecordTree(const DrawEnvironment& environment);
									/*!< The DisplayList of the view and its visible subtree
										in its own coordinates: its own list (recorded if it
										was invalidated) with the children's placed in. Used
										by the window for every frame. */

	// Hooks

	virtual	void				FrameMoved(gfx::Point newPosition);
									/*!< With FrameEvents: the frame moved. */
	virtual	void				FrameResized(float newWidth, float newHeight);
									/*!< With FrameEvents: the frame changed its size. */

private:
	friend class BWindow;

			void				_ParentResized(float dw, float dh);
			void				_SetWindow(BWindow* window);
			gfx::DisplayListRef	_RecordOwn(const DrawEnvironment& environment);
			gfx::DisplayListRef	_RecordChildrenOver(const DrawEnvironment& environment);

			gfx::Rect			fFrame;
			gfx::Point			fScroll = {0, 0};
			BResizingMode		fResizingMode;
			BViewFlags			fFlags;
			std::optional<gfx::Color> fViewColor;
			int					fHideLevel = 0;
			BView*				fParent = nullptr;
			BWindow*			fWindow = nullptr;
			std::vector<std::unique_ptr<BView>> fChildren;
			gfx::DisplayListRef	fList;
			gfx::DisplayListRef	fListAfterChildren;
};


} // namespace ngui
