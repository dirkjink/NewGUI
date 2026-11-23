/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/MessageRunner.h>

#include <mutex>
#include <utility>


namespace ngui {


// Shared with the scheduled action, which holds it weakly: a runner that is
// destroyed stops at once, even while the clock is about to run it.
struct BMessageRunner::State {
	std::mutex			lock;
	Clock&				clock;
	BMessenger			target;
	BMessage			message;
	BMessenger			replyTo;
	Clock::Time			interval;
	std::int32_t		count;
	Clock::TimerId		timer = 0;
	bool				valid;
	bool				detached = false;	// StartSending(): no runner object

	State(Clock& theClock, BMessenger theTarget, const BMessage& theMessage,
		BMessenger theReplyTo, Clock::Time theInterval, std::int32_t theCount)
		:
		clock(theClock),
		target(std::move(theTarget)),
		message(theMessage),
		replyTo(std::move(theReplyTo)),
		interval(theInterval),
		count(theCount),
		valid(theInterval > Clock::Time::zero() && theCount != 0 && target.IsValid())
	{
	}

	// Schedules the next message one interval after \a from; with lock held.
	// A runner object holds its state, so the action holds it weakly; a
	// detached one (StartSending()) lives through its scheduled actions.
	static void ScheduleNext(const std::shared_ptr<State>& state, Clock::Time from)
	{
		const Clock::Time due = from + state->interval;
		if (state->detached) {
			state->timer = state->clock.Schedule(due, [state, due] { Fire(state, due); });
			return;
		}
		const std::weak_ptr<State> weak = state;
		state->timer = state->clock.Schedule(due, [weak, due] {
			if (const std::shared_ptr<State> alive = weak.lock())
				Fire(alive, due);
		});
	}

	static void Fire(const std::shared_ptr<State>& state, Clock::Time due)
	{
		std::scoped_lock guard(state->lock);
		if (state->count == 0)
			return;
		const bool sent = state->target.SendMessage(state->message, state->replyTo)
			.has_value();
		if (state->count > 0)
			state->count--;
		// A target that is gone ends the runner, as Haiku's does.
		if (!sent)
			state->count = 0;
		if (state->count != 0)
			ScheduleNext(state, due);
		else
			state->timer = 0;
	}
};


BMessageRunner::BMessageRunner(BMessenger target, const BMessage& message,
	Clock::Time interval, std::int32_t count, BMessenger replyTo, Clock* clock)
	:
	fState(std::make_shared<State>(clock != nullptr ? *clock : Clock::Default(),
		std::move(target), message, std::move(replyTo), interval, count))
{
	if (!fState->valid)
		return;
	std::scoped_lock guard(fState->lock);
	State::ScheduleNext(fState, fState->clock.Now());
}


BMessageRunner::~BMessageRunner()
{
	std::scoped_lock guard(fState->lock);
	if (fState->timer != 0)
		fState->clock.Cancel(fState->timer);
	fState->count = 0;
}


std::expected<void, Error>
BMessageRunner::InitCheck() const
{
	if (!fState->valid)
		return Failure(ErrorCode::InvalidArgument, "bad interval, count or target");
	return {};
}


std::expected<void, Error>
BMessageRunner::SetInterval(Clock::Time interval)
{
	if (!fState->valid)
		return InitCheck();
	if (interval <= Clock::Time::zero())
		return Failure(ErrorCode::InvalidArgument, "the interval must be positive");
	std::scoped_lock guard(fState->lock);
	fState->interval = interval;
	if (fState->timer != 0) {
		fState->clock.Cancel(fState->timer);
		State::ScheduleNext(fState, fState->clock.Now());
	}
	return {};
}


std::expected<void, Error>
BMessageRunner::SetCount(std::int32_t count)
{
	if (!fState->valid)
		return InitCheck();
	if (count < -1)
		return Failure(ErrorCode::InvalidArgument, "the count must be -1 or more");
	std::scoped_lock guard(fState->lock);
	fState->count = count;
	if (count == 0 && fState->timer != 0) {
		fState->clock.Cancel(fState->timer);
		fState->timer = 0;
	} else if (count != 0 && fState->timer == 0) {
		State::ScheduleNext(fState, fState->clock.Now());
	}
	return {};
}


Clock::Time
BMessageRunner::Interval() const
{
	std::scoped_lock guard(fState->lock);
	return fState->interval;
}


std::int32_t
BMessageRunner::Count() const
{
	std::scoped_lock guard(fState->lock);
	return fState->count;
}


std::expected<void, Error>
BMessageRunner::StartSending(BMessenger target, const BMessage& message,
	Clock::Time interval, std::int32_t count, Clock* clock)
{
	if (count < 1)
		return Failure(ErrorCode::InvalidArgument, "StartSending() needs a count of at least 1");
	auto state = std::make_shared<State>(clock != nullptr ? *clock : Clock::Default(),
		std::move(target), message, BMessenger(), interval, count);
	if (!state->valid)
		return Failure(ErrorCode::InvalidArgument, "bad interval or target");
	state->detached = true;
	std::scoped_lock guard(state->lock);
	State::ScheduleNext(state, state->clock.Now());
	return {};
}


} // namespace ngui
