/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/MessageFilter.h>

#include <utility>

#include <ngui/Handler.h>
#include <ngui/Message.h>


namespace ngui {


BMessageFilter::BMessageFilter(std::optional<std::uint32_t> command, Hook hook)
	:
	BMessageFilter(BMessageDelivery::Any, BMessageSource::Any, command, std::move(hook))
{
}


BMessageFilter::BMessageFilter(BMessageDelivery delivery, BMessageSource source,
	std::optional<std::uint32_t> command, Hook hook)
	:
	fCommand(command),
	fDelivery(delivery),
	fSource(source),
	fHook(std::move(hook))
{
}


BMessageFilter::~BMessageFilter() = default;


BFilterResult
BMessageFilter::Filter(BMessage* message, BHandler** target)
{
	return fHook ? fHook(message, target, this) : BFilterResult::Dispatch;
}


BMessageDelivery
BMessageFilter::MessageDelivery() const
{
	return fDelivery;
}


BMessageSource
BMessageFilter::MessageSource() const
{
	return fSource;
}


std::optional<std::uint32_t>
BMessageFilter::Command() const
{
	return fCommand;
}


bool
BMessageFilter::FiltersAnyCommand() const
{
	return !fCommand.has_value();
}


BLooper*
BMessageFilter::Looper() const
{
	return fOwner != nullptr ? fOwner->Looper() : nullptr;
}


bool
BMessageFilter::Applies(const BMessage& message) const
{
	if (fCommand.has_value() && *fCommand != message.what)
		return false;
	// Every message is local and programmed until drag and drop and remote
	// control exist.
	return fDelivery != BMessageDelivery::Dropped && fSource != BMessageSource::Remote;
}


} // namespace ngui
