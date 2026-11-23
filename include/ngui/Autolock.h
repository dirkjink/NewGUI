/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <ngui/Locker.h>


namespace ngui {


/*!	Locks a BLocker for the lifetime of a scope, as Haiku's BAutolock
	(module G1). Unlike std::scoped_lock it can be asked whether the lock
	was taken and can unlock early; with loopers (G3) locking may fail.

	\code
	BAutolock lock(fLocker);
	if (!lock.IsLocked())
		return;
	\endcode
*/
class BAutolock {
public:
	explicit					BAutolock(BLocker& locker);
									/*!< Locks \a locker. */
	explicit					BAutolock(BLocker* locker);
									/*!< Locks \a locker; holds nothing if it is nullptr. */
								~BAutolock();
									/*!< Unlocks, unless Unlock() did already. */

								BAutolock(const BAutolock&) = delete;
									/*!< Not copyable. */
			BAutolock&			operator=(const BAutolock&) = delete;
									/*!< Not copyable. */

			bool				IsLocked() const;
									/*!< Whether this object holds the lock. */
			void				Unlock();
									/*!< Unlocks early; the destructor then does nothing. */

private:
			BLocker*			fLocker;
			bool				fLocked;
};


inline
BAutolock::BAutolock(BLocker& locker)
	:
	BAutolock(&locker)
{
}


inline
BAutolock::BAutolock(BLocker* locker)
	:
	fLocker(locker),
	fLocked(locker != nullptr && locker->Lock())
{
}


inline
BAutolock::~BAutolock()
{
	Unlock();
}


inline bool
BAutolock::IsLocked() const
{
	return fLocked;
}


inline void
BAutolock::Unlock()
{
	if (fLocked) {
		fLocker->Unlock();
		fLocked = false;
	}
}


} // namespace ngui
