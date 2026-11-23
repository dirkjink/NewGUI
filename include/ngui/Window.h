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
#include <vector>

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


/*!	How a window looks (Haiku's window_look, as far as the platform can
	show it). */
enum class BWindowLook : std::uint8_t {
	Titled,		/*!< B_TITLED_WINDOW_LOOK and B_DOCUMENT_WINDOW_LOOK */
	Bordered,	/*!< B_BORDERED_WINDOW_LOOK: a frame without a title bar. */
	NoBorder	/*!< B_NO_BORDER_WINDOW_LOOK */
};


/*!	Window options (Haiku's B_NOT_RESIZABLE ...). */
enum class BWindowFlag : std::uint32_t {
	NotResizable = 1u << 0,		/*!< B_NOT_RESIZABLE */
	NotClosable = 1u << 1,		/*!< B_NOT_CLOSABLE: the close button does nothing. */
	QuitOnWindowClose = 1u << 2	/*!< B_QUIT_ON_WINDOW_CLOSE: the application quits with
									it. */
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
								~BWindow() override;
									/*!< Destroys its views and closes its platform window. */

			std::expected<void, Error> UseBackend(std::unique_ptr<gfx::Backend> backend);
									/*!< The backend to render with, before the first frame;
										otherwise the one of BackendRegistry::Default() with
										the highest priority. */

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
									/*!< Resizes the content area. */
			const std::string&	Title() const;
									/*!< The title. */
			void				SetTitle(std::string_view title);
									/*!< Changes the title. */
			BWindowFlags		Flags() const;
									/*!< Its flags. */
			bool				IsActive() const;
									/*!< It has the focus of the window system. */

			gfx::DisplayListRef	DisplayList() const;
									/*!< The DisplayList of the last frame (for screenshots,
										specification 19.1); nullptr before the first. */
			std::uint64_t		FrameCount() const;
									/*!< Frames drawn so far. */
			void				UpdateIfNeeded();
									/*!< Draws a frame now if one is due, as Haiku's. */

	// Hooks

	virtual	void				FrameMoved(gfx::Point newPosition);
									/*!< The window moved. */
	virtual	void				FrameResized(float newWidth, float newHeight);
									/*!< The content area changed its size. */
	virtual	void				WindowActivated(bool active);
									/*!< It became active or inactive; then the views'
										hooks are called. */
			void				DispatchMessage(BMessage* message, BHandler* handler) override;
									/*!< The platform's events to the views, then as
										BLooper's. */

private:
	friend class BApplication;
	friend class BView;

			void				_AddDirty(const gfx::Rect& rect);
			void				_ViewDetached(BView* view);
			void				_SetFocus(BView* view);
			void				_SetMouseCapture(BView* view);
			void				_Update();
			void				_MouseEvent(BMessage* message);
			void				_KeyEvent(BMessage* message);
			void				_KeyboardNavigation();
			BView*				_FindNextNavigable(BView* focus, BViewFlags flags) const;
			BView*				_FindPreviousNavigable(BView* focus, BViewFlags flags) const;
			BView*				_LastViewChild(BView* parent) const;

	static	std::vector<BMessenger> _Windows();
			void				_Resized(float width, float height, float scale);

			std::string			fTitle;
			BWindowFlags		fFlags;
			std::unique_ptr<BView> fRoot;
			std::unique_ptr<platform::PlatformWindow> fPlatformWindow;
			std::unique_ptr<gfx::Backend> fBackend;
			gfx::SurfaceRef		fSurface;
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
			bool				fActive = false;
};


} // namespace ngui
