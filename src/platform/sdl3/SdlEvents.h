/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The translation of SDL's events into NewGUI's messages (module P2):
// pure functions over a batch of events, so that tests can feed them
// events without a window system.


#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <ngui/Clock.h>
#include <ngui/Message.h>


namespace ngui::platform::sdl3 {


// A message for the window \a window, or for the application if it is 0
// (kApplicationEvent).
struct TranslatedEvent {
	SDL_WindowID	window;
	BMessage		message;
};


// The "window" of a message for the application: SDL counts its windows
// from 1.
inline constexpr SDL_WindowID kApplicationEvent = 0;


// What the translation needs to know beyond the events.
struct TranslationContext {
	Clock::Time						when{};
	std::uint32_t					modifiers = 0;	// for mouse events, as BModifier bits
	std::int32_t					buttons = 0;	// the mouse buttons down, as "buttons"
	std::function<float(SDL_WindowID)> scale;		// device pixels per logical pixel
	std::uint32_t					dropData = 0;	// the event type of a DropPayload,
													// 0 for none (module P4)
};


std::uint32_t					Modifiers(SDL_Keymod modifiers);
std::int32_t					Buttons(SDL_MouseButtonFlags buttons);

// The messages of \a events, in order. A key down followed by the text it
// typed becomes one kKeyDown with "bytes"; text on its own becomes a
// kKeyDown with key 0. Events NewGUI does not use are left out.
std::vector<TranslatedEvent>	Translate(std::span<const SDL_Event> events,
									const TranslationContext& context);


} // namespace ngui::platform::sdl3
