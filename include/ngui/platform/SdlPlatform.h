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
#include <ngui/Flags.h>
#include <ngui/platform/Platform.h>


namespace ngui::platform {


namespace sdl3 {
class PlatformState;
}


/*!	The modifier keys in the "modifiers" field of mouse and key events, as
	Haiku's B_SHIFT_KEY ... On PC keyboards Alt is Command (Haiku's default),
	Ctrl is Control and the Windows or Super key is Option. */
enum class BModifier : std::uint32_t {
	Shift = 1u << 0,		/*!< B_SHIFT_KEY */
	Command = 1u << 1,		/*!< B_COMMAND_KEY: Alt, or Cmd on a Mac. */
	Control = 1u << 2,		/*!< B_CONTROL_KEY: Ctrl. */
	CapsLock = 1u << 3,		/*!< B_CAPS_LOCK */
	ScrollLock = 1u << 4,	/*!< B_SCROLL_LOCK */
	NumLock = 1u << 5,		/*!< B_NUM_LOCK */
	Option = 1u << 6		/*!< B_OPTION_KEY: Windows or Super key, Option on a Mac. */
};
NGUI_FLAG_ENUM(BModifier)

/*!	A set of BModifier keys. */
using BModifierFlags = Flags<BModifier>;


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
									/*!< See Platform. */

			void				RunOnMainThread(std::function<void()> work);
									/*!< Runs \a work on the main thread: at once if called
										there, otherwise during the next ProcessEvents(). */

private:
								SdlPlatform();

			std::shared_ptr<sdl3::PlatformState> fData;
};


} // namespace ngui::platform
