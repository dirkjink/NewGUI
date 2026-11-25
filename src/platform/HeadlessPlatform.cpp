/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/HeadlessPlatform.h>

#include <algorithm>
#include <utility>

#include <ngui/AppDefs.h>


namespace ngui::platform {


// What the platform and its windows share; the windows may outlive the
// platform's object only as long as nobody uses them.
struct HeadlessWindow::Shared {
	mutable std::mutex					lock;
	std::condition_variable				changed;
	std::deque<std::function<void()>>	work;
	bool								woken = false;
	std::vector<HeadlessWindow*>		windows;
	HeadlessWindow*						active = nullptr;
	ClipboardData						clipboard;
	CursorShape							cursor;
	bool								cursorVisible = true;
	HeadlessPlatform::FileDialogAnswer	answer;
	std::vector<FileDialogSpec>			dialogs;
};


// #pragma mark - HeadlessWindow


HeadlessWindow::HeadlessWindow(std::shared_ptr<Shared> shared, const WindowSpec& spec,
	float scale)
	:
	fShared(std::move(shared)),
	fTitle(spec.title),
	fFrame(spec.frame),
	fScale(scale),
	fTarget(spec.target),
	fParent(spec.parent),
	fPopup(spec.popup)
{
}


HeadlessWindow::~HeadlessWindow()
{
	std::scoped_lock lock(fShared->lock);
	std::erase(fShared->windows, this);
	if (fShared->active == this)
		fShared->active = nullptr;
}


void
HeadlessWindow::SetTitle(std::string_view title)
{
	std::scoped_lock lock(fLock);
	fTitle = title;
}


void
HeadlessWindow::Show()
{
	std::scoped_lock lock(fLock);
	fShown = true;
}


void
HeadlessWindow::Hide()
{
	std::scoped_lock lock(fLock);
	fShown = false;
}


void
HeadlessWindow::MoveTo(gfx::Point where)
{
	std::scoped_lock lock(fLock);
	fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
}


void
HeadlessWindow::ResizeTo(gfx::Size size)
{
	std::scoped_lock lock(fLock);
	fFrame = {fFrame.x0, fFrame.y0, fFrame.x0 + size.w, fFrame.y0 + size.h};
}


void
HeadlessWindow::SetSizeLimits(gfx::Size min, gfx::Size max)
{
	std::scoped_lock lock(fLock);
	fMinSize = min;
	fMaxSize = max;
}


gfx::Rect
HeadlessWindow::Frame() const
{
	std::scoped_lock lock(fLock);
	return fFrame;
}


float
HeadlessWindow::Scale() const
{
	return fScale;
}


gfx::NativeWindow
HeadlessWindow::Native() const
{
	std::scoped_lock lock(fLock);
	gfx::NativeWindow native;
	native.size = {fFrame.Width(), fFrame.Height()};
	native.scale = fScale;
	return native;
}


void
HeadlessWindow::SetTarget(BMessenger target)
{
	std::scoped_lock lock(fLock);
	fTarget = std::move(target);
}


void
HeadlessWindow::Activate()
{
	HeadlessWindow* previous = nullptr;
	{
		std::scoped_lock lock(fShared->lock);
		previous = fShared->active;
		if (previous == this)
			return;
		fShared->active = this;
	}
	// as a window system tells both windows
	BMessage inactive(kWindowActivated);
	(void)inactive.AddBool("active", false);
	if (previous != nullptr)
		(void)previous->Send(inactive);
	BMessage active(kWindowActivated);
	(void)active.AddBool("active", true);
	(void)Send(active);
}


std::expected<void, Error>
HeadlessWindow::Present(const gfx::CpuPixels& pixels, std::span<const gfx::IntRect>)
{
	auto frame = std::make_shared<PresentedFrame>();
	frame->width = pixels.width;
	frame->height = pixels.height;
	frame->stride = pixels.stride;
	frame->format = pixels.format;
	frame->bytes.assign(pixels.bytes.begin(), pixels.bytes.end());
	std::scoped_lock lock(fLock);
	fLastFrame = std::move(frame);
	fFrames++;
	return {};
}


std::string
HeadlessWindow::Title() const
{
	std::scoped_lock lock(fLock);
	return fTitle;
}


bool
HeadlessWindow::IsShown() const
{
	std::scoped_lock lock(fLock);
	return fShown;
}


bool
HeadlessWindow::IsPopup() const
{
	return fPopup;
}


PlatformWindow*
HeadlessWindow::Parent() const
{
	return fParent;
}


std::pair<gfx::Size, gfx::Size>
HeadlessWindow::SizeLimits() const
{
	std::scoped_lock lock(fLock);
	return {fMinSize, fMaxSize};
}


BMessenger
HeadlessWindow::Target() const
{
	std::scoped_lock lock(fLock);
	return fTarget;
}


std::shared_ptr<const HeadlessWindow::PresentedFrame>
HeadlessWindow::LastFrame() const
{
	std::scoped_lock lock(fLock);
	return fLastFrame;
}


std::size_t
HeadlessWindow::CountFrames() const
{
	std::scoped_lock lock(fLock);
	return fFrames;
}


std::expected<void, Error>
HeadlessWindow::Send(const BMessage& message) const
{
	return Target().SendMessage(message);
}


std::expected<void, Error>
HeadlessWindow::RequestClose() const
{
	return Send(BMessage(kQuitRequested));
}


// #pragma mark - HeadlessPlatform


HeadlessPlatform::HeadlessPlatform()
	:
	HeadlessPlatform(Options{})
{
}


HeadlessPlatform::HeadlessPlatform(const Options& options)
	:
	fOptions(options),
	fShared(std::make_shared<HeadlessWindow::Shared>())
{
}


HeadlessPlatform::~HeadlessPlatform() = default;


std::string_view
HeadlessPlatform::Name() const
{
	return "headless";
}


std::expected<std::unique_ptr<PlatformWindow>, Error>
HeadlessPlatform::CreateWindow(const WindowSpec& spec)
{
	if (spec.popup && spec.parent == nullptr)
		return Failure(ErrorCode::InvalidArgument, "a popup needs a parent window");
	std::unique_ptr<HeadlessWindow> window(new HeadlessWindow(fShared, spec, fOptions.scale));
	std::scoped_lock lock(fShared->lock);
	fShared->windows.push_back(window.get());
	return window;
}


std::vector<DisplayInfo>
HeadlessPlatform::Displays() const
{
	return {{{0, 0, fOptions.screen.w, fOptions.screen.h}, fOptions.scale, true}};
}


void
HeadlessPlatform::ProcessEvents()
{
	std::deque<std::function<void()>> work;
	{
		std::scoped_lock lock(fShared->lock);
		work.swap(fShared->work);
	}
	for (std::function<void()>& item : work)
		item();
}


void
HeadlessPlatform::WaitForEvents()
{
	std::unique_lock lock(fShared->lock);
	fShared->changed.wait(lock, [&] { return fShared->woken || !fShared->work.empty(); });
	fShared->woken = false;
}


void
HeadlessPlatform::Wake()
{
	{
		std::scoped_lock lock(fShared->lock);
		fShared->woken = true;
	}
	fShared->changed.notify_all();
}


std::expected<void, Error>
HeadlessPlatform::SetClipboard(const ClipboardData& data)
{
	std::scoped_lock lock(fShared->lock);
	fShared->clipboard = data;
	return {};
}


std::expected<ClipboardData, Error>
HeadlessPlatform::Clipboard() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->clipboard;
}


void
HeadlessPlatform::SetCursor(const CursorShape& cursor)
{
	std::scoped_lock lock(fShared->lock);
	fShared->cursor = cursor;
}


void
HeadlessPlatform::SetCursorVisible(bool visible)
{
	std::scoped_lock lock(fShared->lock);
	fShared->cursorVisible = visible;
}


std::expected<void, Error>
HeadlessPlatform::ShowFileDialog(const FileDialogSpec& spec,
	std::function<void(FileDialogResult)> done)
{
	if (!done)
		return Failure(ErrorCode::InvalidArgument, "no callback");
	FileDialogAnswer answer;
	{
		std::scoped_lock lock(fShared->lock);
		fShared->dialogs.push_back(spec);
		answer = fShared->answer;
	}
	// later, as a real dialog answers
	Post([spec, answer = std::move(answer), done = std::move(done)] {
		done(answer ? answer(spec) : FileDialogResult(std::vector<std::string>()));
	});
	return {};
}


void
HeadlessPlatform::Post(std::function<void()> work)
{
	{
		std::scoped_lock lock(fShared->lock);
		fShared->work.push_back(std::move(work));
	}
	fShared->changed.notify_all();
}


std::vector<HeadlessWindow*>
HeadlessPlatform::Windows() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->windows;
}


HeadlessWindow*
HeadlessPlatform::ActiveWindow() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->active;
}


CursorShape
HeadlessPlatform::Cursor() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->cursor;
}


bool
HeadlessPlatform::CursorVisible() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->cursorVisible;
}


void
HeadlessPlatform::SetFileDialogAnswer(FileDialogAnswer answer)
{
	std::scoped_lock lock(fShared->lock);
	fShared->answer = std::move(answer);
}


std::vector<FileDialogSpec>
HeadlessPlatform::FileDialogs() const
{
	std::scoped_lock lock(fShared->lock);
	return fShared->dialogs;
}


} // namespace ngui::platform
