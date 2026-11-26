/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ngui/Accessibility.h>
#include <ngui/Bitmap.h>
#include <ngui/ControlLook.h>
#include <ngui/Error.h>
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
class BCursor;
class BLayoutContext;
class BMessage;
class BScrollBar;
class BScrollView;
class BToolTip;
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
	InvalidateAfterLayout = 1u << 9,	/*!< B_INVALIDATE_AFTER_LAYOUT: invalidated after
										each layout. */
	ScrollViewAware = 1u << 10		/*!< B_SCROLL_VIEW_AWARE: sets up the scroll bars of
										its BScrollView itself. */
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


/*!	What a snapshot of a window or view shows (specification 19.1). */
struct BSnapshotOptions {
			std::optional<gfx::Rect> area;
									/*!< The part in window (BView::Snapshot(): view)
										coordinates; none for all of it. */
			float				scale = 0;	/*!< Device pixels per logical pixel; 0 for
												the window's. */
			std::string			backend;	/*!< The backend that renders, by name; empty
												for the window's (CPU raster). */
			bool				includePopups = false;
									/*!< BWindow::Snapshot(): with the window's open menus
										and tooltips over it (the cursor never). */
};


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
									/*!< Sets the left top corner of Bounds(), rounded to
										whole pixels and kept within the ranges of its
										scroll bars; they follow. */
			BScrollBar*			ScrollBar(BOrientation direction) const;
									/*!< The scroll bar that scrolls it in \a direction, or
										nullptr. */
	virtual	void				TargetedByScrollView(BScrollView* scrollView);
									/*!< Hook: it became the target of \a scrollView
										(nullptr: no longer). */

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
										the coordinates of the window's root view; in a
										popup (BWindow::OpenPopup()) to the popup's. */
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

			const ControlLook&	Look() const;
									/*!< The look to draw and measure with: its window's look
										of this cycle (BWindow::Look()), or
										LookRegistry::DefaultLook() outside a window. */

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
			std::expected<BBitmap, Error> Snapshot(const BSnapshotOptions& options = {});
									/*!< The view and its children rendered anew into a
										bitmap (B_RGBA32), as the window's backend draws
										them (specification 19.1, level a): its bounds, or
										\a options' area in its coordinates, after pending
										layout and drawing. On the window's thread only;
										InvalidArgument without a window. */

	// Events

	virtual	void				MakeFocus(bool focus = true);
									/*!< Takes (or gives up) the keyboard focus of its
										window; the view that had it gets MakeFocus(false)
										first, as in Haiku. */
			bool				IsFocus() const;
									/*!< It has the focus. */
			void				SetMouseEventMask(BEventMaskFlags mask);
									/*!< In MouseDown(): \a mask (Pointer) sends it all mouse
										events until the buttons are up, as Haiku's. */

	// Cursor (module P2)

			void				SetViewCursor(const BCursor& cursor);
									/*!< The cursor shown while the mouse is over this view
										(not over its children, as in Haiku); an invalid
										cursor is ignored. */
			void				ClearViewCursor();
									/*!< NewGUI: the application's cursor over this view
										again. */
			const BCursor*		ViewCursor() const;
									/*!< NewGUI: the cursor of SetViewCursor(), or
										nullptr. */

	// Accessibility (toolkit scope 11.3, ngui/Accessibility.h)

	virtual	void				GetAccessibleInfo(AccessibleInfo* info) const;
									/*!< What it tells assistive technology. The view's
										own: role None (left out of the tree), the name and
										description set below (the description by default
										its text tooltip), Focusable if it is navigable,
										and Focus then. The toolkit's classes add their
										role, value, states and actions; a name set here
										wins over their label. */
	virtual	std::expected<void, Error> PerformAccessibleAction(AccessibleAction action,
									const BMessage& data);
									/*!< Does \a action of assistive technology, on the
										window's thread: Focus makes a navigable view the
										focus; the toolkit's classes do theirs (Click as
										Space does). NotSupported for an action it does
										not take, InvalidArgument without the data
										SetValue needs. */
	virtual	void				GetAccessibleItems(std::vector<AccessibleItem>* items) const;
									/*!< Its parts that are no views (list items, tabs,
										menu items), as nodes below it, before its child
										views; none by default. */
	virtual	std::expected<void, Error> PerformAccessibleItemAction(int32 index,
									AccessibleAction action, const BMessage& data);
									/*!< Does \a action for its item \a index, on the
										window's thread; NotSupported by default. */
			void				SetLiveRegion(AccessibleLive live);
									/*!< Whether a screen reader announces changes of its
										name (a status line: Polite; an error:
										Assertive). Off by default. */
			AccessibleLive		LiveRegion() const;
									/*!< As SetLiveRegion() set it. */
			void				SetAccessibleName(std::string_view name);
									/*!< Its name for assistive technology, for views
										without a label (an icon button, a text field with
										a label beside it); empty for the label. */
			const std::string&	AccessibleName() const;
									/*!< The name set, or empty. */
			void				SetAccessibleDescription(std::string_view description);
									/*!< More about it for assistive technology; empty
										for its text tooltip. */
			const std::string&	AccessibleDescription() const;
									/*!< The description set, or empty. */

	// Record & Replay (specification 20.8)

			void				SetInputPrivate(bool isPrivate);
									/*!< Whether what is typed into it, or into a view in
										it, is kept out of recordings (rule T5): its
										characters are recorded as bullets "\u2022" (and
										replayed so), its keys without their codes. For
										a credit card number, say; text views that hide
										typing (passwords) are private anyway. */
			bool				IsInputPrivate() const;
									/*!< Whether it is marked private for recordings. */

	// Tooltips (module W6b)

			void				SetToolTip(std::string_view text);
									/*!< A tooltip of \a text (BTextToolTip), shown when
										the mouse rests over the view; an empty text
										removes it. A text tooltip it has takes the new
										text. */
			void				SetToolTip(std::shared_ptr<BToolTip> tip);
									/*!< Its tooltip, which views may share; nullptr
										removes it (and hides it). */
			std::shared_ptr<BToolTip> ToolTip() const;
									/*!< Its tooltip, or nullptr. */
			void				ShowToolTip(std::shared_ptr<BToolTip> tip);
									/*!< Shows \a tip near the mouse, in a popup of its
										window; nothing for nullptr. From the window's
										thread. */
			void				HideToolTip();
									/*!< Hides the tooltip shown for it, after the hide
										delay. NewGUI: only its own (Haiku hid any). */
	virtual	bool				GetToolTipAt(gfx::Point point, std::shared_ptr<BToolTip>* tip);
									/*!< Hook: the tooltip for \a point (its coordinates),
										false for none. By default ToolTip(). */

	// Layout (module G10)

	virtual	BLayout*			SetLayout(std::unique_ptr<BLayout> layout);
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
									/*!< The mouse moved; while something is dragged from
										another application over the view, \a dragMessage
										is a kSimpleData message (without its content,
										which comes with the drop), otherwise nullptr. */
	virtual	void				KeyDown(std::string_view bytes);
									/*!< A key went down while it had the focus; \a bytes is
										the text it types, possibly empty. By default Tab
										moves the focus (keyboard navigation). */
	virtual	void				KeyUp(std::string_view bytes);
									/*!< A key went up. */
	virtual	void				WindowActivated(bool active);
									/*!< Its window became active or inactive. */
			void				MessageReceived(BMessage* message) override;
									/*!< The mouse wheel (kMouseWheelChanged) scrolls it with
										its scroll bars; without any, the message goes on
										to the next handler, its parent. A resting mouse
										(kMouseIdle) shows the tooltip of GetToolTipAt();
										without one, the message goes to the parent.
										Scripting: gets and sets "Frame" and "Hidden", gets
										"Name", counts "View". */
			BHandler*			ResolveSpecifier(BMessage* message, std::int32_t index,
									const BMessage& specifier, std::uint32_t form,
									std::string_view property) override;
									/*!< Its scripting properties "Frame", "Hidden", "Name"
										and the count of "View"; "View" by index, reverse
										index or name (FindView()) leads to that view. */
			std::expected<void, Error> GetSupportedSuites(BMessage* data) override;
									/*!< "suite/vnd.Be-view" and BHandler's. */

protected:
			bool				ScrollWithMouseWheelDelta(BScrollBar* scrollBar,
									float delta);
									/*!< Moves \a scrollBar by \a delta wheel steps: three
										small steps each, a large one with Shift. False for
										no bar or no delta. */
	virtual	void				LayoutInvalidated(bool descendants);
									/*!< Hook: InvalidateLayout() was called. */
	virtual	void				DoLayout();
									/*!< Lays out its children: its layout's DoLayout() by
										default. */
	virtual	void				LayoutChanged();
									/*!< Hook: it and its descendants were laid out. */

private:
	friend class BBox;
	friend class BScrollBar;
	friend class BWindow;
	friend class BLayout;
	friend class BLayoutItem;
	friend class BAbstractLayout;
	friend class BViewLayoutItem;

			BView*				_AddChild(std::unique_ptr<BView> child, BView* before);
			const BView*		_Root() const;
			bool				_IsSurfaceRoot() const;
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
			BScrollBar*			fVerScroller = nullptr;
			BScrollBar*			fHorScroller = nullptr;
			BResizingMode		fResizingMode;
			BViewFlags			fFlags;
			std::optional<gfx::Color> fViewColor;
			int					fHideLevel = 0;
			BView*				fParent = nullptr;
			BWindow*			fWindow = nullptr;
			std::vector<std::unique_ptr<BView>> fChildren;
			gfx::DisplayListRef	fList;
			gfx::DisplayListRef	fListAfterChildren;
			std::shared_ptr<BToolTip> fToolTip;
			bool				fInputPrivate = false;
			std::string			fAccessibleName;
			AccessibleLive		fLiveRegion = AccessibleLive::Off;
			std::string			fAccessibleDescription;
			std::unique_ptr<BCursor> fCursor;

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
