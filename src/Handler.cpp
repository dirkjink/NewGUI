/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Handler.h>

#include <algorithm>
#include <atomic>
#include <cassert>

#include <ngui/Looper.h>


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


void
BHandler::MessageReceived(BMessage* message)
{
	// Unhandled: on to the next handler. Haiku answers scripting messages
	// with B_MESSAGE_NOT_UNDERSTOOD here; that comes with scripting.
	if (fNextHandler != nullptr)
		fNextHandler->MessageReceived(message);
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
