/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Locker.h>

#include <algorithm>
#include <cassert>


namespace ngui {


BLocker::BLocker(std::string_view name)
	:
	fName(name)
{
}


BLocker::~BLocker() = default;


bool
BLocker::Lock()
{
	return LockWithTimeout(kInfiniteTimeout).has_value();
}


std::expected<void, Error>
BLocker::LockWithTimeout(std::chrono::microseconds timeout)
{
	const std::thread::id self = std::this_thread::get_id();
	std::unique_lock guard(fMutex);
	if (fOwner == self) {
		fCount++;
		return {};
	}

	const auto isFree = [this] { return fCount == 0; };
	// A steady deadline, so that changes of the system time do not shorten
	// or lengthen the wait. A timeout beyond the range of the clock counts
	// as infinite, so that the deadline cannot overflow.
	const auto now = std::chrono::steady_clock::now();
	const auto longest = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::time_point::max() - now);
	const std::chrono::microseconds wait = std::max(timeout, std::chrono::microseconds::zero());
	fWaiting++;
	bool acquired = true;
	if (wait >= longest)
		fFree.wait(guard, isFree);
	else
		acquired = fFree.wait_until(guard, now + wait, isFree);
	fWaiting--;
	if (!acquired)
		return Failure(ErrorCode::TimedOut, "the lock \"" + fName + "\" is held");
	fOwner = self;
	fCount = 1;
	return {};
}


void
BLocker::Unlock()
{
	std::unique_lock guard(fMutex);
	const bool owned = fOwner == std::this_thread::get_id() && fCount > 0;
	assert(owned && "BLocker::Unlock() from a thread that does not hold the lock");
	if (!owned)
		return;
	if (--fCount > 0)
		return;
	fOwner = std::thread::id();
	guard.unlock();
	fFree.notify_one();
}


std::thread::id
BLocker::LockingThread() const
{
	std::scoped_lock guard(fMutex);
	return fOwner;
}


bool
BLocker::IsLocked() const
{
	std::scoped_lock guard(fMutex);
	return fCount > 0 && fOwner == std::this_thread::get_id();
}


std::int32_t
BLocker::CountLocks() const
{
	std::scoped_lock guard(fMutex);
	return fCount;
}


std::int32_t
BLocker::CountLockRequests() const
{
	std::scoped_lock guard(fMutex);
	return fWaiting + (fCount > 0 ? 1 : 0);
}


const std::string&
BLocker::Name() const
{
	return fName;
}


void
BLocker::lock()
{
	Lock();
}


bool
BLocker::try_lock()
{
	return LockWithTimeout(std::chrono::microseconds::zero()).has_value();
}


void
BLocker::unlock()
{
	Unlock();
}


} // namespace ngui
