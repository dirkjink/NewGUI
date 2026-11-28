/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform {


class HeadlessPlatform;


/*!	A window of the HeadlessPlatform: no window system behind it. It keeps
	what is done to it and the last frame shown, and sends events to its
	BWindow the way a window system does (Send()). Thread-safe.
*/
class HeadlessWindow final : public PlatformWindow {
public:
	/*!	The last frame shown, as the window's CPU surface had it. */
	struct PresentedFrame {
			std::int32_t		width = 0;		/*!< Device pixels. */
			std::int32_t		height = 0;		/*!< Device pixels. */
			std::size_t			stride = 0;		/*!< Bytes per row. */
			gfx::PixelFormat	format = gfx::PixelFormat::Rgba8Premul;
												/*!< Byte order and alpha. */
			std::vector<std::byte> bytes;		/*!< The pixels. */
	};

								~HeadlessWindow() override;
									/*!< Leaves its platform's list. */

			void				SetTitle(std::string_view title) override;
									/*!< Keeps the title. */
			void				Show() override;
									/*!< Marks it shown. */
			void				Hide() override;
									/*!< Marks it hidden. */
			void				MoveTo(gfx::Point where) override;
									/*!< Moves its frame. */
			void				ResizeTo(gfx::Size size) override;
									/*!< Resizes its frame. */
			void				SetSizeLimits(gfx::Size min, gfx::Size max) override;
									/*!< Keeps the limits. */
			gfx::Rect			Frame() const override;
									/*!< Its content area on the virtual screen. */
			float				Scale() const override;
									/*!< The virtual screen's scale. */
			gfx::NativeWindow	Native() const override;
									/*!< Its size and scale, no handle: windows render
										into CPU surfaces. */
			void				SetTarget(BMessenger target) override;
									/*!< Where Send() sends. */
			void				Activate() override;
									/*!< Makes it the active window: kWindowActivated to
										it, and with "active" false to the one before. */
			std::expected<void, Error> Present(const gfx::CpuPixels& pixels,
									std::span<const gfx::IntRect> dirty) override;
									/*!< Keeps a copy of \a pixels as the last frame. */
			bool				IsAccessibilityActive() const override;
									/*!< As SetAccessibilityActive() set it; false at
										first. */
			void				UpdateAccessibility(const AccessibleTree& tree) override;
									/*!< Keeps \a tree as the last one and counts it. */
			bool				CanDragToSystem() const override;
									/*!< Options::systemDrag, for a window that is no
										popup. */
			std::expected<void, Error> StartSystemDrag(std::vector<DragItem> items) override;
									/*!< Keeps \a items as a drag to the system. */
			bool				HasSystemMenuBar() const override;
									/*!< Options::systemMenuBar, for a window that is no
										popup. */
			void				SetSystemMenuBar(std::vector<SystemMenuItem> menus) override;
									/*!< Keeps \a menus as the last ones and counts them. */
			void				SetFrameLayout(const WindowFrameLayout& layout) override;
									/*!< Keeps \a layout: Send() and the test driver place
										mouse positions by it. */

			std::string			Title() const;
									/*!< The title. */
			bool				IsShown() const;
									/*!< Whether Show() was called last. */
			bool				IsPopup() const;
									/*!< A popup (a menu or tooltip) of Parent(). */
			PlatformWindow*		Parent() const;
									/*!< The window of a popup, nullptr otherwise. */
			std::pair<gfx::Size, gfx::Size> SizeLimits() const;
									/*!< The limits set last. */
			BMessenger			Target() const;
									/*!< Where its events go. */
			std::shared_ptr<const PresentedFrame> LastFrame() const;
									/*!< The last frame shown, nullptr before the first. */
			std::size_t			CountFrames() const;
									/*!< How many frames were shown. */

			std::expected<void, Error> Send(const BMessage& message) const;
									/*!< Sends \a message to its BWindow as an event of the
										window system: input, kWindowResized ... */
			std::expected<void, Error> RequestClose() const;
									/*!< Asks it to close, as its close button does
										(kQuitRequested). */
			void				SetAccessibilityActive(bool active);
									/*!< Acts as assistive technology that begins to
										listen (kAccessibilityRequested to its BWindow)
										or stops, for tests. */
			std::optional<AccessibleTree> LastAccessibleTree() const;
									/*!< The tree its BWindow passed on last, nullopt
										before the first. */
			std::size_t			CountAccessibleUpdates() const;
									/*!< How many trees its BWindow passed on. */
			std::vector<std::vector<DragItem>> SystemDrags() const;
									/*!< The drags handed to the system, oldest first. */
			std::vector<SystemMenuItem> SystemMenuBar() const;
									/*!< The menus its BWindow passed on last for the
										system's menu bar. */
			std::size_t			CountSystemMenuBarUpdates() const;
									/*!< How many times it passed them on. */
			WindowFrameLayout	FrameLayout() const;
									/*!< The frame its BWindow draws, as it passed it on
										last; none (offset 0) for the window system's. */
			std::expected<void, Error> ChooseSystemMenuItem(std::uint64_t id) const;
									/*!< Acts as a user choosing the entry \a id in the
										system's menu bar: the menus open
										(kSystemMenusBeginning), the entry is chosen
										(kSystemMenuInvoked), the menus close
										(kSystemMenusEnded). */

private:
	friend class HeadlessPlatform;
			struct Shared;

								HeadlessWindow(std::shared_ptr<Shared> shared,
									const WindowSpec& spec, float scale);

			std::shared_ptr<Shared> fShared;
	mutable	std::mutex			fLock;
			std::string			fTitle;
			gfx::Rect			fFrame;
			float				fScale;
			gfx::Size			fMinSize = {0, 0};
			gfx::Size			fMaxSize = {0, 0};
			BMessenger			fTarget;
			PlatformWindow*		fParent;
			bool				fPopup;
			bool				fShown = false;
			std::shared_ptr<const PresentedFrame> fLastFrame;
			std::size_t			fFrames = 0;
			bool				fAccessibilityActive = false;
			std::optional<AccessibleTree> fAccessibleTree;
			std::size_t			fAccessibleUpdates = 0;
			std::vector<std::vector<DragItem>> fSystemDrags;
			std::vector<SystemMenuItem> fSystemMenus;
			std::size_t			fSystemMenuUpdates = 0;
			WindowFrameLayout	fFrameLayout;
};


/*!	A platform without a window system (specification 20.2), for tests and
	tools: windows render into CPU surfaces on a virtual screen of a given
	size and scale; a virtual clipboard; the system's file dialogs replaced
	by a placeholder that answers as the program says (by default as if
	cancelled). BApplication::UsePlatform("headless") or the environment
	variable NGUI_PLATFORM=headless choose it.

	Post() runs work on the main thread, as the events of a window system
	come there; HeadlessWindow::Send() delivers events to a window.
	Thread-safe.
*/
class HeadlessPlatform final : public Platform {
public:
	/*!	The virtual screen. */
	struct Options {
			gfx::Size			screen = {1920, 1080};
									/*!< Its size in logical pixels. */
			float				scale = 1;
									/*!< Device pixels per logical pixel. */
			gfx::Rect			usable = {0, 0, 0, 0};
									/*!< The part of the screen free of panels
										(DisplayInfo::usable); empty for all of it. */
			SystemColorScheme	colorScheme = SystemColorScheme::Light;
									/*!< The colors the user chose for the virtual
										system (ColorScheme()). */
			std::optional<gfx::Color> accentColor = std::nullopt;
									/*!< Its accent color (AccentColor()), none by
										default. */
			bool				systemDrag = false;
									/*!< Acts as a platform whose drags go on to other
										applications (PlatformWindow::CanDragToSystem()). */
			bool				systemMenuBar = false;
									/*!< Acts as a platform with a menu bar of the
										system (macOS): windows take their menus there
										(PlatformWindow::HasSystemMenuBar()). */
	};

	/*!	How the placeholder answers a file dialog. */
	using FileDialogAnswer = std::function<FileDialogResult(const FileDialogSpec&)>;

								HeadlessPlatform();
									/*!< A screen of 1920 × 1080 at scale 1. */
	explicit					HeadlessPlatform(const Options& options);
									/*!< The screen of \a options. */
								~HeadlessPlatform() override;
									/*!< Its windows must be gone. */

			std::string_view	Name() const override;
									/*!< "headless". */
			std::expected<std::unique_ptr<PlatformWindow>, Error> CreateWindow(
									const WindowSpec& spec) override;
									/*!< A HeadlessWindow. */
			std::vector<DisplayInfo> Displays() const override;
									/*!< The virtual screen. */
			void				ProcessEvents() override;
									/*!< Runs the work posted so far. */
			void				WaitForEvents() override;
									/*!< Waits for work or Wake(). */
			void				Wake() override;
									/*!< Ends a WaitForEvents(). */
			std::expected<void, Error> SetClipboard(const ClipboardData& data) override;
									/*!< Fills the virtual clipboard. */
			std::expected<ClipboardData, Error> Clipboard() const override;
									/*!< The virtual clipboard. */
			void				SetCursor(const CursorShape& cursor) override;
									/*!< Keeps the cursor. */
			void				SetCursorVisible(bool visible) override;
									/*!< Keeps whether it shows. */
			std::expected<void, Error> ShowFileDialog(const FileDialogSpec& spec,
									std::function<void(FileDialogResult)> done) override;
									/*!< The placeholder: \a done gets the answer of
										SetFileDialogAnswer() (by default none, as
										cancelled) on the main thread, later. */
			std::expected<void, Error> ShowNotification(const NotificationSpec& spec) override;
									/*!< Keeps \a spec as shown; one with the id of an
										earlier one takes its place. */
			bool				HasTransparentWindows() const override;
									/*!< True: its windows show whatever they draw. */
			SystemColorScheme	ColorScheme() const override;
									/*!< Options::colorScheme, or as SetColorScheme() set
										it. */
			std::optional<gfx::Color> AccentColor() const override;
									/*!< Options::accentColor, or as SetAccentColor() set
										it. */

			void				Post(std::function<void()> work);
									/*!< Runs \a work on the main thread in the next
										ProcessEvents(). */
			std::vector<HeadlessWindow*> Windows() const;
									/*!< Its windows, popups too, oldest first. The
										pointers are valid while the windows live. */
			HeadlessWindow*		ActiveWindow() const;
									/*!< The window activated last, nullptr if none. */
			CursorShape			Cursor() const;
									/*!< The cursor set last. */
			bool				CursorVisible() const;
									/*!< Whether the cursor shows. */
			void				SetColorScheme(SystemColorScheme scheme);
									/*!< Acts as a user switching the system to light or
										dark: kSystemColorsChanged to the application. */
			void				SetAccentColor(std::optional<gfx::Color> accent);
									/*!< Acts as a user choosing another accent color:
										kSystemColorsChanged to the application. */
			void				SetFileDialogAnswer(FileDialogAnswer answer);
									/*!< How file dialogs are answered from now on;
										nullptr: as cancelled. */
			std::vector<FileDialogSpec> FileDialogs() const;
									/*!< The file dialogs asked for so far. */
			std::vector<NotificationSpec> Notifications() const;
									/*!< The notifications shown, oldest first (one that
										replaced another in its place). */
			std::expected<void, Error> ClickNotification(std::size_t index) const;
									/*!< Acts as a user clicking the notification \a index
										of Notifications(): its onClick goes to its target.
										InvalidArgument for none or one without a click. */
			bool				IsIdle() const;
									/*!< No posted work waits or runs (for the test
										driver's WaitForIdle()). */
			std::uint64_t		Activity() const;
									/*!< Grows with every work posted and run: unchanged
										over a look, nothing happened. */

private:
	friend class HeadlessWindow;

			Options				fOptions;
			std::shared_ptr<HeadlessWindow::Shared> fShared;
};


} // namespace ngui::platform
