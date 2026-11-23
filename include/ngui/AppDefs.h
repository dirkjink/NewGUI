/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>

#include <ngui/Message.h>


namespace ngui {


// The system commands of BMessage::what (Haiku's AppDefs.h), with the codes
// of Haiku. Constants with k prefix (CONVENTIONS.md 4.2) rather than an enum
// class, since `what` also holds the commands of applications.


/*!	Asks a looper to quit; it does if QuitRequested() agrees
	(B_QUIT_REQUESTED). */
inline constexpr std::uint32_t kQuitRequested = FourCharCode("_QRQ");

/*!	The answer a waiting sender gets when its message is destroyed without a
	reply (B_NO_REPLY). */
inline constexpr std::uint32_t kNoReply = FourCharCode("_NRP");

/*!	A plain reply, e.g. to kQuitRequested with the bool "result" (B_REPLY). */
inline constexpr std::uint32_t kReply = FourCharCode("RPLY");

/*!	The command line, as strings "argv" and the int32 "argc"; to
	BApplication::ArgvReceived() (B_ARGV_RECEIVED). */
inline constexpr std::uint32_t kArgvReceived = FourCharCode("_ARG");

/*!	The application is about to process messages; to
	BApplication::ReadyToRun() (B_READY_TO_RUN). */
inline constexpr std::uint32_t kReadyToRun = FourCharCode("_RTR");

/*!	The regular pulse of the application; to BApplication::Pulse()
	(B_PULSE). */
inline constexpr std::uint32_t kPulse = FourCharCode("_PUL");

/*!	The user asks for the about box; to BApplication::AboutRequested()
	(B_ABOUT_REQUESTED). */
inline constexpr std::uint32_t kAboutRequested = FourCharCode("_ABR");


// The events of the platform (module P1), sent to the target of a
// PlatformWindow. Every one has the int64 "when", the time of the Clock in
// microseconds. Positions are logical pixels in the content area.

/*!	A mouse button went down (B_MOUSE_DOWN): "where" (Point), "buttons"
	(int32, the buttons now down: 1 primary, 2 secondary, 4 tertiary),
	"clicks" (int32, 2 for a double click), "modifiers" (int32). */
inline constexpr std::uint32_t kMouseDown = FourCharCode("_MDN");

/*!	A mouse button went up (B_MOUSE_UP): "where", "buttons", "modifiers". */
inline constexpr std::uint32_t kMouseUp = FourCharCode("_MUP");

/*!	The mouse moved (B_MOUSE_MOVED): "where", "buttons", "modifiers", and
	the bool "be:exited" when it left the window. */
inline constexpr std::uint32_t kMouseMoved = FourCharCode("_MMV");

/*!	The wheel turned (B_MOUSE_WHEEL_CHANGED): the floats "be:wheel_delta_x"
	and "be:wheel_delta_y", positive to the right and downwards. */
inline constexpr std::uint32_t kMouseWheelChanged = FourCharCode("_MWC");

/*!	A key went down (B_KEY_DOWN): "key" (int32, the key code), "bytes"
	(string, the UTF-8 text it types, may be empty), "modifiers" (int32),
	"be:key_repeat" (int32, how often it repeated). Key codes and modifier
	bits come with the platform implementations (P2). */
inline constexpr std::uint32_t kKeyDown = FourCharCode("_KYD");

/*!	A key went up (B_KEY_UP): "key", "bytes", "modifiers". */
inline constexpr std::uint32_t kKeyUp = FourCharCode("_KYU");

/*!	The content area changed its size (B_WINDOW_RESIZED): the floats
	"width" and "height", logical pixels, and the float "be:scale" (device
	pixels per logical pixel). */
inline constexpr std::uint32_t kWindowResized = FourCharCode("_WRS");

/*!	The window moved (B_WINDOW_MOVED): "where", the content area's left top
	corner on the desktop. */
inline constexpr std::uint32_t kWindowMoved = FourCharCode("_WMV");

/*!	The window became active or inactive (B_WINDOW_ACTIVATED): the bool
	"active". */
inline constexpr std::uint32_t kWindowActivated = FourCharCode("_ACT");


/*!	B_UPDATE_STATUS_BAR: BStatusBar::Update() with "delta" (float), "text"
	and "trailing_text" (strings). */
inline constexpr std::uint32_t kUpdateStatusBar = FourCharCode("_UPS");

/*!	B_RESET_STATUS_BAR: BStatusBar::Reset() with "label" and
	"trailing_label" (strings). */
inline constexpr std::uint32_t kResetStatusBar = FourCharCode("_RES");


} // namespace ngui
