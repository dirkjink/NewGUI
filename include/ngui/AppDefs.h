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


} // namespace ngui
