/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Messenger.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/IntRect.h>
#include <ngui/gfx/NativeWindow.h>
#include <ngui/gfx/Point.h>
#include <ngui/gfx/Rect.h>
#include <ngui/gfx/Size.h>


namespace ngui::platform {


/*!	A screen (module P1). */
struct DisplayInfo {
			gfx::Rect			frame;		/*!< Its area on the desktop, logical pixels. */
			float				scale = 1;	/*!< Device pixels per logical pixel. */
			bool				primary = false;	/*!< The main screen. */
};


class PlatformWindow;


/*!	What a new window looks like (module P1). Look and feel of BWindow
	map onto it (G9). */
struct WindowSpec {
			std::string			title;					/*!< The title. */
			gfx::Rect			frame = {0, 0, 0, 0};	/*!< The content area, logical
															pixels on the desktop. */
			bool				decorated = true;		/*!< With the frame of the window
															system. */
			bool				resizable = true;		/*!< The user may resize it. */
			BMessenger			target;					/*!< Where its events go. */
			PlatformWindow*		parent = nullptr;		/*!< For a popup: the window it
															belongs to, which must
															outlive it. */
			bool				popup = false;			/*!< A popup (a menu, a tooltip)
															of \a parent: without frame,
															above it, without the
															keyboard focus; #frame is
															still on the desktop. */
			bool				tooltip = false;		/*!< With #popup: a tooltip, which
															takes no input (the mouse
															reaches what is below). */
};


/*!	A window of the window system (module P1). Its events go to its target
	as messages (AppDefs.h: kMouseDown, kWindowResized ...). All methods may
	be called from any thread; an implementation forwards them to the main
	thread where the window system needs that.
*/
class PlatformWindow {
public:
	virtual						~PlatformWindow() = default;
									/*!< Closes the window. */

	virtual	void				SetTitle(std::string_view title) = 0;
									/*!< Changes the title. */
	virtual	void				Show() = 0;
									/*!< Makes it visible. */
	virtual	void				Hide() = 0;
									/*!< Hides it. */
	virtual	void				MoveTo(gfx::Point where) = 0;
									/*!< Moves the content area's left top corner. */
	virtual	void				ResizeTo(gfx::Size size) = 0;
									/*!< Changes the size of the content area. */
	virtual	void				SetSizeLimits(gfx::Size min, gfx::Size max) = 0;
									/*!< The smallest and largest size the user may give the
										content area (BWindow::SetSizeLimits()); sizes of
										kSizeUnlimited or more mean no limit. */
	virtual	gfx::Rect			Frame() const = 0;
									/*!< The content area, logical pixels on the desktop. */
	virtual	float				Scale() const = 0;
									/*!< Device pixels per logical pixel. */
	virtual	gfx::NativeWindow	Native() const = 0;
									/*!< For Backend::CreateWindowSurface(). */
	virtual	void				SetTarget(BMessenger target) = 0;
									/*!< Where its events go from now on. */
	virtual	void				Activate();
									/*!< Raises it and gives it the focus, as far as the
										window system lets a program do that. Nothing by
										default. */
	virtual	void				SetTextInputArea(gfx::Rect rect, int32 cursor);
									/*!< Where text is typed, in window coordinates, and the
										cursor's offset in it: input methods show their
										candidates there. Nothing by default. */
	virtual	void				ClearComposition();
									/*!< Ends an input method's composition, dropping its
										text. Nothing by default. */
	virtual	std::expected<void, Error> Present(const gfx::CpuPixels& pixels,
									std::span<const gfx::IntRect> dirty) = 0;
									/*!< Shows the \a dirty device-pixel areas of \a pixels,
										a CPU surface of the window's size (the CPU
										presenter, specification 12). The pixels are copied
										before it returns. */
};


/*!	What the system clipboard holds: data per MIME type, e.g. "text/plain"
	with UTF-8 text. */
using ClipboardData = std::vector<std::pair<std::string, std::vector<std::byte>>>;


/*!	A cursor image: RGBA bytes, not premultiplied, rows without padding. */
struct CursorImage {
			std::int32_t		width = 0;		/*!< Width in pixels. */
			std::int32_t		height = 0;		/*!< Height in pixels. */
			gfx::Point			hotspot = {0, 0};	/*!< The pixel that points. */
			std::vector<std::uint8_t> rgba;		/*!< `width * height * 4` bytes. */

			bool				operator==(const CursorImage& other) const = default;
									/*!< Same size, hot spot and pixels. */
};


/*!	The mouse cursor to show: a system cursor by Haiku's BCursorID value, or
	an image. */
struct CursorShape {
			std::uint8_t		system = 1;		/*!< A BCursorID value; ignored with an
													image. */
			std::shared_ptr<const CursorImage> image;
												/*!< The image, or nullptr. */

			bool				operator==(const CursorShape& other) const;
									/*!< The same system cursor, or images with the same
										pixels. */
};


/*!	The window system (module P1): windows, screens and the events of the
	main thread. BApplication::UsePlatform() hands it to the application,
	whose loop then waits for the platform's events and its own messages
	together, on the main thread.

	CreateWindow() and Displays() may be called from any thread;
	ProcessEvents() and WaitForEvents() only on the main thread; Wake()
	from any thread.
*/
class Platform {
public:
	virtual						~Platform() = default;
									/*!< Default. */

	virtual	std::string_view	Name() const = 0;
									/*!< Its id, e.g. "sdl3", "headless". */
	virtual	std::expected<std::unique_ptr<PlatformWindow>, Error> CreateWindow(
									const WindowSpec& spec) = 0;
									/*!< A new, hidden window. */
	virtual	std::vector<DisplayInfo> Displays() const = 0;
									/*!< The screens, the primary one first. */

	virtual	void				ProcessEvents() = 0;
									/*!< Delivers the pending events as messages and runs
										the work queued for the main thread; does not wait. */
	virtual	void				WaitForEvents() = 0;
									/*!< Blocks until there is an event or Wake() is called. */
	virtual	void				Wake() = 0;
									/*!< Ends WaitForEvents() early; from any thread. */

	virtual	std::expected<void, Error> SetClipboard(const ClipboardData& data);
									/*!< Puts \a data on the system clipboard; from any
										thread. NotSupported by default. */
	virtual	std::expected<ClipboardData, Error> Clipboard() const;
									/*!< What the system clipboard holds; from any thread.
										NotSupported by default. */

	virtual	void				SetCursor(const CursorShape& cursor);
									/*!< Shows \a cursor for the mouse; from any thread.
										Nothing by default. */
	virtual	void				SetCursorVisible(bool visible);
									/*!< Shows or hides the mouse cursor; from any thread.
										Nothing by default. */
};


/*!	The platform of the running application, nullptr without one. Valid
	while BApplication::Run() runs; windows must be closed before it
	returns. */
Platform*						Current();


} // namespace ngui::platform
