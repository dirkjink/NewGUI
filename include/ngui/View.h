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
#include <ngui/Layout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui {


class BAbstractLayout;
class BLayoutContext;
class BMessage;
class BViewLayoutItem;
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
	InputMethodAware = 1u << 7,		/*!< B_INPUT_METHOD_AWARE */
	SupportsLayout = 1u << 8,		/*!< B_SUPPORTS_LAYOUT: a new size lays out its
										children instead of moving them by their
										resizing modes. */
	InvalidateAfterLayout = 1u << 9	/*!< B_INVALIDATE_AFTER_LAYOUT: invalidated after
										each layout. */
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


/*!	Where the mouse is relative to a view in MouseMoved() (Haiku's
	B_ENTERED_VIEW ...). */
enum class BMouseTransit : std::uint8_t {
	Entered,	/*!< B_ENTERED_VIEW: it just came in. */
	Inside,		/*!< B_INSIDE_VIEW: it moved inside. */
	Exited,		/*!< B_EXITED_VIEW: it just left. */
	Outside		/*!< B_OUTSIDE_VIEW: it moved outside, while the view tracks it. */
};


/*!	Events a view asks for beyond those over it (Haiku's B_POINTER_EVENTS,
	B_KEYBOARD_EVENTS). */
enum class BEventMask : std::uint32_t {
	Pointer = 1u << 0,	/*!< All mouse events until the buttons are up. */
	Keyboard = 1u << 1	/*!< All key events, also without the focus. */
};
NGUI_FLAG_ENUM(BEventMask)

/*!	A set of BEventMask. */
using BEventMaskFlags = Flags<BEventMask>;


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
	- Positions are given to the mouse hooks; buttons, modifiers and clicks
	  are in Window()->CurrentMessage() as in Haiku. KeyDown() gets the
	  typed text as std::string_view.
	- Layout (module G10) as in Haiku: SetLayout() gives the view a layout
	  that places its children; MinSize(), MaxSize(), PreferredSize() and
	  LayoutAlignment() tell layouts how large it may be. Sizes are pixel
	  counts (BLayoutUtils). A view that has a layout lays it out when its
	  size changes and, in a window, before each frame in which a layout
	  was invalidated.
*/
class BView : public BHandler {
public:
								BView(gfx::Rect frame, std::string_view name,
									BResizingMode resizingMode = kFollowLeftTop,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A view at \a frame in its future parent. */
	explicit					BView(std::string_view name,
									BViewFlags flags = BViewFlag::WillDraw);
									/*!< A view for a layout, with an empty frame;
										SupportsLayout is added to \a flags. */
								~BView() override;
									/*!< Destroys its children; a parent must remove it
										first (RemoveChild()). */

	// The tree

			BView*				AddChild(std::unique_ptr<BView> child, BView* before = nullptr);
									/*!< Adds \a child, owned, before \a before (at the end
										if nullptr); returns it. nullptr if \a child is
										nullptr or \a before is not a child. */
			BLayoutItem*		AddChild(std::unique_ptr<BLayoutItem> child);
									/*!< Adds \a child to its layout (BLayout::AddItem());
										nullptr if it has none. */
			std::unique_ptr<BView> RemoveChild(BView* child);
									/*!< Takes \a child out and gives it back; nullptr if it
										is not a child. Its items in layouts of this view
										are removed and destroyed. */
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
			gfx::Point			ConvertToWindow(gfx::Point point) const;
									/*!< From its coordinates to the window's content area,
										the coordinates of the window's root view. */
			gfx::Point			ConvertFromWindow(gfx::Point point) const;
									/*!< From the window's content area to its coordinates. */
			gfx::Rect			ConvertToWindow(const gfx::Rect& rect) const;
									/*!< From its coordinates to the window's content area. */
			gfx::Point			ConvertToScreen(gfx::Point point) const;
									/*!< From its coordinates to the desktop's. */
			gfx::Point			ConvertFromScreen(gfx::Point point) const;
									/*!< From the desktop's coordinates to its own. */

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

	// Events

			void				MakeFocus(bool focus = true);
									/*!< Takes (or gives up) the keyboard focus of its
										window. */
			bool				IsFocus() const;
									/*!< It has the focus. */
			void				SetMouseEventMask(BEventMaskFlags mask);
									/*!< In MouseDown(): \a mask (Pointer) sends it all mouse
										events until the buttons are up, as Haiku's. */

	// Layout (module G10)

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout);
									/*!< Gives the view \a layout, which places its
										children (all of them are added to it); the old
										layout is destroyed. Returns the layout. */
			BLayout*			GetLayout() const;
									/*!< Its layout, or nullptr. */

	virtual	gfx::Size			MinSize();
									/*!< The explicit minimum where set, else the layout's,
										else GetPreferredSize(). */
	virtual	gfx::Size			MaxSize();
									/*!< The explicit maximum where set, else the layout's,
										else unlimited. */
	virtual	gfx::Size			PreferredSize();
									/*!< The explicit preferred size where set, else the
										layout's, else GetPreferredSize(). */
	virtual	BAlignment			LayoutAlignment();
									/*!< The explicit alignment where set, else the
										layout's, else centered. */
	virtual	void				GetPreferredSize(float* width, float* height);
									/*!< The size it would like without a layout; its
										current size by default. Either may be nullptr. */
	virtual	void				ResizeToPreferred();
									/*!< ResizeTo() GetPreferredSize(). */

			void				SetExplicitMinSize(gfx::Size size);
									/*!< Overrides the set parts of MinSize(). */
			void				SetExplicitMaxSize(gfx::Size size);
									/*!< Overrides the set parts of MaxSize(). */
			void				SetExplicitPreferredSize(gfx::Size size);
									/*!< Overrides the set parts of PreferredSize(). */
			void				SetExplicitSize(gfx::Size size);
									/*!< Sets all three. */
			void				SetExplicitAlignment(BAlignment alignment);
									/*!< Overrides the set parts of LayoutAlignment(). */
			gfx::Size			ExplicitMinSize() const;
									/*!< The explicit minimum (kUnsetSize by default). */
			gfx::Size			ExplicitMaxSize() const;
									/*!< The explicit maximum. */
			gfx::Size			ExplicitPreferredSize() const;
									/*!< The explicit preferred size. */
			BAlignment			ExplicitAlignment() const;
									/*!< The explicit alignment. */

	virtual	bool				HasHeightForWidth();
									/*!< Whether its height depends on its width; its
										layout's answer, false without one. */
	virtual	void				GetHeightForWidth(float width, float* min,
									float* max, float* preferred);
									/*!< The heights for \a width, from its layout. */

	virtual	void				InvalidateLayout(bool descendants = false);
									/*!< Its sizes changed: the layouts it is in and its own
										must be computed again (with its descendants if
										\a descendants); its window lays out before the next
										frame. */
			void				EnableLayoutInvalidation();
									/*!< Undoes one DisableLayoutInvalidation(). */
			void				DisableLayoutInvalidation();
									/*!< Ignores InvalidateLayout() until enabled again. */
			bool				IsLayoutInvalidationDisabled() const;
									/*!< Whether invalidation is disabled. */
			bool				IsLayoutValid() const;
									/*!< Whether it is laid out. */
			void				ResetLayoutInvalidation();
									/*!< Its sizes are known to be valid again. */
			BLayoutContext*		LayoutContext() const;
									/*!< The layout pass running, or nullptr. */

			void				Layout(bool force);
									/*!< Lays out the view and its descendants where needed;
										with \a force all views, but a valid layout keeps
										its placement (as in Haiku). */
			void				Relayout();
									/*!< Lays it out again, now unless its parent is being
										laid out. */

	// Hooks

	virtual	void				AttachedToWindow();
									/*!< It came into a window; before its children. */
	virtual	void				AllAttached();
									/*!< It and its subtree came into a window; after its
										children. */
	virtual	void				DetachedFromWindow();
									/*!< It leaves a window; before its children. */
	virtual	void				AllDetached();
									/*!< It and its subtree left the window. */
	virtual	void				FrameMoved(gfx::Point newPosition);
									/*!< With FrameEvents: the frame moved. */
	virtual	void				FrameResized(float newWidth, float newHeight);
									/*!< With FrameEvents: the frame changed its size. */
	virtual	void				MouseDown(gfx::Point where);
									/*!< A button went down over it, \a where in its
										coordinates. */
	virtual	void				MouseUp(gfx::Point where);
									/*!< A button went up. */
	virtual	void				MouseMoved(gfx::Point where, BMouseTransit transit,
									const BMessage* dragMessage);
									/*!< The mouse moved; \a dragMessage is nullptr until
										drag and drop exists. */
	virtual	void				KeyDown(std::string_view bytes);
									/*!< A key went down while it had the focus; \a bytes is
										the text it types, possibly empty. By default Tab
										moves the focus (keyboard navigation). */
	virtual	void				KeyUp(std::string_view bytes);
									/*!< A key went up. */
	virtual	void				WindowActivated(bool active);
									/*!< Its window became active or inactive. */

protected:
	virtual	void				LayoutInvalidated(bool descendants);
									/*!< Hook: InvalidateLayout() was called. */
	virtual	void				DoLayout();
									/*!< Lays out its children: its layout's DoLayout() by
										default. */
	virtual	void				LayoutChanged();
									/*!< Hook: it and its descendants were laid out. */

private:
	friend class BWindow;
	friend class BLayout;
	friend class BLayoutItem;
	friend class BAbstractLayout;
	friend class BViewLayoutItem;

			BView*				_AddChild(std::unique_ptr<BView> child, BView* before);
			void				_RemoveLayoutItemsFromLayout();
			void				_RegisterLayoutItem(BLayoutItem* item);
			void				_DeregisterLayoutItem(BLayoutItem* item);
			void				_Layout(bool force, BLayoutContext* context);
			void				_InvalidateParentLayout();
			bool				_WillLayout() const;
			int					_HideLevel() const;
			void				_DropLayout();

			void				_ParentResized(float dw, float dh);
			void				_Attach(BWindow* window);
			void				_Detach();
			void				_Damage(const gfx::Rect& rect);
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

			// Layout, as Haiku's BView::LayoutData
			gfx::Size			fMinSize = kUnsetSize;
			gfx::Size			fMaxSize = kUnsetSize;
			gfx::Size			fPreferredSize = kUnsetSize;
			BAlignment			fAlignment;
			int					fLayoutInvalidationDisabled = 0;
			std::unique_ptr<BLayout> fLayout;
			BLayoutContext*		fLayoutContext = nullptr;
			std::vector<BLayoutItem*> fLayoutItems;
			bool				fLayoutValid = true;
			bool				fMinMaxValid = true;
			bool				fLayoutInProgress = false;
			bool				fNeedsRelayout = true;
};


} // namespace ngui
