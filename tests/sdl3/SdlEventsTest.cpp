/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/SdlEvents.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/platform/SdlPlatform.h>


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::platform::sdl3;


constexpr SDL_WindowID kWindow = 7;


TranslationContext
Context()
{
	TranslationContext context;
	context.when = Clock::Time(1234);
	context.modifiers = BModifierFlags(BModifier::Shift).Raw();
	context.scale = [](SDL_WindowID) { return 2.0f; };
	return context;
}


SDL_Event
Button(bool down, std::uint8_t button, float x, float y, std::uint8_t clicks)
{
	SDL_Event event{};
	event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	event.button.windowID = kWindow;
	event.button.button = button;
	event.button.down = down;
	event.button.clicks = clicks;
	event.button.x = x;
	event.button.y = y;
	return event;
}


SDL_Event
Key(bool down, SDL_Scancode scancode, SDL_Keymod modifiers, bool repeat = false)
{
	SDL_Event event{};
	event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
	event.key.windowID = kWindow;
	event.key.scancode = scancode;
	event.key.mod = modifiers;
	event.key.down = down;
	event.key.repeat = repeat;
	return event;
}


SDL_Event
Text(const char* text, SDL_WindowID window = kWindow)
{
	SDL_Event event{};
	event.type = SDL_EVENT_TEXT_INPUT;
	event.text.windowID = window;
	event.text.text = text;
	return event;
}


SDL_Event
WindowEvent(SDL_EventType type, std::int32_t data1 = 0, std::int32_t data2 = 0)
{
	SDL_Event event{};
	event.type = type;
	event.window.windowID = kWindow;
	event.window.data1 = data1;
	event.window.data2 = data2;
	return event;
}


TEST(SdlEventsTest, MouseEventsBecomeHaikusMessages)
{
	// What is tested: A left button press with a double click, its release,
	// a motion with the right button held, and a wheel turned towards the
	// user, normal and flipped.
	// Success criteria: kMouseDown with "where", "buttons" 1 (the buttons
	// down after it), "clicks" 2, "modifiers" and "when"; kMouseUp with
	// buttons 0; kMouseMoved with buttons 2; a wheel turned down gives a
	// positive "be:wheel_delta_y", the flipped one a negative.
	// Why it matters: Views see only these messages (module P2); their
	// fields must mean what they mean in Haiku.
	//
	// Given
	SDL_Event motion{};
	motion.type = SDL_EVENT_MOUSE_MOTION;
	motion.motion.windowID = kWindow;
	motion.motion.x = 3;
	motion.motion.y = 4;
	motion.motion.state = SDL_BUTTON_RMASK;
	SDL_Event wheel{};
	wheel.type = SDL_EVENT_MOUSE_WHEEL;
	wheel.wheel.windowID = kWindow;
	wheel.wheel.y = -1;
	SDL_Event flipped = wheel;
	flipped.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
	const std::vector<SDL_Event> events = {Button(true, SDL_BUTTON_LEFT, 10.5f, 20, 2),
		Button(false, SDL_BUTTON_LEFT, 10.5f, 20, 2), motion, wheel, flipped};

	// When
	const std::vector<TranslatedEvent> messages = Translate(events, Context());

	// Then
	ASSERT_EQ(messages.size(), 5u);
	const BMessage& down = messages[0].message;
	EXPECT_EQ(messages[0].window, kWindow);
	EXPECT_EQ(down.what, kMouseDown);
	EXPECT_EQ(down.GetPoint("where", {}), (gfx::Point{10.5f, 20}));
	EXPECT_EQ(down.GetInt32("buttons", -1), 1);
	EXPECT_EQ(down.GetInt32("clicks", -1), 2);
	EXPECT_EQ(down.GetInt32("modifiers", -1), 1);
	EXPECT_EQ(down.GetInt64("when", -1), 1234);
	EXPECT_EQ(messages[1].message.what, kMouseUp);
	EXPECT_EQ(messages[1].message.GetInt32("buttons", -1), 0);
	EXPECT_EQ(messages[2].message.what, kMouseMoved);
	EXPECT_EQ(messages[2].message.GetInt32("buttons", -1), 2);
	EXPECT_EQ(messages[3].message.what, kMouseWheelChanged);
	EXPECT_EQ(messages[3].message.GetFloat("be:wheel_delta_y", 0), 1);
	EXPECT_EQ(messages[4].message.GetFloat("be:wheel_delta_y", 0), -1);
}


TEST(SdlEventsTest, KeysCarryTheTextTheyType)
{
	// What is tested: Shift+A followed by its text "A", a repeated key, a
	// key up, text on its own (an input method), and text of another window
	// after a key.
	// Success criteria: Shift+A is one kKeyDown with "key" 4 (the USB HID
	// usage of A), "bytes" "A" and Shift in "modifiers"; the repeat has
	// "be:key_repeat" 1; kKeyUp has no text; lone text is a kKeyDown with
	// key 0; text of another window is not attached to the key.
	// Why it matters: Haiku's B_KEY_DOWN carries the typed text, which text
	// views rely on; SDL delivers key and text separately.
	//
	// Given
	const std::vector<SDL_Event> events = {Key(true, SDL_SCANCODE_A, SDL_KMOD_LSHIFT), Text("A"),
		Key(true, SDL_SCANCODE_B, SDL_KMOD_NONE, true), Key(false, SDL_SCANCODE_A, SDL_KMOD_NONE),
		Text("ä"), Key(true, SDL_SCANCODE_C, SDL_KMOD_NONE), Text("c", kWindow + 1)};

	// When
	const std::vector<TranslatedEvent> messages = Translate(events, Context());

	// Then
	ASSERT_EQ(messages.size(), 6u);
	EXPECT_EQ(messages[0].message.what, kKeyDown);
	EXPECT_EQ(messages[0].message.GetInt32("key", -1), 4);
	EXPECT_EQ(messages[0].message.GetString("bytes", "?"), "A");
	EXPECT_EQ(messages[0].message.GetInt32("modifiers", -1), 1);
	EXPECT_EQ(messages[0].message.GetInt32("be:key_repeat", -1), 0);
	EXPECT_EQ(messages[1].message.GetInt32("be:key_repeat", -1), 1);
	EXPECT_EQ(messages[1].message.GetString("bytes", "?"), "");
	EXPECT_EQ(messages[2].message.what, kKeyUp);
	EXPECT_EQ(messages[2].message.GetString("bytes", "?"), "");
	EXPECT_EQ(messages[3].message.what, kKeyDown);
	EXPECT_EQ(messages[3].message.GetInt32("key", -1), 0);
	EXPECT_EQ(messages[3].message.GetString("bytes", "?"), "ä");
	EXPECT_EQ(messages[4].message.GetString("bytes", "?"), "");
	EXPECT_EQ(messages[5].window, kWindow + 1);
	EXPECT_EQ(messages[5].message.GetString("bytes", "?"), "c");
}


TEST(SdlEventsTest, KeysWithoutTextCarryHaikusBytes)
{
	// What is tested: Return, Tab, Escape, the left arrow, F5, a key up of
	// Return, and a letter key without text.
	// Success criteria: The bytes are Haiku's: 0x0a, 0x09, 0x1b, 0x1c,
	// 0x10 (B_FUNCTION_KEY); the key up of Return carries 0x0a as well; a
	// letter without text has none.
	// Why it matters: SDL sends text only for keys that type some; buttons
	// react to B_ENTER and keyboard navigation to B_TAB, which must also
	// work with a real keyboard.
	//
	// Given
	const std::vector<SDL_Event> events = {Key(true, SDL_SCANCODE_RETURN, SDL_KMOD_NONE),
		Key(true, SDL_SCANCODE_TAB, SDL_KMOD_NONE), Key(true, SDL_SCANCODE_ESCAPE, SDL_KMOD_NONE),
		Key(true, SDL_SCANCODE_LEFT, SDL_KMOD_NONE), Key(true, SDL_SCANCODE_F5, SDL_KMOD_NONE),
		Key(false, SDL_SCANCODE_RETURN, SDL_KMOD_NONE), Key(true, SDL_SCANCODE_Q, SDL_KMOD_NONE)};

	// When
	const std::vector<TranslatedEvent> messages = Translate(events, Context());

	// Then
	ASSERT_EQ(messages.size(), 7u);
	EXPECT_EQ(messages[0].message.GetString("bytes", "?"), "\n");
	EXPECT_EQ(messages[1].message.GetString("bytes", "?"), "\t");
	EXPECT_EQ(messages[2].message.GetString("bytes", "?"), "\x1b");
	EXPECT_EQ(messages[3].message.GetString("bytes", "?"), "\x1c");
	EXPECT_EQ(messages[4].message.GetString("bytes", "?"), "\x10");
	EXPECT_EQ(messages[5].message.what, kKeyUp);
	EXPECT_EQ(messages[5].message.GetString("bytes", "?"), "\n");
	EXPECT_EQ(messages[6].message.GetString("bytes", "?"), "");
}


TEST(SdlEventsTest, WindowEventsAndModifiers)
{
	// What is tested: A resize to 640 × 480, a move, focus gained and lost,
	// the mouse leaving, the close button; and the mapping of SDL's
	// modifier keys.
	// Success criteria: kWindowResized with the logical size and the scale;
	// kWindowMoved with "where"; kWindowActivated with "active"; a
	// kMouseMoved with "be:exited"; the close button asks to quit; Alt is
	// Command, Ctrl Control and the GUI key Option.
	// Why it matters: The window (G9) relayouts, tracks activation and
	// asks QuitRequested() from these messages.
	//
	// Given
	const std::vector<SDL_Event> events = {WindowEvent(SDL_EVENT_WINDOW_RESIZED, 640, 480),
		WindowEvent(SDL_EVENT_WINDOW_MOVED, 30, 40), WindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED),
		WindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST), WindowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE),
		WindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED), WindowEvent(SDL_EVENT_WINDOW_EXPOSED)};

	// When
	const std::vector<TranslatedEvent> messages = Translate(events, Context());

	// Then
	ASSERT_EQ(messages.size(), 6u);
	EXPECT_EQ(messages[0].message.what, kWindowResized);
	EXPECT_EQ(messages[0].message.GetFloat("width", 0), 640);
	EXPECT_EQ(messages[0].message.GetFloat("height", 0), 480);
	EXPECT_EQ(messages[0].message.GetFloat("be:scale", 0), 2);
	EXPECT_EQ(messages[1].message.GetPoint("where", {}), (gfx::Point{30, 40}));
	EXPECT_TRUE(messages[2].message.GetBool("active", false));
	EXPECT_FALSE(messages[3].message.GetBool("active", true));
	EXPECT_TRUE(messages[4].message.GetBool("be:exited", false));
	EXPECT_EQ(messages[5].message.what, kQuitRequested);

	EXPECT_EQ(Modifiers(SDL_KMOD_RALT), BModifierFlags(BModifier::Command).Raw());
	EXPECT_EQ(Modifiers(SDL_KMOD_LCTRL), BModifierFlags(BModifier::Control).Raw());
	EXPECT_EQ(Modifiers(SDL_KMOD_LGUI), BModifierFlags(BModifier::Option).Raw());
	EXPECT_EQ(Modifiers(SDL_KMOD_CAPS | SDL_KMOD_NUM | SDL_KMOD_SCROLL),
		(BModifier::CapsLock | BModifier::NumLock | BModifier::ScrollLock).Raw());
	EXPECT_EQ(Buttons(SDL_BUTTON_LMASK | SDL_BUTTON_MMASK), 5);
}


} // namespace
