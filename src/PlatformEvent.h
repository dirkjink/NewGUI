/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The mark of the events a platform sends (input, window and drop events,
// the application's quit request): the looper that dispatches one hands it
// to the recorder (Record & Replay, specification 20.8) and takes the mark
// off, so that a copy the application sends on is not an event again.


#include <utility>

#include <ngui/Message.h>


namespace ngui {


struct PlatformEventMark {
	static void Set(BMessage& message)
	{
		message.fPlatformEvent = true;
	}

	// Whether \a message was marked; it is not any more.
	static bool Take(BMessage& message)
	{
		return std::exchange(message.fPlatformEvent, false);
	}
};


} // namespace ngui
