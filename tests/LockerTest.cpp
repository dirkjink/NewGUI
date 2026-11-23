/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Locker.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/Autolock.h>


namespace {


using namespace ngui;
using namespace std::chrono_literals;


// Runs \a body on another thread and waits for it.
template<typename Body>
void
OnOtherThread(Body body)
{
	std::thread thread(body);
	thread.join();
}


TEST(LockerTest, TheOwnerMayLockAgain)
{
	// What is tested: Lock() twice from one thread, the state queries, and
	// Unlock() twice.
	// Success criteria: Both locks succeed; while held, IsLocked() is true
	// for the owner and false for another thread, LockingThread() is the
	// owner and CountLocks() counts both; after two Unlock() calls the
	// locker is free.
	// Why it matters: BLocker is recursive like Haiku's: a looper locks
	// itself while dispatching, and the handlers it calls lock it again.
	//
	// Given
	BLocker locker("test");
	bool lockedElsewhere = true;

	// When / Then
	EXPECT_TRUE(locker.Lock());
	EXPECT_TRUE(locker.Lock());
	EXPECT_TRUE(locker.IsLocked());
	EXPECT_EQ(locker.LockingThread(), std::this_thread::get_id());
	EXPECT_EQ(locker.CountLocks(), 2);
	EXPECT_EQ(locker.CountLockRequests(), 1);
	OnOtherThread([&] { lockedElsewhere = locker.IsLocked(); });
	EXPECT_FALSE(lockedElsewhere);
	locker.Unlock();
	EXPECT_EQ(locker.CountLocks(), 1);
	locker.Unlock();
	EXPECT_FALSE(locker.IsLocked());
	EXPECT_EQ(locker.LockingThread(), std::thread::id());
	EXPECT_EQ(locker.CountLocks(), 0);
	EXPECT_EQ(locker.Name(), "test");
}


TEST(LockerTest, TimeoutsEndTheWait)
{
	// What is tested: LockWithTimeout() and try_lock() from another thread
	// while the lock is held, with 0, 20 ms and a negative timeout, and
	// again after the lock is released.
	// Success criteria: While held, every attempt fails with TimedOut, the
	// 20 ms one after at least 20 ms; after release the other thread gets
	// the lock at once, and LockWithTimeout() by the owner always succeeds.
	// Why it matters: Haiku's LockWithTimeout() lets a caller give up, e.g.
	// BLooper::LockLooperWithTimeout(); B_WOULD_BLOCK and B_TIMED_OUT become
	// ErrorCode::TimedOut.
	//
	// Given
	BLocker locker;
	locker.Lock();
	ErrorCode zero{};
	ErrorCode twenty{};
	ErrorCode negative{};
	bool tried = true;
	std::chrono::steady_clock::duration waited{};

	// When
	OnOtherThread([&] {
		zero = locker.LockWithTimeout(0us).error().code;
		const auto start = std::chrono::steady_clock::now();
		twenty = locker.LockWithTimeout(20ms).error().code;
		waited = std::chrono::steady_clock::now() - start;
		negative = locker.LockWithTimeout(-5ms).error().code;
		tried = locker.try_lock();
	});
	const bool again = locker.LockWithTimeout(0us).has_value();
	locker.Unlock();
	locker.Unlock();
	bool later = false;
	OnOtherThread([&] {
		later = locker.LockWithTimeout(0us).has_value();
		locker.Unlock();
	});

	// Then
	EXPECT_EQ(zero, ErrorCode::TimedOut);
	EXPECT_EQ(twenty, ErrorCode::TimedOut);
	EXPECT_GE(waited, 20ms);
	EXPECT_EQ(negative, ErrorCode::TimedOut);
	EXPECT_FALSE(tried);
	EXPECT_TRUE(again);
	EXPECT_TRUE(later);
}


TEST(LockerTest, WaitingThreadsAreCounted)
{
	// What is tested: CountLockRequests() while one thread holds the lock
	// and another waits for it, and the hand-over when the holder unlocks.
	// Success criteria: Two requests while one waits; the waiting thread
	// gets the lock once it is released, and afterwards there are none.
	// Why it matters: Haiku's CountLockRequests() tells whether others
	// wait; a waiter must wake up when the lock becomes free.
	//
	// Given
	BLocker locker;
	locker.Lock();
	std::atomic<bool> got = false;
	std::thread waiter([&] {
		locker.Lock();
		got = true;
		locker.Unlock();
	});

	// When
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (locker.CountLockRequests() < 2 && std::chrono::steady_clock::now() < deadline)
		std::this_thread::yield();
	const std::int32_t requests = locker.CountLockRequests();
	const bool gotEarly = got;
	locker.Unlock();
	waiter.join();

	// Then
	EXPECT_EQ(requests, 2);
	EXPECT_FALSE(gotEarly);
	EXPECT_TRUE(got);
	EXPECT_EQ(locker.CountLockRequests(), 0);
}


TEST(LockerTest, LockingExcludesOtherThreads)
{
	// What is tested: Four threads each increment a plain counter 20,000
	// times, under std::scoped_lock on the locker with a nested BAutolock.
	// Success criteria: The counter ends at exactly 80,000.
	// Why it matters: Mutual exclusion is the point of the lock; the
	// Lockable interface makes std::scoped_lock usable, as the conventions
	// prefer it to AutoLock (CONVENTIONS.md 4.3).
	//
	// Given
	BLocker locker;
	std::int64_t counter = 0;

	// When
	std::vector<std::thread> threads;
	for (int t = 0; t < 4; t++) {
		threads.emplace_back([&] {
			for (int i = 0; i < 20000; i++) {
				std::scoped_lock outer(locker);
				BAutolock inner(locker);
				counter++;
			}
		});
	}
	for (std::thread& thread : threads)
		thread.join();

	// Then
	EXPECT_EQ(counter, 80000);
	EXPECT_EQ(locker.CountLocks(), 0);
}


TEST(LockerTest, UnlockWithoutLockIsAProgrammingError)
{
	// What is tested: Unlock() on a locker the calling thread does not hold.
	// Success criteria: Debug builds stop with an assertion; release builds
	// ignore the call, and the locker still works.
	// Why it matters: Haiku stops in the debugger here; releasing someone
	// else's lock would break the mutual exclusion silently.
	//
	// Given
	BLocker locker;

	// When / Then
	EXPECT_DEBUG_DEATH(locker.Unlock(), "does not hold the lock");
	EXPECT_TRUE(locker.Lock());
	EXPECT_EQ(locker.CountLocks(), 1);
	locker.Unlock();
}


TEST(LockerTest, AutolockHoldsForItsScope)
{
	// What is tested: BAutolock with a locker, with nullptr, and with an
	// early Unlock().
	// Success criteria: It holds the lock until its scope ends or Unlock()
	// is called, and only once; with nullptr it holds nothing.
	// Why it matters: The classic Haiku pattern `BAutolock lock(x); if
	// (!lock.IsLocked()) return;` must work as it does there.
	//
	// Given
	BLocker locker;

	// When / Then
	{
		BAutolock lock(locker);
		EXPECT_TRUE(lock.IsLocked());
		EXPECT_TRUE(locker.IsLocked());
		lock.Unlock();
		EXPECT_FALSE(lock.IsLocked());
		EXPECT_FALSE(locker.IsLocked());
	}
	EXPECT_EQ(locker.CountLocks(), 0);
	{
		BAutolock lock(&locker);
		EXPECT_EQ(locker.CountLocks(), 1);
	}
	EXPECT_EQ(locker.CountLocks(), 0);
	BAutolock none(nullptr);
	EXPECT_FALSE(none.IsLocked());
}


} // namespace
