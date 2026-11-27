/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ngui/Accessibility.h>
#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/SupportDefs.h>
#include <ngui/gfx/Backend.h>
#include <ngui/gfx/Color.h>
#include <ngui/gfx/DisplayList.h>
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


/*!	The pixels a window shows (PlatformWindow::ReadShownFrame()):
	premultiplied RGBA, 8 bits each, the top row first, without padding. */
struct ShownFrame {
			std::int32_t		width = 0;		/*!< Width in device pixels. */
			std::int32_t		height = 0;		/*!< Height in device pixels. */
			std::vector<std::byte> rgba;		/*!< `width * height * 4` bytes. */
};


/*!	An entry of a system menu bar (macOS, module P3): an item of the
	window's key menu bar as the platform shows it. */
struct SystemMenuItem {
			std::uint64_t		id = 0;		/*!< What kSystemMenuInvoked names. */
			std::string			label{};	/*!< Its label; empty for a separator. */
			bool				separator = false;	/*!< A separator line. */
			bool				enabled = true;		/*!< It can be chosen. */
			bool				marked = false;		/*!< It has a check mark. */
			std::uint32_t		shortcut = 0;	/*!< The character of its shortcut, 0
												if none. */
			BModifierFlags		modifiers{};	/*!< Its modifiers as BMenuItem::Shortcut()
												gives them: with Command unless
												BModifier::NoCommand is among them. */
			std::vector<SystemMenuItem> items{};	/*!< The items of its submenu. */

			bool				operator==(const SystemMenuItem& other) const = default;
									/*!< Everything the same. */
};


/*!	A system menu item was chosen: to the window, with its id in "id"
	(int64). */
inline constexpr std::uint32_t kSystemMenuInvoked = FourCharCode("_SMI");

/*!	A menu of the system menu bar is about to open: to the window, which
	calls MenusBeginning() and passes its menus on again. */
inline constexpr std::uint32_t kSystemMenusBeginning = FourCharCode("_SMB");

/*!	The menus of the system menu bar have closed: to the window, which calls
	MenusEnded(). */
inline constexpr std::uint32_t kSystemMenusEnded = FourCharCode("_SME");


/*!	Data that goes to or comes from another application by drag & drop
	(module P4): a MIME type and its bytes, such as "text/plain" (UTF-8) or
	"text/uri-list" (file:// URLs, one per line). */
struct DragItem {
			std::string			type;		/*!< The MIME type. */
			std::vector<std::byte> data{};	/*!< The bytes. */

			bool				operator==(const DragItem& other) const = default;
									/*!< Same type and bytes. */
};


/*!	Data dropped from another application, all of it at once: to the
	window, between kDropBegin/kDropPosition and kDropComplete, with "where"
	and each item as a field of its MIME type (BTypeCode::Mime). */
inline constexpr std::uint32_t kDropData = FourCharCode("_DRD");


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
			bool				transparent = false;	/*!< Without #decorated: where it
															draws no color (alpha 0), what
															is behind it shows; for the
															frames NewGUI draws
															(Platform::
															HasTransparentWindows()). */
};


/*!	What the window system does with a press on an area of the frame NewGUI
	draws (SDL's hit test): the window moves or resizes, or the window gets
	the press. */
enum class WindowFrameHit : std::uint8_t {
	Normal,				/*!< The window gets the mouse (a button). */
	Move,				/*!< The window system moves the window. */
	ResizeLeft,			/*!< The window system resizes it at this side ... */
	ResizeRight,
	ResizeTop,
	ResizeBottom,
	ResizeTopLeft,		/*!< ... or corner. */
	ResizeTopRight,
	ResizeBottomLeft,
	ResizeBottomRight
};


/*!	An area of the frame and what a press on it does. */
struct WindowFrameArea {
			gfx::Rect			rect;					/*!< In window coordinates. */
			WindowFrameHit		hit = WindowFrameHit::Move;	/*!< What a press does. */

			bool				operator==(const WindowFrameArea& other) const = default;
									/*!< Same area, same action. */
};


/*!	The frame NewGUI draws around a window's content (BWindowFrames::Look),
	for the platform: where the content lies in the window and which areas
	the frame covers (the rest is transparent). */
struct WindowFrameLayout {
			gfx::Point			contentOffset = {0, 0};
												/*!< The content's left top corner in
													the window. */
			gfx::Size			contentSize = {0, 0};
												/*!< The content's size; 0 × 0 without
													such a frame. */
			std::vector<gfx::Rect> shape;		/*!< The frame's areas in window
													coordinates (borders, tab). */
			std::vector<WindowFrameArea> areas;	/*!< What a press does where: the
													first area containing the point
													counts; the content and the rest
													are Normal. */
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
	virtual	bool				IsAccessibilityActive() const;
									/*!< Whether assistive technology (a screen reader)
										listens to the window: then the window passes its
										accessibility tree on when it changes. False by
										default. A platform that begins to listen sends
										kAccessibilityRequested to the target; actions come
										as kAccessibleAction. */
	virtual	void				UpdateAccessibility(const AccessibleTree& tree);
									/*!< The window's accessibility tree, from the window's
										thread, while IsAccessibilityActive(). Nothing by
										default. */
	virtual	std::expected<void, Error> Present(const gfx::CpuPixels& pixels,
									std::span<const gfx::IntRect> dirty) = 0;
									/*!< Shows the \a dirty device-pixel areas of \a pixels,
										a CPU surface of the window's size (the CPU
										presenter, specification 12). The pixels are copied
										before it returns. */

	// The platform renders the frames (phase G): the window hands over its
	// DisplayList instead of pixels.

	virtual	bool				DrawsLists() const;
									/*!< Whether the platform renders this window's frames
										itself (e.g. on the GPU), so that the window calls
										PresentList() instead of Present(); false by
										default. */
	virtual	std::expected<void, Error> PresentList(gfx::DisplayListRef frame,
									const gfx::Color& clear);
									/*!< Shows \a frame, the whole content area in logical
										pixels, on \a clear, rendered by the platform at
										the window's scale; from the window's thread, the
										list is kept. NotSupported by default. */
	virtual	std::expected<ShownFrame, Error> ReadShownFrame();
									/*!< What the window shows of the last PresentList()
										(BWindow::CaptureFrame()). NotSupported by
										default. */

	virtual	bool				CanDragToSystem() const;
									/*!< Whether a drag can go on to other applications
										(StartSystemDrag()); false by default. */
	virtual	std::expected<void, Error> StartSystemDrag(std::vector<DragItem> items);
									/*!< Hands a drag that left the application's windows
										to the system: \a items go to the program it is
										dropped on, from the main thread, while the mouse
										button is down (module P4). The window then gets a
										kMouseUp when it ends. NotSupported by default. */

	virtual	bool				HasSystemMenuBar() const;
									/*!< Whether the window's menus belong in a menu bar of
										the system (macOS, module P3): its key menu bar then
										hides and passes its menus on. False by default. */
	virtual	void				SetSystemMenuBar(std::vector<SystemMenuItem> menus);
									/*!< The window's menus for the system menu bar, empty
										for none; shown while the window is the one in
										front. Ignored by default. */
	virtual	void				SetFrameLayout(const WindowFrameLayout& layout);
									/*!< The frame its BWindow draws around the content
										(BWindowFrames::Look): mouse positions come in
										window coordinates, the content at
										layout.contentOffset. Ignored by default. */
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


/*!	A file dialog of the system (module P2): what to ask for. */
struct FileDialogSpec {
	/*!	What the dialog picks. */
	enum class Kind : std::uint8_t {
		Open,	/*!< Existing files. */
		Save,	/*!< A file to save to, existing or not. */
		Folder	/*!< Existing folders. */
	};

			Kind				kind = Kind::Open;	/*!< What it picks. */
			std::string			title;				/*!< The title, empty for the
														system's. */
			std::string			acceptLabel;		/*!< The accepting button, empty for
														the system's. */
			std::string			cancelLabel;		/*!< The cancelling button, empty
														for the system's. */
			std::string			location;			/*!< The folder (or for Save, the
														file) to start at; may be
														empty. */
			std::vector<std::pair<std::string, std::string>> filters;
													/*!< Names and patterns
														("Images", "png;jpg"); empty
														for all files. */
			bool				many = false;		/*!< Open, Folder: more than one may
														be picked. */
			PlatformWindow*		parent = nullptr;	/*!< The window it is modal to, or
														nullptr. */
};


/*!	What a file dialog gives back: the chosen paths (UTF-8), empty if it was
	cancelled. */
using FileDialogResult = std::expected<std::vector<std::string>, Error>;


/*!	A notification of the system (module P5): what BNotification::Send()
	hands on. */
struct NotificationSpec {
	/*!	How it matters (Haiku's notification_type). */
	enum class Kind : std::uint8_t {
		Information,	/*!< Something to know. */
		Important,		/*!< Something to see soon. */
		Error,			/*!< Something that went wrong. */
		Progress		/*!< How far a task has come (progress). */
	};

			Kind				kind = Kind::Information;
												/*!< How it matters. */
			std::string			application;	/*!< The name of the sending
													application. */
			std::string			group;			/*!< The group it shows in, empty for
													the application's. */
			std::string			title;			/*!< The headline. */
			std::string			content;		/*!< The text. */
			std::string			id;				/*!< Replaces the one shown before with
													the same id; empty for a new one. */
			float				progress = 0;	/*!< Progress: 0 to 1. */
			std::int32_t		iconWidth = 0;	/*!< The icon's width in pixels. */
			std::int32_t		iconHeight = 0;	/*!< The icon's height in pixels. */
			std::vector<std::uint8_t> icon;		/*!< The icon: RGBA bytes, not
													premultiplied, rows without
													padding; empty for none. */
			std::chrono::milliseconds timeout{-1};
												/*!< How long it shows, negative for
													as long as the system likes. */
			BMessenger			target;			/*!< Where a click goes. */
			std::optional<BMessage> onClick;	/*!< Sent to target when the user
													clicks it; nullopt for no click. */
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

	virtual	std::expected<void, Error> ShowFileDialog(const FileDialogSpec& spec,
									std::function<void(FileDialogResult)> done);
									/*!< Shows the system's file dialog and returns at once;
										\a done gets the result later, on any thread. From
										any thread. NotSupported by default. */

	virtual	bool				HasTransparentWindows() const;
									/*!< Whether windows may be transparent (WindowSpec::
										transparent), for the frames NewGUI draws. False
										by default. */
	virtual	std::expected<void, Error> ShowNotification(const NotificationSpec& spec);
									/*!< Hands \a spec to the system's notifications and
										returns at once; a click later sends its onClick to
										its target. From any thread. NotSupported by
										default. */
};


/*!	The platform of the running application, nullptr without one. Valid
	while BApplication::Run() runs; windows must be closed before it
	returns. */
Platform*						Current();


} // namespace ngui::platform
