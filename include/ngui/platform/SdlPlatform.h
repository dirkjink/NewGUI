/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/platform/Platform.h>
#include <ngui/platform/PlatformRegistry.h>


namespace ngui::platform {


namespace sdl3 {
class PlatformState;
}


/*!	The platform on SDL3 (module P2), for Linux (X11, Wayland) and Windows;
	part of the library ngui_sdl3, built with NGUI_WITH_SDL3. This header
	includes no SDL header.

	SDL wants its video functions on one thread, the main thread: Create()
	must be called there, and so must the application's Run(). Calls from
	other threads (CreateWindow(), the methods of the windows) are handed
	to the main thread; CreateWindow() waits for its result, so the main
	thread must be running the application's loop then.

	Events become the messages of AppDefs.h. "key" is the USB HID usage of
	the key (the keyboard page, as SDL's scancodes); "bytes" is the text a
	key types, taken from the text input that follows it.
*/
class SdlPlatform final : public Platform {
public:
	static	std::expected<std::unique_ptr<SdlPlatform>, Error> Create();
									/*!< Initializes SDL's video on the calling thread, the
										main thread; NotSupported if SDL cannot. */
	static	bool				Register(PlatformRegistry& registry, int priority = 100);
									/*!< Adds "sdl3" to \a registry (normally
										PlatformRegistry::Default()); false if it is there
										already. */
								~SdlPlatform() override;
									/*!< Shuts SDL down; all windows must be gone. */

			std::string_view	Name() const override;
									/*!< "sdl3". */
			std::string_view	VideoDriver() const;
									/*!< SDL's video driver: "x11", "wayland", "windows",
										"offscreen" ... */
			std::expected<std::unique_ptr<PlatformWindow>, Error> CreateWindow(
									const WindowSpec& spec) override;
									/*!< See Platform. */
			std::vector<DisplayInfo> Displays() const override;
									/*!< See Platform. */
			void				ProcessEvents() override;
									/*!< See Platform. */
			void				WaitForEvents() override;
									/*!< See Platform. */
			void				Wake() override;
			std::expected<void, Error> SetClipboard(const ClipboardData& data) override;
									/*!< SDL's clipboard: text with SDL_SetClipboardText(),
										other types with SDL_SetClipboardData(). */
			std::expected<ClipboardData, Error> Clipboard() const override;
									/*!< SDL's clipboard: "text/plain" from
										SDL_GetClipboardText(), the other types as
										offered. */
									/*!< See Platform. */

			void				SetCursor(const CursorShape& cursor) override;
									/*!< SDL's system cursor nearest to the BCursorID, or a
										color cursor of the image; the transparent pixel for
										NoCursor. */
			void				SetCursorVisible(bool visible) override;
									/*!< SDL_ShowCursor() or SDL_HideCursor(). */
			std::expected<void, Error> ShowFileDialog(const FileDialogSpec& spec,
									std::function<void(FileDialogResult)> done) override;
									/*!< SDL_ShowFileDialogWithProperties(): the system's
										dialog (Windows' and macOS' own; under Linux the
										xdg-desktop-portal's, without one zenity's). */
			SystemColorScheme	ColorScheme() const override;
									/*!< SDL_GetSystemTheme(): under Linux the desktop's
										portal (org.freedesktop.appearance color-scheme),
										under Windows and macOS their own setting;
										SDL_EVENT_SYSTEM_THEME_CHANGED becomes
										kSystemColorsChanged. */
			std::optional<gfx::Color> AccentColor() const override;
									/*!< Under Linux the desktop's portal
										(org.freedesktop.appearance accent-color), under
										Windows the desktop window manager's (the registry),
										under macOS NSColor's controlAccentColor; a change
										becomes kSystemColorsChanged. Asked on first
										use. */
			bool				HasTransparentWindows() const override;
									/*!< Under Wayland, Windows and macOS; under X11 while a
										compositing manager runs; not with the offscreen
										driver. */
			std::expected<void, Error> ShowNotification(const NotificationSpec& spec) override;
									/*!< The system's notifications: under Linux
										org.freedesktop.Notifications on the session bus
										(NotSupported without one), under Windows a
										balloon of an icon in the notification area
										(Shell_NotifyIcon(), shown as a toast), under
										macOS UNUserNotificationCenter, in an
										application bundle only (NotSupported
										otherwise). */

			void				SetGpuRendering(bool gpu);
									/*!< Whether windows made from now on render on the GPU
										(phase G): the main thread draws their lists
										through an OpenGL context of each window with the
										backend of the BackendRegistry that has GPU
										surfaces; without OpenGL, such a backend or a
										context they draw on the CPU (said once on stderr).
										Popups always draw on the CPU. Off by default;
										NGUI_RENDERER=gpu turns it on at Create(). */
			bool				GpuRendering() const;
									/*!< Whether new windows render on the GPU. */
			void				RunOnMainThread(std::function<void()> work);
									/*!< Runs \a work on the main thread: at once if called
										there, otherwise during the next ProcessEvents(). */

private:
								SdlPlatform();

			std::shared_ptr<sdl3::PlatformState> fData;
};


} // namespace ngui::platform
