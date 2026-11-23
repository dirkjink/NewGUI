/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "SdlEvents.h"

#include <SDL3/SDL_mouse.h>

#include <string_view>

#include <ngui/AppDefs.h>
#include <ngui/platform/SdlPlatform.h>


namespace ngui::platform::sdl3 {


namespace {


BMessage
Stamped(std::uint32_t what, const TranslationContext& context)
{
	BMessage message(what);
	(void)message.AddInt64("when", context.when.count());
	return message;
}


BMessage
MouseMessage(std::uint32_t what, float x, float y, std::int32_t buttons,
	const TranslationContext& context)
{
	BMessage message = Stamped(what, context);
	(void)message.AddPoint("where", {x, y});
	(void)message.AddInt32("buttons", buttons);
	(void)message.AddInt32("modifiers", static_cast<std::int32_t>(context.modifiers));
	return message;
}


std::int32_t
ButtonBit(std::uint8_t button)
{
	switch (button) {
		case SDL_BUTTON_LEFT:
			return 1;
		case SDL_BUTTON_RIGHT:
			return 2;
		case SDL_BUTTON_MIDDLE:
			return 4;
		default:
			return 0;
	}
}


} // namespace


std::uint32_t
Modifiers(SDL_Keymod modifiers)
{
	BModifierFlags flags;
	if ((modifiers & SDL_KMOD_SHIFT) != 0)
		flags.Set(BModifier::Shift);
	if ((modifiers & SDL_KMOD_ALT) != 0)
		flags.Set(BModifier::Command);
	if ((modifiers & SDL_KMOD_CTRL) != 0)
		flags.Set(BModifier::Control);
	if ((modifiers & SDL_KMOD_GUI) != 0)
		flags.Set(BModifier::Option);
	if ((modifiers & SDL_KMOD_CAPS) != 0)
		flags.Set(BModifier::CapsLock);
	if ((modifiers & SDL_KMOD_NUM) != 0)
		flags.Set(BModifier::NumLock);
	if ((modifiers & SDL_KMOD_SCROLL) != 0)
		flags.Set(BModifier::ScrollLock);
	return flags.Raw();
}


std::int32_t
Buttons(SDL_MouseButtonFlags buttons)
{
	std::int32_t bits = 0;
	if ((buttons & SDL_BUTTON_LMASK) != 0)
		bits |= 1;
	if ((buttons & SDL_BUTTON_RMASK) != 0)
		bits |= 2;
	if ((buttons & SDL_BUTTON_MMASK) != 0)
		bits |= 4;
	return bits;
}


std::vector<TranslatedEvent>
Translate(std::span<const SDL_Event> events, const TranslationContext& context)
{
	std::vector<TranslatedEvent> translated;
	for (std::size_t i = 0; i < events.size(); i++) {
		const SDL_Event& event = events[i];
		switch (event.type) {
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
			case SDL_EVENT_MOUSE_BUTTON_UP:
			{
				const SDL_MouseButtonEvent& button = event.button;
				// "buttons" holds the buttons down after the event.
				const std::int32_t bit = ButtonBit(button.button);
				const std::int32_t buttons = button.down ? (context.buttons | bit)
					: (context.buttons & ~bit);
				BMessage message = MouseMessage(button.down ? kMouseDown : kMouseUp, button.x,
					button.y, buttons, context);
				if (button.down)
					(void)message.AddInt32("clicks", button.clicks);
				translated.push_back({button.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_MOUSE_MOTION:
			{
				const SDL_MouseMotionEvent& motion = event.motion;
				translated.push_back({motion.windowID, MouseMessage(kMouseMoved, motion.x,
					motion.y, Buttons(motion.state), context)});
				break;
			}
			case SDL_EVENT_MOUSE_WHEEL:
			{
				const SDL_MouseWheelEvent& wheel = event.wheel;
				// SDL counts upwards and to the right as positive; Haiku's
				// delta is positive downwards.
				const float flip = wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
				BMessage message = Stamped(kMouseWheelChanged, context);
				(void)message.AddFloat("be:wheel_delta_x", flip * wheel.x);
				(void)message.AddFloat("be:wheel_delta_y", -flip * wheel.y);
				translated.push_back({wheel.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_KEY_DOWN:
			case SDL_EVENT_KEY_UP:
			{
				const SDL_KeyboardEvent& key = event.key;
				BMessage message = Stamped(key.down ? kKeyDown : kKeyUp, context);
				(void)message.AddInt32("key", static_cast<std::int32_t>(key.scancode));
				(void)message.AddInt32("modifiers",
					static_cast<std::int32_t>(Modifiers(key.mod)));
				std::string_view bytes;
				if (key.down && i + 1 < events.size()
					&& events[i + 1].type == SDL_EVENT_TEXT_INPUT
					&& events[i + 1].text.windowID == key.windowID
					&& events[i + 1].text.text != nullptr) {
					bytes = events[i + 1].text.text;
					i++;
				}
				(void)message.AddString("bytes", bytes);
				if (key.down)
					(void)message.AddInt32("be:key_repeat", key.repeat ? 1 : 0);
				translated.push_back({key.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_TEXT_INPUT:
			{
				// Text without a key before it: an input method or a
				// composed character.
				const SDL_TextInputEvent& text = event.text;
				BMessage message = Stamped(kKeyDown, context);
				(void)message.AddInt32("key", 0);
				(void)message.AddInt32("modifiers", static_cast<std::int32_t>(context.modifiers));
				(void)message.AddString("bytes", text.text != nullptr ? text.text : "");
				(void)message.AddInt32("be:key_repeat", 0);
				translated.push_back({text.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_WINDOW_RESIZED:
			{
				// The logical size; the pixel size follows from it and the
				// scale.
				const SDL_WindowEvent& window = event.window;
				BMessage message = Stamped(kWindowResized, context);
				(void)message.AddFloat("width", static_cast<float>(window.data1));
				(void)message.AddFloat("height", static_cast<float>(window.data2));
				(void)message.AddFloat("be:scale",
					context.scale ? context.scale(window.windowID) : 1.0f);
				translated.push_back({window.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_WINDOW_MOVED:
			{
				const SDL_WindowEvent& window = event.window;
				BMessage message = Stamped(kWindowMoved, context);
				(void)message.AddPoint("where", {static_cast<float>(window.data1),
					static_cast<float>(window.data2)});
				translated.push_back({window.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
			case SDL_EVENT_WINDOW_FOCUS_LOST:
			{
				BMessage message = Stamped(kWindowActivated, context);
				(void)message.AddBool("active", event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
				translated.push_back({event.window.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
			{
				BMessage message = MouseMessage(kMouseMoved, -1, -1, 0, context);
				(void)message.AddBool("be:exited", true);
				translated.push_back({event.window.windowID, std::move(message)});
				break;
			}
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				translated.push_back({event.window.windowID, Stamped(kQuitRequested, context)});
				break;
			default:
				break;
		}
	}
	return translated;
}


} // namespace ngui::platform::sdl3
