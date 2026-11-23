/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <ngui/platform/Platform.h>


namespace ngui::test {


// A platform without a window system, for tests of the loop integration:
// "events" are functions injected from any thread and run by
// ProcessEvents() on the main thread. Windows are not supported.
class FakePlatform : public platform::Platform {
public:
	std::string_view Name() const override
	{
		return "fake";
	}

	std::expected<std::unique_ptr<platform::PlatformWindow>, Error> CreateWindow(
		const platform::WindowSpec&) override
	{
		return Failure(ErrorCode::NotSupported, "no windows");
	}

	std::vector<platform::DisplayInfo> Displays() const override
	{
		return {{{0, 0, 800, 600}, 1, true}};
	}

	void ProcessEvents() override
	{
		std::deque<std::function<void()>> events;
		{
			std::scoped_lock lock(fLock);
			events.swap(fEvents);
			fProcessThreads.push_back(std::this_thread::get_id());
		}
		for (std::function<void()>& event : events)
			event();
	}

	void WaitForEvents() override
	{
		std::unique_lock lock(fLock);
		fWaits++;
		fBlocked = true;
		fChanged.wait(lock, [this] { return fWoken || !fEvents.empty(); });
		fBlocked = false;
		fWoken = false;
	}

	void Wake() override
	{
		{
			std::scoped_lock lock(fLock);
			fWoken = true;
			fWakes++;
		}
		fChanged.notify_all();
	}

	// Queues \a event for the main thread, as a window system would.
	void Inject(std::function<void()> event)
	{
		{
			std::scoped_lock lock(fLock);
			fEvents.push_back(std::move(event));
		}
		fChanged.notify_all();
	}

	// Whether the main thread is blocked in WaitForEvents() right now.
	bool IsBlocked()
	{
		std::scoped_lock lock(fLock);
		return fBlocked;
	}

	int Waits()
	{
		std::scoped_lock lock(fLock);
		return fWaits;
	}

	std::vector<std::thread::id> ProcessThreads()
	{
		std::scoped_lock lock(fLock);
		return fProcessThreads;
	}

private:
	std::mutex fLock;
	std::condition_variable fChanged;
	std::deque<std::function<void()>> fEvents;
	std::vector<std::thread::id> fProcessThreads;
	bool fWoken = false;
	bool fBlocked = false;
	int fWaits = 0;
	int fWakes = 0;
};


} // namespace ngui::test
