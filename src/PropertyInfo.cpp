/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/app/PropertyInfo.cpp (commit b1d497bb69):
// FindMatch(), FindCommand() and FindSpecifier(); the flattened form is a
// BMessage instead of Haiku's own format.


#include <ngui/PropertyInfo.h>

#include <algorithm>
#include <string>

#include <ngui/AppDefs.h>


namespace ngui {


namespace {


// Haiku's BPropertyInfo::FindCommand(): without commands the entry leads on
// and takes any command, otherwise only the last specifier (index 0) can
// carry one of them.
bool
FindCommand(std::uint32_t what, std::int32_t index, const BPropertyInfoEntry& entry)
{
	if (entry.commands.empty())
		return true;
	if (index != 0)
		return false;
	return std::ranges::find(entry.commands, what) != entry.commands.end();
}


bool
FindSpecifier(std::uint32_t form, const BPropertyInfoEntry& entry)
{
	return entry.specifiers.empty()
		|| std::ranges::find(entry.specifiers, form) != entry.specifiers.end();
}


} // namespace


BPropertyInfo::BPropertyInfo(std::span<const BPropertyInfoEntry> properties)
	:
	fProperties(properties)
{
}


std::int32_t
BPropertyInfo::FindMatch(const BMessage& message, std::int32_t index, std::uint32_t form,
	std::string_view property, std::uint32_t* extraData) const
{
	for (std::size_t i = 0; i < fProperties.size(); i++) {
		const BPropertyInfoEntry& entry = fProperties[i];
		if (entry.name == property && FindCommand(message.what, index, entry)
			&& FindSpecifier(form, entry)) {
			if (extraData != nullptr)
				*extraData = entry.extraData;
			return static_cast<std::int32_t>(i);
		}
	}
	return -1;
}


std::span<const BPropertyInfoEntry>
BPropertyInfo::Properties() const
{
	return fProperties;
}


BMessage
BPropertyInfo::ToMessage() const
{
	BMessage table;
	for (const BPropertyInfoEntry& entry : fProperties) {
		BMessage property;
		(void)property.AddString("name", entry.name);
		for (const std::uint32_t command : entry.commands)
			(void)property.AddUInt32("commands", command);
		for (const std::uint32_t form : entry.specifiers)
			(void)property.AddUInt32("specifiers", form);
		(void)property.AddString("usage", entry.usage);
		for (const BTypeCode type : entry.types)
			(void)property.AddUInt32("types", static_cast<std::uint32_t>(type));
		(void)table.AddMessage("property", property);
	}
	return table;
}


std::int32_t
ScriptingStatus(const std::expected<void, Error>& result)
{
	return result.has_value() ? 0 : static_cast<std::int32_t>(result.error().code) + 1;
}


std::expected<void, Error>
ScriptingResult(const BMessage& reply)
{
	const std::int32_t status = reply.GetInt32("error", 0);
	if (status > 0 && status <= static_cast<std::int32_t>(ErrorCode::BadScriptSyntax) + 1) {
		return Failure(static_cast<ErrorCode>(status - 1),
			std::string(reply.GetString("message", "")));
	}
	if (reply.what == kMessageNotUnderstood)
		return Failure(ErrorCode::BadScriptSyntax, std::string(reply.GetString("message", "")));
	return {};
}


void
ReplyScriptingError(BMessage& message, const Error& error)
{
	BMessage reply(kMessageNotUnderstood);
	(void)reply.AddInt32("error", ScriptingStatus(std::unexpected(error)));
	(void)reply.AddString("message", error.message);
	(void)message.SendReply(reply);
}


} // namespace ngui
