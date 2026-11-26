/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Looper.h>

#include <algorithm>
#include <cassert>
#include <string_view>
#include <vector>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Autolock.h>
#include <ngui/PropertyInfo.h>

#include "PlatformEvent.h"
#include "record/Recorder.h"
#include "LooperPort.h"


namespace ngui {


namespace {


// The properties of every looper (Haiku's sLooperPropInfo, without
// "Handlers": its messengers cannot be flattened for remote control).
constexpr std::uint32_t kProcessInternally = 1;
constexpr std::uint32_t kHandlerByIndex = 2;
constexpr std::int32_t kCountHandlersEntry = 1;

const std::vector<BPropertyInfoEntry> kLooperProperties = {
	{"Handler", {}, {kIndexSpecifier, kReverseIndexSpecifier},
		"The handler at an index; leads on to its properties.", kHandlerByIndex, {}},
	{"Handler", {kCountProperties}, {kDirectSpecifier}, "How many handlers it has.",
		kProcessInternally, {BTypeCode::Int32}},
};


} // namespace


BLooper::BLooper(std::string_view name)
	:
	BHandler(name),
	fPort(LooperPort::Create()),
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
	// A looper that ran on a thread of its own is finished by that thread
	// after this destructor; one that ran on its caller's thread
	// (BApplication) or never ran is finished here.
	if (!fOwnThread)
		fPort->Finish();
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
	looper->fOwnThread = true;
	std::thread([owned = std::move(looper)]() mutable {
		owned->fThread = std::this_thread::get_id();
		owned->fPort->SetThread(owned->fThread);
		owned->fPort->SetName(owned->Name());
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
BLooper::_RunOnThisThread(EventSource* source)
{
	assert(!fRunning && "the looper runs already");
	if (fRunning)
		return;
	fRunning = true;
	fThread = std::this_thread::get_id();
	fPort->SetThread(fThread);
	fPort->SetName(Name());
	if (source == nullptr) {
		_Loop();
		return;
	}

	// The messages and the events of the source, in turn. A message that
	// arrives while the source waits wakes it; one that arrived before
	// WaitForEvents() makes it return at once, since Wake() was called.
	fPort->SetWaker([source] { source->Wake(); });
	while (!fQuitting) {
		source->ProcessEvents();
		if (fQuitting)
			break;
		std::unique_ptr<BMessage> message = fPort->TryPop();
		if (message != nullptr) {
			_Dispatch(std::move(message));
			continue;
		}
		if (!fPort->IsOpen())
			break;
		source->WaitForEvents();
	}
	fPort->SetWaker(nullptr);
	fPort->Close();
}


void
BLooper::_Loop()
{
	while (!fQuitting) {
		std::unique_ptr<BMessage> message = fPort->Pop();
		if (message == nullptr)
			break;
		_Dispatch(std::move(message));
	}
	fPort->Close();
}


void
BLooper::_Dispatch(std::unique_ptr<BMessage> message)
{
	if (message->fDelivery == nullptr)
		message->fDelivery = std::make_unique<MessageDelivery>();
	message->fDelivery->delivered = true;

	BAutolock lock(fLock);
	fCurrent = std::move(message);
	// the test kit's work on this thread (RunOnLooper())
	if (RunRequestedWork(*this, *fCurrent)) {
		fCurrent.reset();
		return;
	}
	// an event of the platform, as the looper gets it (Record & Replay,
	// specification 20.8), before the filters
	if (PlatformEventMark::Take(*fCurrent))
		detail::RecordPlatformEvent(*this, _Target(*fCurrent), *fCurrent);
	BHandler* handler = _FilteredTarget(fCurrent.get(), _Target(*fCurrent));
	// a scripting message goes to the handler its specifiers name
	if (handler != nullptr && fCurrent->HasSpecifiers())
		handler = _ResolveSpecifier(handler, fCurrent.get());
	// A message for a handler that left the looper is dropped; its waiting
	// sender gets kNoReply.
	if (handler != nullptr)
		DispatchMessage(fCurrent.get(), handler);
	fCurrent.reset();
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
	if (message->what == kQuitNow && handler == this) {
		Quit();
		return;
	}
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
	// Ported from Haiku: src/kits/app/Looper.cpp (commit b1d497bb69).
	if (message->what == kCountProperties && message->HasSpecifiers()) {
		const auto specifier = message->GetCurrentSpecifier();
		const BPropertyInfo properties(kLooperProperties);
		if (specifier.has_value() && properties.FindMatch(*message, specifier->index,
				specifier->form, specifier->property) == kCountHandlersEntry) {
			BMessage reply(kReply);
			(void)reply.AddInt32("result", CountHandlers());
			(void)reply.AddInt32("error", 0);
			(void)message->SendReply(reply);
			return;
		}
	}
	BHandler::MessageReceived(message);
}


BHandler*
BLooper::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
	std::uint32_t form, std::string_view property)
{
	const BPropertyInfo properties(kLooperProperties);
	std::uint32_t data = 0;
	if (properties.FindMatch(*message, index, form, property, &data) < 0)
		return BHandler::ResolveSpecifier(message, index, specifier, form, property);
	if (data == kProcessInternally)
		return this;
	// kHandlerByIndex
	std::int32_t handlerIndex = specifier.GetInt32("index", -1);
	if (form == kReverseIndexSpecifier)
		handlerIndex = CountHandlers() - handlerIndex;
	BHandler* target = HandlerAt(handlerIndex);
	if (target == nullptr) {
		ReplyScriptingError(*message, {ErrorCode::OutOfRange, "handler index out of range"});
		return nullptr;
	}
	(void)message->PopSpecifier();
	return target;
}


std::expected<void, Error>
BLooper::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-looper"); !added)
		return added;
	if (auto added = data->AddMessage("messages", BPropertyInfo(kLooperProperties).ToMessage());
			!added) {
		return added;
	}
	return BHandler::GetSupportedSuites(data);
}


// Haiku's BLooper::resolve_specifier(): each handler turns the current
// specifier into the next one, until one keeps the message or none is
// left; nullptr if a handler replied an error or left the looper.
BHandler*
BLooper::_ResolveSpecifier(BHandler* target, BMessage* message)
{
	BHandler* next = target;
	do {
		const auto specifier = message->GetCurrentSpecifier();
		if (!specifier.has_value()) {
			BMessage reply(kReply);
			(void)reply.AddInt32("error", ScriptingStatus(std::unexpected(specifier.error())));
			(void)reply.AddString("message", specifier.error().message);
			(void)message->SendReply(reply);
			return nullptr;
		}
		target = next;
		next = target->ResolveSpecifier(message, specifier->index, specifier->specifier,
			specifier->form, specifier->property);
		if (next == nullptr || IndexOf(next) < 0)
			return nullptr;
	} while (next != target && message->CurrentSpecifierIndex() >= 0);
	return next;
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
