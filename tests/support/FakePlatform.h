/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <condition_variable>
#include <deque>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <ngui/platform/Platform.h>


namespace ngui::test {


// A window of FakePlatform: it records what is done to it and what is
// presented, and lets the test send it events.
class FakeWindow : public platform::PlatformWindow {
public:
	FakeWindow(const platform::WindowSpec& spec, float scale)
		:
		fFrame(spec.frame),
		fScale(scale),
		fTarget(spec.target),
		fParent(spec.parent),
		fPopup(spec.popup),
		fToolTip(spec.tooltip)
	{
	}

	// The window a popup belongs to (WindowSpec::parent).
	platform::PlatformWindow* Parent() const
	{
		return fParent;
	}

	// Whether it was made as a popup.
	bool IsPopup() const
	{
		return fPopup;
	}

	// Whether it was made as a tooltip popup.
	bool IsToolTip() const
	{
		return fToolTip;
	}

	void SetTitle(std::string_view) override
	{
	}

	void Show() override
	{
		std::scoped_lock lock(fLock);
		fShown = true;
	}

	void Hide() override
	{
		std::scoped_lock lock(fLock);
		fShown = false;
	}

	void MoveTo(gfx::Point where) override
	{
		std::scoped_lock lock(fLock);
		fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
	}

	void ResizeTo(gfx::Size size) override
	{
		std::scoped_lock lock(fLock);
		fFrame = {fFrame.x0, fFrame.y0, fFrame.x0 + size.w, fFrame.y0 + size.h};
	}

	void SetSizeLimits(gfx::Size min, gfx::Size max) override
	{
		std::scoped_lock lock(fLock);
		fMinSize = min;
		fMaxSize = max;
	}

	// The limits SetSizeLimits() got last.
	std::pair<gfx::Size, gfx::Size> SizeLimits() const
	{
		std::scoped_lock lock(fLock);
		return {fMinSize, fMaxSize};
	}

	gfx::Rect Frame() const override
	{
		std::scoped_lock lock(fLock);
		return fFrame;
	}

	float Scale() const override
	{
		return fScale;
	}

	gfx::NativeWindow Native() const override
	{
		std::scoped_lock lock(fLock);
		gfx::NativeWindow native;
		native.size = {fFrame.Width(), fFrame.Height()};
		native.scale = fScale;
		return native;
	}

	void SetTarget(BMessenger target) override
	{
		std::scoped_lock lock(fLock);
		fTarget = std::move(target);
	}

	std::expected<void, Error> Present(const gfx::CpuPixels& pixels,
		std::span<const gfx::IntRect> dirty) override
	{
		{
			std::scoped_lock lock(fLock);
			fPresented.emplace_back(dirty.begin(), dirty.end());
			fPresentedSize = {static_cast<float>(pixels.width), static_cast<float>(pixels.height)};
		}
		fChanged.notify_all();
		return {};
	}

	// Sends \a message to the window's target, as the window system would.
	void Send(const BMessage& message)
	{
		BMessenger target;
		{
			std::scoped_lock lock(fLock);
			target = fTarget;
		}
		(void)target.SendMessage(message);
	}

	// Waits until \a count frames have been presented; false after 5 s.
	bool WaitForPresents(std::size_t count)
	{
		std::unique_lock lock(fLock);
		return fChanged.wait_for(lock, std::chrono::seconds(5),
			[&] { return fPresented.size() >= count; });
	}

	std::vector<std::vector<gfx::IntRect>> Presented() const
	{
		std::scoped_lock lock(fLock);
		return fPresented;
	}

	gfx::Size PresentedSize() const
	{
		std::scoped_lock lock(fLock);
		return fPresentedSize;
	}

	bool IsShown() const
	{
		std::scoped_lock lock(fLock);
		return fShown;
	}

private:
	mutable std::mutex fLock;
	std::condition_variable fChanged;
	gfx::Rect fFrame;
	gfx::Size fMinSize = {0, 0};
	gfx::Size fMaxSize = {0, 0};
	float fScale;
	BMessenger fTarget;
	platform::PlatformWindow* fParent = nullptr;
	bool fPopup = false;
	bool fToolTip = false;
	bool fShown = false;
	std::vector<std::vector<gfx::IntRect>> fPresented;
	gfx::Size fPresentedSize{0, 0};
};


// A platform without a window system, for tests of the loop integration:
// "events" are functions injected from any thread and run by
// ProcessEvents() on the main thread. Its windows are FakeWindows.
class FakePlatform : public platform::Platform {
public:
	std::string_view Name() const override
	{
		return "fake";
	}

	std::expected<std::unique_ptr<platform::PlatformWindow>, Error> CreateWindow(
		const platform::WindowSpec& spec) override
	{
		auto window = std::make_unique<FakeWindow>(spec, windowScale);
		std::scoped_lock lock(fLock);
		fWindows.push_back(window.get());
		return window;
	}

	// The windows made so far, in order; they belong to whoever made them.
	std::vector<FakeWindow*> Windows()
	{
		std::scoped_lock lock(fLock);
		return fWindows;
	}

	float windowScale = 1;
	// The screens it reports; set before the application runs.
	std::vector<platform::DisplayInfo> displays = {{{0, 0, 800, 600}, 1, true}};

	std::vector<platform::DisplayInfo> Displays() const override
	{
		return displays;
	}

	void SetCursor(const platform::CursorShape& cursor) override
	{
		std::scoped_lock lock(fLock);
		fCursors.push_back(cursor);
	}

	void SetCursorVisible(bool visible) override
	{
		std::scoped_lock lock(fLock);
		fCursorVisible.push_back(visible);
	}

	std::expected<void, Error> ShowFileDialog(const platform::FileDialogSpec& spec,
		std::function<void(platform::FileDialogResult)> done) override
	{
		std::scoped_lock lock(fLock);
		fDialogs.push_back(spec);
		fDialogAnswers.push_back(std::move(done));
		return {};
	}

	// The file dialogs shown so far, in order.
	std::vector<platform::FileDialogSpec> Dialogs()
	{
		std::scoped_lock lock(fLock);
		return fDialogs;
	}

	// Answers file dialog \a index with \a result, as the system would
	// (from the calling thread); false if there is none or it was answered.
	bool AnswerDialog(std::size_t index, platform::FileDialogResult result)
	{
		std::function<void(platform::FileDialogResult)> done;
		{
			std::scoped_lock lock(fLock);
			if (index >= fDialogAnswers.size() || !fDialogAnswers[index])
				return false;
			done = std::move(fDialogAnswers[index]);
			fDialogAnswers[index] = nullptr;
		}
		done(std::move(result));
		return true;
	}

	// The cursors shown so far, in order.
	std::vector<platform::CursorShape> Cursors()
	{
		std::scoped_lock lock(fLock);
		return fCursors;
	}

	// The visibility changes so far, in order.
	std::vector<bool> CursorVisibility()
	{
		std::scoped_lock lock(fLock);
		return fCursorVisible;
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
	std::vector<FakeWindow*> fWindows;
	std::vector<platform::CursorShape> fCursors;
	std::vector<bool> fCursorVisible;
	std::vector<platform::FileDialogSpec> fDialogs;
	std::vector<std::function<void(platform::FileDialogResult)>> fDialogAnswers;
	bool fWoken = false;
	bool fBlocked = false;
	int fWaits = 0;
	int fWakes = 0;
};


} // namespace ngui::test
