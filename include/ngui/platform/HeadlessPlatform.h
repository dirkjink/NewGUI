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
			void				SetFileDialogAnswer(FileDialogAnswer answer);
									/*!< How file dialogs are answered from now on;
										nullptr: as cancelled. */
			std::vector<FileDialogSpec> FileDialogs() const;
									/*!< The file dialogs asked for so far. */

private:
	friend class HeadlessWindow;

			Options				fOptions;
			std::shared_ptr<HeadlessWindow::Shared> fShared;
};


} // namespace ngui::platform
