/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Handler.h>

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
