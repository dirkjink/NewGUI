/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/Flags.h>
#include <ngui/Looper.h>
#include <ngui/View.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/DisplayList.h>
#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/Rect.h>
#include <ngui/platform/Platform.h>


namespace ngui {


class BButton;
class BMenuBar;
class BMenuItem;
class BMessageRunner;
class MenuTracking;
class ToolTipPopup;


/*!	How a window looks (Haiku's window_look, as far as the platform can
	show it). */
enum class BWindowLook : std::uint8_t {
	Titled,		/*!< B_TITLED_WINDOW_LOOK and B_DOCUMENT_WINDOW_LOOK */
	Bordered,	/*!< B_BORDERED_WINDOW_LOOK: a frame without a title bar. */
	NoBorder	/*!< B_NO_BORDER_WINDOW_LOOK */
};


/*!	How a window behaves towards the other windows (Haiku's window_feel). */
enum class BWindowFeel : std::uint8_t {
	Normal,		/*!< B_NORMAL_WINDOW_FEEL */
	ModalApp	/*!< B_MODAL_APP_WINDOW_FEEL: while it is shown, the other windows of the
					application take no mouse and keyboard input; a click on one of them
					brings it to the front. Of several, the one shown last has the
					input. */
};


/*!	Window options (Haiku's B_NOT_RESIZABLE ...). */
enum class BWindowFlag : std::uint32_t {
	NotResizable = 1u << 0,		/*!< B_NOT_RESIZABLE */
	NotClosable = 1u << 1,		/*!< B_NOT_CLOSABLE: the close button does nothing. */
	QuitOnWindowClose = 1u << 2,	/*!< B_QUIT_ON_WINDOW_CLOSE: the application quits
									with it. */
	AutoUpdateSizeLimits = 1u << 3	/*!< B_AUTO_UPDATE_SIZE_LIMITS: the size limits follow
									the root view's minimum and maximum size. */
};
NGUI_FLAG_ENUM(BWindowFlag)

/*!	A set of BWindowFlag. */
using BWindowFlags = Flags<BWindowFlag>;


/*!	A window (module G9), as Haiku's BWindow: a looper with a window of the
	platform and a tree of views below its root view, which covers the
	content area. It distributes the platform's events to the views and
	draws them.

	Life: made on any thread before it runs, then handed to its thread with
	BLooper::Run() (CONVENTIONS.md 3.4); Show() makes it visible. Its
	platform window is created in the constructor if an application with a
	platform runs; without one the window still records and renders, for
	tests.

	Frames: an Invalidate() of a view, a change of the tree or of a frame
	asks for a frame. The window puts the views' DisplayLists together
	(specification 11.4), renders them with its backend into its window
	surface and hands the changed areas to the platform's presenter.

	Events, as in Haiku: mouse events go to the view under the mouse,
	unless a view asked for them with SetMouseEventMask() in MouseDown(),
	which then gets them until the buttons are up (controls track a click
	this way). Key events go to the focus view; Tab moves the focus through
	the views with BViewFlag::Navigable (a view's default KeyDown() does
	that, so views that use Tab themselves override it), Option+Tab through
	those with NavigableJump, Shift goes backwards. The close button asks
	QuitRequested().

	The application knows its windows: BApplication::QuitRequested() asks
	them, and windows left when the application ends are quit.
*/
class BWindow : public BLooper {
public:
								BWindow(gfx::Rect frame, std::string_view title,
									BWindowLook look = BWindowLook::Titled,
									BWindowFlags flags = {});
									/*!< A hidden window; \a frame is the content area on the
										desktop. */
								BWindow(gfx::Rect frame, std::string_view title,
									BWindowLook look, BWindowFeel feel, BWindowFlags flags = {});
									/*!< A hidden window with \a feel. */
								~BWindow() override;
									/*!< Destroys its views and closes its platform window. */

			std::expected<void, Error> UseBackend(std::unique_ptr<gfx::Backend> backend);
									/*!< The backend to render with, before the first frame;
										otherwise the one of BackendRegistry::Default() with
										the highest priority. The window adds the fonts of
										TextSystem::Default() to it (Backend::AddFont()). */

	// Views

			BView*				AddChild(std::unique_ptr<BView> child, BView* before = nullptr);
									/*!< Adds \a child to the root view. */
			std::unique_ptr<BView> RemoveChild(BView* child);
									/*!< Takes a child of the root view back. */
			std::int32_t		CountChildren() const;
									/*!< The children of the root view. */
			BView*				ChildAt(std::int32_t index) const;
									/*!< Child \a index of the root view. */
			BView*				FindView(std::string_view name) const;
									/*!< The first view called \a name. */
			BView*				FindView(gfx::Point where) const;
									/*!< The visible view deepest in the tree at \a where
										(content-area coordinates), the root view if no other;
										nullptr outside. */
			BView*				CurrentFocus() const;
									/*!< The view with the focus, nullptr if none. */
			BView*				RootView() const;
									/*!< The view that covers the content area. */

	// Look and default button (module W2)

			const ControlLook&	Look() const;
									/*!< The look of this cycle: taken at the start of each
										layout and drawing cycle (rule L10) from
										LookRegistry::DefaultLook(), and kept until the next
										one. */
			BButton*			DefaultButton() const;
									/*!< The button Enter invokes, or nullptr. */
			void				SetDefaultButton(BButton* button);
									/*!< Makes \a button (a view of this window) the default
										button, the old one an ordinary one; nullptr for
										none. */

	// Layout (module G10)

			BLayout*			SetLayout(std::unique_ptr<BLayout> layout);
									/*!< The layout of the root view (BView::SetLayout()). */
			BLayout*			GetLayout() const;
									/*!< The root view's layout, or nullptr. */
			BLayoutItem*		AddChild(std::unique_ptr<BLayoutItem> child);
									/*!< Adds \a child to the root view's layout. */
			void				InvalidateLayout(bool descendants = false);
									/*!< BView::InvalidateLayout() of the root view. */
			void				Layout(bool force);
									/*!< Updates the size limits (AutoUpdateSizeLimits) and
										lays out the views. Done before each frame in which
										a layout was invalidated. */
			void				ResizeToPreferred();
									/*!< Resizes to the root view's preferred size within its
										minimum and maximum. */
			void				SetSizeLimits(float minWidth, float maxWidth,
									float minHeight, float maxHeight);
									/*!< Limits the size of the content area (ignored if a
										minimum is above its maximum); resizes the window
										into the limits. */
			void				GetSizeLimits(float* minWidth, float* maxWidth,
									float* minHeight, float* maxHeight) const;
									/*!< The limits; each pointer may be nullptr. */
			void				UpdateSizeLimits();
									/*!< With AutoUpdateSizeLimits: the limits from the root
										view's minimum and maximum size. */

	// The window

			void				Show();
									/*!< Makes it visible; nested with Hide(), as in Haiku. */
			void				Hide();
									/*!< Hides it. */
			bool				IsHidden() const;
									/*!< Hidden. */
			gfx::Rect			Frame() const;
									/*!< The content area on the desktop. */
			gfx::Rect			Bounds() const;
									/*!< The content area in its own coordinates. */
			void				MoveTo(gfx::Point where);
									/*!< Moves the content area's left top corner. */
			void				ResizeTo(float width, float height);
									/*!< Resizes the content area, within the size limits. */
			const std::string&	Title() const;
									/*!< The title. */
			void				SetTitle(std::string_view title);
									/*!< Changes the title. */
			BWindowFlags		Flags() const;
									/*!< Its flags. */
			bool				IsActive() const;
									/*!< It has the focus of the window system. */
			void				Activate(bool active = true);
									/*!< Brings it to the front with the focus (Haiku: false
										gives the focus away; NewGUI: does nothing). */
			void				SetFeel(BWindowFeel feel);
									/*!< How it behaves towards the other windows. */
			BWindowFeel			Feel() const;
									/*!< Its feel. */

			gfx::DisplayListRef	DisplayList() const;
									/*!< The DisplayList of the last frame (for screenshots,
										specification 19.1); nullptr before the first. */
			std::uint64_t		FrameCount() const;
									/*!< Frames drawn so far. */
			void				UpdateIfNeeded();
									/*!< Draws a frame now if one is due, as Haiku's. */

	// Keyboard shortcuts

			void				AddShortcut(std::uint32_t key, BModifierFlags modifiers,
									std::unique_ptr<BMessage> message);
									/*!< Sends \a message to the window when \a key is typed
										with \a modifiers; Command is always needed, unless
										BModifier::NoCommand is given. Replaces a shortcut
										of the same keys. */
			void				AddShortcut(std::uint32_t key, BModifierFlags modifiers,
									std::unique_ptr<BMessage> message, BHandler* target);
									/*!< As above, to \a target; nullptr for the focus view
										(or the window without one). */
			void				RemoveShortcut(std::uint32_t key, BModifierFlags modifiers);
									/*!< Removes the shortcut; Command+Q too, which is not a
										real one. */
			bool				HasShortcut(std::uint32_t key, BModifierFlags modifiers) const;
									/*!< Whether there is such a shortcut. */

			void				SetKeyMenuBar(BMenuBar* bar);
									/*!< The menu bar Command+Escape opens (a menu bar
										sets itself when attached). */
			BMenuBar*			KeyMenuBar() const;
									/*!< The menu bar Command+Escape opens, or nullptr. */

	// Popups (NewGUI): menus and tooltips of the window

			BView*				OpenPopup(std::unique_ptr<BView> view);
									/*!< Shows \a view in a popup of the window, at its frame,
										which is on the screen: a surface without frame
										above the window that gets no keyboard focus. It
										is drawn and gets its mouse events in the
										window's thread; moving or resizing \a view moves
										or resizes the popup. From the window's thread. */
			std::unique_ptr<BView> ClosePopup(BView* view);
									/*!< Closes the popup of \a view and gives the view back;
										nullptr if \a view has no popup. */
			std::int32_t		CountPopups() const;
									/*!< The open popups. */
			BView*				PopupAt(std::int32_t index) const;
									/*!< The view of popup \a index, or nullptr. */

	// Hooks

	virtual	void				FrameMoved(gfx::Point newPosition);
									/*!< The window moved. */
	virtual	void				FrameResized(float newWidth, float newHeight);
									/*!< The content area changed its size. */
	virtual	void				WindowActivated(bool active);
									/*!< It became active or inactive; then the views'
										hooks are called. */
	virtual	void				MenusBeginning();
									/*!< Hook: a menu of the window is about to open. */
	virtual	void				MenusEnded();
									/*!< Hook: the menus of the window have closed. */
			void				DispatchMessage(BMessage* message, BHandler* handler) override;
									/*!< The platform's events to the views, then as
										BLooper's. */

private:
	friend class BApplication;
	friend class BView;

			class PopupSurface;
			struct Shortcut;
	friend class MenuTracker;
	friend class BMenuItem;
	friend class BAlert;
	friend class BPopUpMenu;
	friend class BTextView;
	friend class BToolTipManager;
	friend class ToolTipPopup;

			void				_AddDirty(const gfx::Rect& rect);
			void				_AddDirty(const BView* root, const gfx::Rect& rect);
			gfx::Point			_SurfaceOrigin(const BView* root) const;
			void				_RootFrameChanged(BView* root);
			PopupSurface*		_PopupOf(const BView* root) const;
			void				_PopupEvent(PopupSurface* popup, BMessage* message);
			void				_WheelToMenus(const BMessage* message);
			BView*				_OpenPopup(std::unique_ptr<BView> view, bool tooltip);
			void				_MouseMovedForToolTip(gfx::Point screen);
			void				_MouseIdleCheck();
			void				_CheckToolTipOwner(const BView* under);
			void				_UpdateCursor(const BView* under);
			void				_DropEvent(BMessage* message);
			void				_DragMoved(gfx::Point where);
			void				_ViewCursorChanged(const BView* view);
			void				_UpdatePopup(PopupSurface& popup,
									const gfx::Caps& caps, const TextSystem& text);
			void				_ScheduleUpdate();
	static	BWindow*			_DispatchingWindow();
			void				_SetTextInputArea(gfx::Rect rect, int32 cursor);
			void				_ClearComposition();
			void				_DisposeLater(std::unique_ptr<BView> view);
			bool				_HandleKeyDown(BMessage* event);
			void				_AddShortcut(std::uint32_t* key, BModifierFlags* modifiers,
									BMenuItem* item);
			Shortcut*			_FindShortcut(std::uint32_t key, BModifierFlags modifiers) const;
			void				_ViewDetached(BView* view);
			void				_SetFocus(BView* view);
			void				_SetMouseCapture(BView* view);
			void				_Update();
			void				_MouseEvent(BMessage* message, BView* root);
			void				_KeyEvent(BMessage* message);
			void				_KeyboardNavigation();
			BView*				_FindNextNavigable(BView* focus, BViewFlags flags) const;
			BView*				_FindPreviousNavigable(BView* focus, BViewFlags flags) const;
			BView*				_LastViewChild(BView* parent) const;

	static	std::vector<BMessenger> _Windows();
			void				_UpdateModal();
			bool				_BlockedByModal(BMessenger* modal) const;
			void				_Resized(float width, float height, float scale);
			void				_LayoutInvalidated();
			void				_TakeLook();

			std::string			fTitle;
			BWindowFlags		fFlags;
			BWindowFeel			fFeel = BWindowFeel::Normal;
			std::unique_ptr<BView> fRoot;
			std::unique_ptr<platform::PlatformWindow> fPlatformWindow;
			std::unique_ptr<gfx::Backend> fBackend;
			gfx::SurfaceRef		fSurface;
			const TextSystem*	fFontsOf = nullptr;
									// The text system whose fonts fBackend has.
			gfx::Rect			fFrame;
			float				fScale = 1;
			std::vector<gfx::Rect> fDirty;
			gfx::DisplayListRef	fLastFrame;
			std::uint64_t		fFrameCount = 0;
			BView*				fFocus = nullptr;
			BView*				fMouseView = nullptr;
			BView*				fCapture = nullptr;
			std::uint32_t		fButtons = 0;
			int					fHideLevel = 1;
			bool				fUpdatePending = false;
			std::shared_ptr<const ControlLook> fLook;
			BButton*			fDefaultButton = nullptr;
			gfx::Size			fMinSize = {0, 0};
			gfx::Size			fMaxSize = {kSizeUnlimited, kSizeUnlimited};
			bool				fActive = false;
			std::vector<std::unique_ptr<PopupSurface>> fPopups;
			MenuTracking*		fMenuTracking = nullptr;
									// the menu tracking that gets the events, while it runs
			std::vector<std::unique_ptr<Shortcut>> fShortcuts;
			BMenuBar*			fKeyMenuBar = nullptr;
			bool				fNoQuitShortcut = false;
			std::optional<gfx::Point> fLastMouse;
									// on the screen, from the last mouse event
			std::vector<std::unique_ptr<BView>> fDisposals;
									// destroyed after the message being dispatched
			const BView*		fCursorView = nullptr;
			std::vector<std::string> fDropPaths;
			std::string			fDropText;
			bool				fDropTextSeen = false;
			BView*				fDragView = nullptr;
									// the view a drag from outside is over
									// the view under the mouse whose cursor shows
			std::unique_ptr<ToolTipPopup> fToolTip;
									// the tooltip shown, once one was
			std::unique_ptr<BMessageRunner> fIdleRunner;
			Clock::Time			fLastMove{0};
			bool				fIdlePending = false;
									// the mouse moved and has not rested yet
			bool				fMainUpdateWanted = true;
									// The content area needs a frame (not only popups).
};


} // namespace ngui
