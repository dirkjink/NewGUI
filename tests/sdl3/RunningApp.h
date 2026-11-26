/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


// The application of the SDL platform tests.


#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <utility>

#include <SDL3/SDL.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Clock.h>
#include <ngui/Messenger.h>
#include <ngui/platform/SdlPlatform.h>


namespace ngui::test {


using namespace std::chrono_literals;


// An application with the SDL platform, run on a thread of the test as on
// a main thread. SDL is initialized on that thread, as it must be; the
// test thread uses the platform as any other thread of a program would.
class RunningApp {
public:
	RunningApp()
	{
		std::promise<platform::SdlPlatform*> started;
		fThread = std::thread([this, &started] {
			auto platform = platform::SdlPlatform::Create();
			if (!platform.has_value()) {
				started.set_value(nullptr);
				return;
			}
			platform::SdlPlatform* raw = platform->get();
			BApplication app("application/x-vnd.ngui-sdl-test");
			app.SetPulseRate(Clock::Time::zero());
			(void)app.UsePlatform(std::move(*platform));
			started.set_value(raw);
			(void)app.Run();
			fEnded = true;
		});
		fPlatform = started.get_future().get();
		// Run() makes the platform the current one only after the thread
		// has signalled; the system clipboard, cursors and dialogs reach
		// it through platform::Current().
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (fPlatform != nullptr && platform::Current() != fPlatform
			&& std::chrono::steady_clock::now() < deadline) {
			std::this_thread::yield();
		}
	}

	~RunningApp()
	{
		if (fPlatform != nullptr && !fEnded) {
			const BMessenger app = WaitForApp();
			(void)app.SendMessage(kQuitRequested);
		}
		fThread.join();
	}

	platform::SdlPlatform* Platform() const
	{
		return fPlatform;
	}

	// Runs \a work on the main thread and waits for it.
	template<typename Work>
	auto OnMain(Work work) -> decltype(work())
	{
		std::packaged_task<decltype(work())()> task(std::move(work));
		auto result = task.get_future();
		fPlatform->RunOnMainThread([&task] { task(); });
		return result.get();
	}

	// Whether the application's Run() returned within \a timeout.
	bool WaitForEnd(std::chrono::milliseconds timeout)
	{
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (!fEnded && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		return fEnded;
	}

	// Puts SDL_EVENT_QUIT into SDL's queue, as SIGTERM does.
	void PushQuit()
	{
		(void)OnMain([] {
			SDL_Event quit{};
			quit.type = SDL_EVENT_QUIT;
			return SDL_PushEvent(&quit);
		});
	}

private:
	static BMessenger WaitForApp()
	{
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (!AppMessenger().IsValid() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		return AppMessenger();
	}

	std::thread fThread;
	platform::SdlPlatform* fPlatform = nullptr;
	std::atomic<bool> fEnded = false;
};



} // namespace ngui::test
