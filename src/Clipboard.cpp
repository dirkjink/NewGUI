/*
 * Copyright 2001-2005, Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * After Haiku's src/kits/app/Clipboard.cpp (commit b1d497bb69): its
 * interface, with the content kept in the process per name (Haiku: by the
 * registrar) and, for the system clipboard, in the platform's clipboard
 * (see Clipboard.h).
 *
 * Authors (Haiku):
 *		Gabe Yoder (gyoder@stny.rr.com)
 */


#include <ngui/Clipboard.h>

#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include <ngui/platform/Platform.h>


namespace ngui {


namespace {


constexpr std::string_view kSystemName = "system";


} // namespace


// What the clipboards of one name share.
struct BClipboard::Store {
	std::mutex				lock;
	BMessage				data;
	uint32					count = 0;

	static std::shared_ptr<Store> Named(std::string_view name)
	{
		static std::mutex sLock;
		static std::map<std::string, std::weak_ptr<Store>, std::less<>> sStores;
		std::scoped_lock guard(sLock);
		auto found = sStores.find(name);
		if (found != sStores.end()) {
			if (std::shared_ptr<Store> store = found->second.lock())
				return store;
		}
		auto store = std::make_shared<Store>();
		sStores[std::string(name)] = store;
		return store;
	}
};


BClipboard::BClipboard(std::string_view name)
	:
	fName(name),
	fStore(Store::Named(name))
{
}


BClipboard::~BClipboard()
{
}


const std::string&
BClipboard::Name() const
{
	return fName;
}


uint32
BClipboard::LocalCount() const
{
	return fCount;
}


uint32
BClipboard::SystemCount() const
{
	std::scoped_lock guard(fStore->lock);
	return fStore->count;
}


bool
BClipboard::Lock()
{
	if (!fLock.Lock())
		return false;

	_DownloadFromSystem();
	return true;
}


void
BClipboard::Unlock()
{
	fLock.Unlock();
}


bool
BClipboard::IsLocked() const
{
	return fLock.IsLocked();
}


std::expected<void, Error>
BClipboard::Clear()
{
	if (!IsLocked())
		return Failure(ErrorCode::InvalidArgument, "the clipboard is not locked");

	fData.MakeEmpty();
	return {};
}


std::expected<void, Error>
BClipboard::Commit()
{
	return Commit(false);
}


std::expected<void, Error>
BClipboard::Commit(bool failIfChanged)
{
	if (!IsLocked())
		return Failure(ErrorCode::InvalidArgument, "the clipboard is not locked");

	if (failIfChanged && SystemCount() != fCount)
		return Failure(ErrorCode::InvalidArgument, "the clipboard changed");

	return _UploadToSystem();
}


std::expected<void, Error>
BClipboard::Revert()
{
	if (!IsLocked())
		return Failure(ErrorCode::InvalidArgument, "the clipboard is not locked");

	_DownloadFromSystem();
	return {};
}


BMessage*
BClipboard::Data() const
{
	return IsLocked() ? &fData : nullptr;
}


BClipboard&
BClipboard::System()
{
	static BClipboard sSystem(kSystemName);
	return sSystem;
}


void
BClipboard::_DownloadFromSystem()
{
	// The system clipboard comes from the platform, if it has one.
	platform::Platform* platform = fName == kSystemName ? platform::Current() : nullptr;
	if (platform != nullptr) {
		if (auto data = platform->Clipboard()) {
			BMessage content;
			for (const auto& [mime, bytes] : *data)
				(void)content.AddData(mime, BTypeCode::Mime, bytes);
			std::scoped_lock guard(fStore->lock);
			fStore->data = std::move(content);
		}
	}

	std::scoped_lock guard(fStore->lock);
	fData = fStore->data;
	fCount = fStore->count;
}


std::expected<void, Error>
BClipboard::_UploadToSystem()
{
	{
		std::scoped_lock guard(fStore->lock);
		fStore->data = fData;
		fStore->count++;
		fCount = fStore->count;
	}

	platform::Platform* platform = fName == kSystemName ? platform::Current() : nullptr;
	if (platform == nullptr)
		return {};

	platform::ClipboardData data;
	for (const BFieldInfo& field : fData.Fields(BTypeCode::Mime)) {
		if (auto bytes = fData.FindData(field.name, BTypeCode::Mime))
			data.emplace_back(field.name, std::vector<std::byte>(bytes->begin(), bytes->end()));
	}
	if (auto result = platform->SetClipboard(data); !result.has_value()
		&& result.error().code != ErrorCode::NotSupported) {
		return result;
	}
	return {};
}


} // namespace ngui
