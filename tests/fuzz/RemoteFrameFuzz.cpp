/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "FuzzTarget.h"

#include <cstddef>
#include <span>

#include <ngui/Message.h>

#include "remote/LocalSocket.h"


NGUI_FUZZ_TARGET(RemoteFrame)
{
	// What is tested: DecodeFrame() of arbitrary bytes, as a remote control
	// client may send them, and for a frame that decodes, the message
	// encoded and decoded again.
	// Success criteria: Nothing crashes (checked by the sanitizers); a
	// decoded message survives a round trip unchanged.
	// Why it matters: Remote control reads frames from another process
	// (specification 20.7); its length and payload must never make the
	// application read out of bounds, allocate what the sender likes, or
	// crash.
	//
	// Given
	const auto bytes = std::as_bytes(data);

	// When
	const auto message = ngui::remote::detail::DecodeFrame(bytes);
	if (!message.has_value())
		return;
	const auto frame = ngui::remote::detail::EncodeFrame(*message);

	// Then
	NGUI_FUZZ_CHECK(frame.has_value());
	const auto again = ngui::remote::detail::DecodeFrame(*frame);
	NGUI_FUZZ_CHECK(again.has_value() && *again == *message);
}
