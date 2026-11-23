/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Looper.h>

#include <algorithm>
#include <cassert>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Autolock.h>

#include "LooperPort.h"


namespace ngui {


BLooper::BLooper(std::string_view name)
	:
	BHandler(name),
	fPort(std::make_shared<LooperPort>()),
	fLock(name)
{
	// The looper is its own first handler, as in Haiku.
	fLooper = this;
	fHandlers.push_back(this);
}


BLooper::~BLooper()
{
	fPort->Close();
	for (BHandler* handler : fHandlers) {
		handler->fLooper = nullptr;
		handler->fNextHandler = nullptr;
	}
	fHandlers.clear();
}


std::expected<BMessenger, Error>
BLooper::Run(std::unique_ptr<BLooper> looper)
{
	if (looper == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no looper");
	if (looper->fRunning)
		return Failure(ErrorCode::InvalidArgument, "the looper runs already");
	BMessenger messenger(looper.get());
	looper->fRunning = true;
	std::thread([owned = std::move(looper)]() mutable {
		owned->fThread = std::this_thread::get_id();
		owned->fPort->SetThread(owned->fThread);
		owned->_Loop();
		// The looper dies on its own thread; afterwards anyone waiting in
		// WaitForLooperQuit() may go on.
		const std::shared_ptr<LooperPort> port = owned->fPort;
		owned.reset();
		port->Finish();
	}).detach();
	return messenger;
}


void
BLooper::_Loop()
{
	while (!fQuitting) {
		std::unique_ptr<BMessage> message = fPort->Pop();
		if (message == nullptr)
			break;
		if (message->fDelivery == nullptr)
			message->fDelivery = std::make_unique<MessageDelivery>();
		message->fDelivery->delivered = true;

		BAutolock lock(fLock);
		fCurrent = std::move(message);
		BHandler* handler = _FilteredTarget(fCurrent.get(), _Target(*fCurrent));
		// A message for a handler that left the looper is dropped; its
		// waiting sender gets kNoReply.
		if (handler != nullptr)
			DispatchMessage(fCurrent.get(), handler);
		fCurrent.reset();
	}
	fPort->Close();
}


bool
BLooper::_OnOwnThread() const
{
	return !fRunning || std::this_thread::get_id() == fThread;
}


BHandler*
BLooper::_Target(const BMessage& message) const
{
	const std::int32_t token = message.fDelivery != nullptr ? message.fDelivery->handlerToken : 0;
	if (token == 0)
		return fPreferred != nullptr ? fPreferred : const_cast<BLooper*>(this);
	const auto handler = std::find_if(fHandlers.begin(), fHandlers.end(),
		[&](const BHandler* candidate) { return candidate->fToken == token; });
	return handler != fHandlers.end() ? *handler : nullptr;
}


BHandler*
BLooper::_ApplyFilters(const std::vector<std::unique_ptr<BMessageFilter>>& filters,
	BMessage* message, BHandler* target)
{
	for (const std::unique_ptr<BMessageFilter>& filter : filters) {
		if (target == nullptr)
			return nullptr;
		if (!filter->Applies(*message))
			continue;
		if (filter->Filter(message, &target) == BFilterResult::Skip)
			return nullptr;
	}
	return target;
}


BHandler*
BLooper::_FilteredTarget(BMessage* message, BHandler* target)
{
	// As Haiku's _TopLevelFilter(): the common filters, then the filters of
	// the target, again for each new target a filter chooses. A target in
	// another looper drops the message.
	target = _ApplyFilters(fCommonFilters, message, target);
	for (int round = 0; target != nullptr && round < 64; round++) {
		if (target->fLooper != this)
			return nullptr;
		BHandler* next = _ApplyFilters(target->fFilters, message, target);
		if (next == target)
			return target;
		target = next;
	}
	return target != nullptr && target->fLooper == this ? target : nullptr;
}


BMessageFilter*
BLooper::AddCommonFilter(std::unique_ptr<BMessageFilter> filter)
{
	assert(_OnOwnThread() && "BLooper::AddCommonFilter() from another thread");
	if (filter == nullptr)
		return nullptr;
	filter->fOwner = this;
	fCommonFilters.push_back(std::move(filter));
	return fCommonFilters.back().get();
}


std::unique_ptr<BMessageFilter>
BLooper::RemoveCommonFilter(BMessageFilter* filter)
{
	assert(_OnOwnThread() && "BLooper::RemoveCommonFilter() from another thread");
	const auto found = std::find_if(fCommonFilters.begin(), fCommonFilters.end(),
		[filter](const std::unique_ptr<BMessageFilter>& owned) { return owned.get() == filter; });
	if (found == fCommonFilters.end())
		return nullptr;
	std::unique_ptr<BMessageFilter> removed = std::move(*found);
	fCommonFilters.erase(found);
	removed->fOwner = nullptr;
	return removed;
}


std::vector<BMessageFilter*>
BLooper::CommonFilterList() const
{
	std::vector<BMessageFilter*> filters;
	for (const std::unique_ptr<BMessageFilter>& filter : fCommonFilters)
		filters.push_back(filter.get());
	return filters;
}


void
BLooper::Quit()
{
	assert(_OnOwnThread() && "BLooper::Quit() from another thread; send kQuitRequested");
	fQuitting = true;
}


bool
BLooper::QuitRequested()
{
	return true;
}


void
BLooper::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (message->what == kQuitRequested && handler == this) {
		const bool quit = QuitRequested();
		if (message->IsSourceWaiting()) {
			BMessage reply(kReply);
			(void)reply.AddBool("result", quit);
			(void)message->SendReply(reply);
		}
		if (quit)
			Quit();
		return;
	}
	handler->MessageReceived(message);
}


void
BLooper::MessageReceived(BMessage* message)
{
	BHandler::MessageReceived(message);
}


std::expected<void, Error>
BLooper::PostMessage(std::uint32_t command)
{
	return PostMessage(BMessage(command));
}


std::expected<void, Error>
BLooper::PostMessage(const BMessage& message, BHandler* handler, BHandler* replyTo)
{
	assert(_OnOwnThread() && "BLooper::PostMessage() from another thread; use a BMessenger");
	if (handler != nullptr && handler->fLooper != this)
		return Failure(ErrorCode::InvalidArgument, "the handler is in another looper");
	const BMessenger target(handler, this);
	return target.SendMessage(message, replyTo != nullptr ? BMessenger(replyTo) : BMessenger());
}


void
BLooper::AddHandler(BHandler* handler)
{
	assert(_OnOwnThread() && "BLooper::AddHandler() from another thread");
	assert((handler != nullptr && handler->fLooper == nullptr)
		&& "BLooper::AddHandler() with a handler that is in a looper");
	if (handler == nullptr || handler->fLooper != nullptr || !_OnOwnThread())
		return;
	handler->fLooper = this;
	if (handler->fNextHandler == nullptr)
		handler->fNextHandler = this;
	fHandlers.push_back(handler);
}


bool
BLooper::RemoveHandler(BHandler* handler)
{
	assert(_OnOwnThread() && "BLooper::RemoveHandler() from another thread");
	if (handler == this || handler == nullptr || handler->fLooper != this)
		return false;
	fHandlers.erase(std::remove(fHandlers.begin(), fHandlers.end(), handler), fHandlers.end());
	handler->fLooper = nullptr;
	handler->fNextHandler = nullptr;
	if (fPreferred == handler)
		fPreferred = nullptr;
	// Handlers that passed on to the removed one now pass on to the looper.
	for (BHandler* other : fHandlers) {
		if (other->fNextHandler == handler)
			other->fNextHandler = this;
	}
	return true;
}


std::int32_t
BLooper::CountHandlers() const
{
	return static_cast<std::int32_t>(fHandlers.size());
}


BHandler*
BLooper::HandlerAt(std::int32_t index) const
{
	if (index < 0 || static_cast<std::size_t>(index) >= fHandlers.size())
		return nullptr;
	return fHandlers[static_cast<std::size_t>(index)];
}


std::int32_t
BLooper::IndexOf(const BHandler* handler) const
{
	const auto found = std::find(fHandlers.begin(), fHandlers.end(), handler);
	return found != fHandlers.end() ? static_cast<std::int32_t>(found - fHandlers.begin()) : -1;
}


BHandler*
BLooper::PreferredHandler() const
{
	return fPreferred;
}


void
BLooper::SetPreferredHandler(BHandler* handler)
{
	assert((handler == nullptr || handler->fLooper == this)
		&& "BLooper::SetPreferredHandler() with a handler of another looper");
	if (handler != nullptr && handler->fLooper != this)
		return;
	fPreferred = handler;
}


BMessage*
BLooper::CurrentMessage() const
{
	return fCurrent.get();
}


std::unique_ptr<BMessage>
BLooper::DetachCurrentMessage()
{
	return std::move(fCurrent);
}


bool
BLooper::Lock()
{
	assert(_OnOwnThread() && "BLooper::Lock() from another thread; send a message");
	return fLock.Lock();
}


void
BLooper::Unlock()
{
	fLock.Unlock();
}


bool
BLooper::IsLocked() const
{
	return fLock.IsLocked();
}


std::thread::id
BLooper::Thread() const
{
	return fThread;
}


} // namespace ngui
