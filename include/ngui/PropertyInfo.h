/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include <ngui/Error.h>
#include <ngui/Message.h>


namespace ngui {


/*!	A property a handler understands in scripting messages (Haiku's
	property_info, specification 20.7). */
struct BPropertyInfoEntry {
			std::string_view	name;		/*!< The property, e.g. "Value". */
			std::vector<std::uint32_t> commands;
									/*!< kGetProperty, kSetProperty …; empty: any command,
										but only for a specifier that is not the last one,
										which leads on to another handler (e.g. "View"). */
			std::vector<std::uint32_t> specifiers;
									/*!< The specifier forms it takes; empty: any. */
			std::string_view	usage;		/*!< What it is, for humans (hey's help). */
			std::uint32_t		extraData = 0;
									/*!< A value of the handler's, e.g. for a switch. */
			std::vector<BTypeCode> types;	/*!< The types of its values. */
};


/*!	A table of properties (Haiku's BPropertyInfo): what a handler
	understands, for ResolveSpecifier() to match and GetSupportedSuites() to
	report. */
class BPropertyInfo {
public:
	explicit					BPropertyInfo(std::span<const BPropertyInfoEntry> properties);
									/*!< Over \a properties, which must outlive it. */

			std::int32_t		FindMatch(const BMessage& message, std::int32_t index,
									std::uint32_t form, std::string_view property,
									std::uint32_t* extraData = nullptr) const;
									/*!< The first entry for \a property whose commands take
										message's `what` (or that has none, if \a index is not
										0, the last specifier) and whose specifiers take
										\a form; its extraData into \a extraData. -1 if none
										matches. */
			std::span<const BPropertyInfoEntry> Properties() const;
									/*!< The table. */
			BMessage			ToMessage() const;
									/*!< The table as a message, one message "property" per
										entry with "name", "commands", "specifiers", "usage"
										and "types"; what kGetSupportedSuites replies in
										"messages" (Haiku flattens the BPropertyInfo). */

private:
			std::span<const BPropertyInfoEntry> fProperties;
};


/*!	The int32 "error" of a scripting reply: 0 for success, otherwise the
	error's code plus 1 (Haiku: a status_t). */
std::int32_t					ScriptingStatus(const std::expected<void, Error>& result);

/*!	What a scripting reply says: success, or the error of its "error" and
	"message" fields; a kMessageNotUnderstood without them is
	BadScriptSyntax. */
std::expected<void, Error>		ScriptingResult(const BMessage& reply);

/*!	Answers the scripting message \a message with \a error: a
	kMessageNotUnderstood with "error" and "message"
	(Haiku's B_MESSAGE_NOT_UNDERSTOOD with B_BAD_SCRIPT_SYNTAX). */
void							ReplyScriptingError(BMessage& message, const Error& error);


} // namespace ngui
