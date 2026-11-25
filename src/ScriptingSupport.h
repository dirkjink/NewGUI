/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// What the property handlers of the toolkit's classes share (scripting,
// specification 20.7).


#include <cstdint>
#include <expected>

#include <ngui/AppDefs.h>
#include <ngui/Error.h>
#include <ngui/Message.h>
#include <ngui/PropertyInfo.h>


namespace ngui::detail {


// Answers \a message with \a reply and its "error": success as it is, a
// failure as kMessageNotUnderstood with the error's "message" (the end of
// Haiku's BView::MessageReceived()).
inline void
SendScriptingReply(BMessage& message, BMessage reply, const std::expected<void, Error>& result)
{
	if (!result.has_value()) {
		reply.what = kMessageNotUnderstood;
		(void)reply.AddString("message", result.error().message);
	}
	(void)reply.AddInt32("error", ScriptingStatus(result));
	(void)message.SendReply(reply);
}


// The item an index or reverse index \a specifier names among \a count
// (Haiku: count - index from the end); -1 without an index.
inline std::int32_t
SpecifiedIndex(const BMessage& specifier, std::uint32_t form, std::int32_t count)
{
	const std::int32_t index = specifier.GetInt32("index", -1);
	if (index < 0)
		return -1;
	return form == kReverseIndexSpecifier || form == kReverseRangeSpecifier ? count - index
		: index;
}


} // namespace ngui::detail
