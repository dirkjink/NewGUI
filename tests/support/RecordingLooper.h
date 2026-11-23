/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <ngui/Looper.h>
#include <ngui/Message.h>


namespace ngui::test {


// What a RecordingLooper saw. Shared, because the looper is destroyed on
// its own thread and the test reads the record afterwards.
struct LooperRecord {
	std::mutex lock;
	std::condition_variable changed;
	std::vector<std::string> received;	// "<handler>:<what as number>"
	std::vector<std::thread::id> threads;
	std::thread::id destroyedOn;
	bool destroyed = false;

	void Add(const std::string& entry)
	{
		{
			std::scoped_lock guard(lock);
			received.push_back(entry);
			threads.push_back(std::this_thread::get_id());
		}
		changed.notify_all();
	}

	// Waits until \a count entries are there; false after 5 s.
	bool WaitFor(std::size_t count)
	{
		std::unique_lock guard(lock);
		return changed.wait_for(guard, std::chrono::seconds(5),
			[&] { return received.size() >= count; });
	}

	// Waits until \a entry has been received \a times times; false after 5 s.
	bool WaitForEntry(const std::string& entry, std::size_t times)
	{
		std::unique_lock guard(lock);
		return changed.wait_for(guard, std::chrono::seconds(5), [&] {
			return static_cast<std::size_t>(std::count(received.begin(), received.end(), entry))
				>= times;
		});
	}

	std::vector<std::string> Received()
	{
		std::scoped_lock guard(lock);
		return received;
	}
};


// A handler that records what it receives and then runs an optional
// action, e.g. to reply.
class RecordingHandler : public BHandler {
public:
	RecordingHandler(std::string_view name, std::shared_ptr<LooperRecord> record)
		:
		BHandler(name),
		fRecord(std::move(record))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		fRecord->Add(Name() + ":" + std::to_string(message->what));
		if (action)
			action(*message);
		else if (passOn)
			BHandler::MessageReceived(message);
	}

	std::function<void(BMessage&)> action;
	bool passOn = false;

private:
	std::shared_ptr<LooperRecord> fRecord;
};


// A looper that records what reaches it and where it is destroyed.
class RecordingLooper : public BLooper {
public:
	explicit RecordingLooper(std::shared_ptr<LooperRecord> record)
		:
		BLooper("recording"),
		fRecord(std::move(record))
	{
	}

	~RecordingLooper() override
	{
		std::scoped_lock guard(fRecord->lock);
		fRecord->destroyed = true;
		fRecord->destroyedOn = std::this_thread::get_id();
	}

	void MessageReceived(BMessage* message) override
	{
		fRecord->Add("looper:" + std::to_string(message->what));
		if (action)
			action(*message);
	}

	bool QuitRequested() override
	{
		return quitRequested ? quitRequested() : true;
	}

	std::function<void(BMessage&)> action;
	std::function<bool()> quitRequested;

private:
	std::shared_ptr<LooperRecord> fRecord;
};


} // namespace ngui::test
