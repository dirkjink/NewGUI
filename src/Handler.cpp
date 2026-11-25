/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Handler.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <string>
#include <vector>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/PropertyInfo.h>


namespace ngui {


namespace {


// Handler tokens start at 1; 0 means "the preferred handler".
std::int32_t
NewToken()
{
	static std::atomic<std::int32_t> sNext = 1;
	return sNext.fetch_add(1);
}


} // namespace


BHandler::BHandler(std::string_view name)
	:
	fName(name),
	fToken(NewToken())
{
}


BHandler::~BHandler()
{
	if (fLooper != nullptr && fLooper != this)
		fLooper->RemoveHandler(this);
}


namespace {


// The properties of every handler (Haiku's sHandlerPropInfo, without
// "Messenger": a BMessenger cannot be flattened, and remote control
// (specification 20.7) gets nothing from it).
const std::vector<BPropertyInfoEntry> kHandlerProperties = {
	{"Suites", {kGetProperty}, {kDirectSpecifier},
		"The suites and properties it understands.", 0, {}},
	{"InternalName", {kGetProperty}, {kDirectSpecifier}, "Its name.", 0,
		{BTypeCode::String}},
};


// A command of scripting: without specifiers Haiku answers it with the
// handler's messenger, which NewGUI leaves out, so it is not understood.
bool
IsScriptingCommand(std::uint32_t what)
{
	return what == kGetProperty || what == kSetProperty || what == kCreateProperty
		|| what == kDeleteProperty || what == kCountProperties || what == kExecuteProperty;
}


} // namespace


void
BHandler::MessageReceived(BMessage* message)
{
	// Ported from Haiku: src/kits/app/Handler.cpp (commit b1d497bb69).
	switch (message->what) {
		case kGetProperty:
		{
			const auto specifier = message->GetCurrentSpecifier();
			if (!specifier.has_value())
				break;
			BMessage reply(kReply);
			std::expected<void, Error> result;
			if (specifier->property == "Suites")
				result = GetSupportedSuites(&reply);
			else if (specifier->property == "InternalName")
				result = reply.AddString("result", Name());
			else
				break;
			(void)reply.AddInt32("error", ScriptingStatus(result));
			(void)message->SendReply(reply);
			return;
		}
		case kGetSupportedSuites:
		{
			BMessage reply(kReply);
			(void)reply.AddInt32("error", ScriptingStatus(GetSupportedSuites(&reply)));
			(void)message->SendReply(reply);
			return;
		}
		default:
			break;
	}

	// Unhandled: on to the next handler; at the end of the chain a
	// scripting message is answered (B_MESSAGE_NOT_UNDERSTOOD).
	if (fNextHandler != nullptr)
		fNextHandler->MessageReceived(message);
	else if (message->HasSpecifiers() || IsScriptingCommand(message->what)) {
		ReplyScriptingError(*message, {ErrorCode::BadScriptSyntax,
			"\"" + Name() + "\" does not understand the message"});
	}
}


BHandler*
BHandler::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& /*specifier*/,
	std::uint32_t form, std::string_view property)
{
	// Straight from the BeBook, as Haiku says.
	const BPropertyInfo properties(kHandlerProperties);
	if (properties.FindMatch(*message, index, form, property) >= 0)
		return this;
	ReplyScriptingError(*message, {ErrorCode::BadScriptSyntax,
		"\"" + Name() + "\" has no property \"" + std::string(property) + "\""});
	return nullptr;
}


std::expected<void, Error>
BHandler::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-handler"); !added)
		return added;
	return data->AddMessage("messages", BPropertyInfo(kHandlerProperties).ToMessage());
}


BLooper*
BHandler::Looper() const
{
	return fLooper;
}


const std::string&
BHandler::Name() const
{
	return fName;
}


void
BHandler::SetName(std::string_view name)
{
	fName = name;
}


BMessageFilter*
BHandler::AddFilter(std::unique_ptr<BMessageFilter> filter)
{
	if (filter == nullptr)
		return nullptr;
	filter->fOwner = this;
	fFilters.push_back(std::move(filter));
	return fFilters.back().get();
}


std::unique_ptr<BMessageFilter>
BHandler::RemoveFilter(BMessageFilter* filter)
{
	const auto found = std::find_if(fFilters.begin(), fFilters.end(),
		[filter](const std::unique_ptr<BMessageFilter>& owned) { return owned.get() == filter; });
	if (found == fFilters.end())
		return nullptr;
	std::unique_ptr<BMessageFilter> removed = std::move(*found);
	fFilters.erase(found);
	removed->fOwner = nullptr;
	return removed;
}


std::vector<BMessageFilter*>
BHandler::FilterList() const
{
	std::vector<BMessageFilter*> filters;
	for (const std::unique_ptr<BMessageFilter>& filter : fFilters)
		filters.push_back(filter.get());
	return filters;
}


BHandler*
BHandler::NextHandler() const
{
	return fNextHandler;
}


void
BHandler::SetNextHandler(BHandler* handler)
{
	assert((handler == nullptr || handler->fLooper == fLooper)
		&& "BHandler::SetNextHandler() with a handler of another looper");
	if (handler != nullptr && handler->fLooper != fLooper)
		return;
	fNextHandler = handler;
}


} // namespace ngui
