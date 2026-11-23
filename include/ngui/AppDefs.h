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


} // namespace ngui
