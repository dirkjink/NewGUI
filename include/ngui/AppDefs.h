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

/*!	The mouse rested (B_MOUSE_IDLE): no movement for the tooltip show delay
	(BToolTipManager::ShowDelay()) since it last moved. BWindow sends it to
	the view under the mouse, with "where" in the view's coordinates; the
	view shows its tooltip. Once per rest, as Haiku's app_server. */
inline constexpr std::uint32_t kMouseIdle = FourCharCode("_MSI");

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


/*!	B_CUT: cut the selection to the clipboard (Command+X goes to the focus
	view). */
inline constexpr std::uint32_t kCut = FourCharCode("CCUT");

/*!	B_COPY: copy the selection (Command+C). */
inline constexpr std::uint32_t kCopy = FourCharCode("COPY");

/*!	B_PASTE: paste the clipboard (Command+V). */
inline constexpr std::uint32_t kPaste = FourCharCode("PSTE");

/*!	B_SELECT_ALL: select everything (Command+A). */
inline constexpr std::uint32_t kSelectAll = FourCharCode("SALL");

/*!	B_UNDO: undo (or redo) the last change, e.g. of a text view. */
inline constexpr std::uint32_t kUndo = FourCharCode("UNDO");

/*!	B_INPUT_METHOD_EVENT: an input method composes text; to the focus view.
	"be:opcode" (int32) is one of kInputMethodStarted ...; with
	kInputMethodChanged "be:string" (the text composed so far),
	"be:confirmed" (bool, whether it is final), "be:selection" (two int32,
	byte offsets of the part the input method works on) and optionally pairs
	of "be:clause_start" and "be:clause_end". */
inline constexpr std::uint32_t kInputMethodEvent = FourCharCode("IMEV");

/*!	B_INPUT_METHOD_STARTED: composition begins. */
inline constexpr std::int32_t kInputMethodStarted = 0;
/*!	B_INPUT_METHOD_STOPPED: composition ends; the final text, if any, came
	confirmed or as key events. */
inline constexpr std::int32_t kInputMethodStopped = 1;
/*!	B_INPUT_METHOD_CHANGED: the composed text changed. */
inline constexpr std::int32_t kInputMethodChanged = 2;
/*!	B_INPUT_METHOD_LOCATION_REQUEST: the input method asks where the text is. */
inline constexpr std::int32_t kInputMethodLocationRequest = 3;


} // namespace ngui
