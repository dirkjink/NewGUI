/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <ngui/Error.h>


namespace ngui {


/*!	A recursive lock that knows its owner thread, as Haiku's BLocker
	(module G1): the thread that holds it may lock it again and must unlock
	it as often. Loopers and windows build on it.

	Differences to Haiku:
	- LockWithTimeout() takes a std::chrono duration and returns
	  std::expected; a timeout or a zero timeout on a held lock gives
	  ErrorCode::TimedOut (B_TIMED_OUT, B_WOULD_BLOCK).
	- Thread ids are std::thread::id; there is no semaphore (Sem()) and no
	  benaphore option, the lock is always cheap when it is free.
	- lock(), try_lock() and unlock() make it a Lockable, so that
	  std::scoped_lock works (CONVENTIONS.md 4.3).
	- Unlocking from a thread that does not hold the lock is a programming
	  error: debug builds stop with an assertion, release builds ignore it.
	- Destroying a locker that a thread holds or waits for is undefined, as
	  for every C++ mutex.
*/
class BLocker {
public:
	/*!	Wait without a time limit (B_INFINITE_TIMEOUT). */
	static constexpr std::chrono::microseconds kInfiniteTimeout
		= std::chrono::microseconds::max();

								BLocker(std::string_view name = {});
									/*!< An unlocked locker; \a name is for debugging. */
								~BLocker();
									/*!< See the class description. */

								BLocker(const BLocker&) = delete;
									/*!< Not copyable. */
			BLocker&			operator=(const BLocker&) = delete;
									/*!< Not copyable. */

			bool				Lock();
									/*!< Waits until the calling thread holds the lock;
										always true. */
			std::expected<void, Error> LockWithTimeout(std::chrono::microseconds timeout);
									/*!< As Lock(), but waits at most \a timeout; TimedOut if
										the lock stays held by another thread. */
			void				Unlock();
									/*!< Releases one level; the lock is free when every
										Lock() is matched. */

			std::thread::id		LockingThread() const;
									/*!< The holder; std::thread::id() if it is free. */
			bool				IsLocked() const;
									/*!< Whether the calling thread holds the lock. */
			std::int32_t		CountLocks() const;
									/*!< How often the holder has locked it, 0 if free. */
			std::int32_t		CountLockRequests() const;
									/*!< Threads that hold or wait for the lock. */
			const std::string&	Name() const;
									/*!< The name given to the constructor. */

			void				lock();
									/*!< Lock(), for std::scoped_lock. */
			bool				try_lock();
									/*!< LockWithTimeout(0) succeeded. */
			void				unlock();
									/*!< Unlock(). */

private:
			std::string			fName;
	mutable	std::mutex			fMutex;
			std::condition_variable fFree;
			std::thread::id		fOwner;
			std::int32_t		fCount = 0;
			std::int32_t		fWaiting = 0;
};


} // namespace ngui
