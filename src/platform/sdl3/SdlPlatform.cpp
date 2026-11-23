/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/SdlPlatform.h>

#include <atomic>
#include <cmath>
#include <deque>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <SDL3/SDL.h>

#include <ngui/AppDefs.h>
#include <ngui/platform/CpuPresenter.h>

#include "SdlEvents.h"


namespace ngui::platform {


namespace {


// The state of one window, shared by the window object, the work queued
// for the main thread and the event routing, so that none of them outlives
// what it uses. SDL objects are touched on the main thread only.
struct WindowState {
	std::mutex				lock;
	SDL_Window*				window = nullptr;
	SDL_WindowID			id = 0;
	BMessenger				target;
	gfx::Rect				frame = {0, 0, 0, 0};
	float					scale = 1;
	gfx::NativeWindow		native;

	// The pixels of the last frame, as Present() copied them, and the
	// device-pixel areas not shown yet.
	std::vector<std::byte>	pixels;
	std::int32_t			width = 0;
	std::int32_t			height = 0;
	std::size_t				stride = 0;
	gfx::PixelFormat		format = gfx::PixelFormat::Bgra8Premul;
	std::vector<gfx::IntRect> dirty;
	bool					showQueued = false;
};


gfx::NativeWindow
NativeOf(SDL_Window* window)
{
	gfx::NativeWindow native;
	const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
	const std::string_view driver = SDL_GetCurrentVideoDriver() != nullptr
		? SDL_GetCurrentVideoDriver() : "";
	if (driver == "x11") {
		native.kind = gfx::NativeWindow::Kind::X11;
		native.display = SDL_GetPointerProperty(properties,
			SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
		native.window = static_cast<std::uintptr_t>(SDL_GetNumberProperty(properties,
			SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
	} else if (driver == "wayland") {
		native.kind = gfx::NativeWindow::Kind::Wayland;
		native.display = SDL_GetPointerProperty(properties,
			SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
		native.window = reinterpret_cast<std::uintptr_t>(SDL_GetPointerProperty(properties,
			SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
	} else if (driver == "windows") {
		native.kind = gfx::NativeWindow::Kind::Win32;
		native.window = reinterpret_cast<std::uintptr_t>(SDL_GetPointerProperty(properties,
			SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
	}
	return native;
}


// Reads position, size and pixel density of \a state's window into it;
// main thread, with the state's lock held.
void
UpdateGeometry(WindowState& state)
{
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	SDL_GetWindowPosition(state.window, &x, &y);
	SDL_GetWindowSize(state.window, &width, &height);
	const float density = SDL_GetWindowPixelDensity(state.window);
	state.scale = density > 0 ? density : 1;
	state.frame = {static_cast<float>(x), static_cast<float>(y),
		static_cast<float>(x + width), static_cast<float>(y + height)};
	state.native.size = {static_cast<float>(width), static_cast<float>(height)};
	state.native.scale = state.scale;
}


gfx::PixelFormat
FormatOf(SDL_PixelFormat format, bool& known)
{
	known = true;
	switch (format) {
		case SDL_PIXELFORMAT_BGRA32:
		case SDL_PIXELFORMAT_BGRX32:
			return gfx::PixelFormat::Bgra8Premul;
		case SDL_PIXELFORMAT_RGBA32:
		case SDL_PIXELFORMAT_RGBX32:
			return gfx::PixelFormat::Rgba8Premul;
		default:
			known = false;
			return gfx::PixelFormat::Bgra8Premul;
	}
}


// Copies the dirty areas of the last frame into the window; main thread.
void
ShowFrame(WindowState& state)
{
	std::scoped_lock lock(state.lock);
	state.showQueued = false;
	if (state.window == nullptr || state.dirty.empty())
		return;
	SDL_Surface* surface = SDL_GetWindowSurface(state.window);
	if (surface == nullptr)
		return;

	const gfx::CpuPixels source{state.width, state.height, state.stride, state.format,
		state.pixels};
	bool known = false;
	const gfx::PixelFormat format = FormatOf(surface->format, known);
	if (known && SDL_LockSurface(surface)) {
		const PixelTarget target{surface->w, surface->h, static_cast<std::size_t>(surface->pitch),
			format, std::span(static_cast<std::byte*>(surface->pixels),
				static_cast<std::size_t>(surface->pitch) * static_cast<std::size_t>(surface->h))};
		CopyPixels(source, target, state.dirty);
		SDL_UnlockSurface(surface);
	} else {
		// Another format: SDL converts.
		SDL_Surface* frame = SDL_CreateSurfaceFrom(state.width, state.height,
			state.format == gfx::PixelFormat::Bgra8Premul ? SDL_PIXELFORMAT_BGRA32
				: SDL_PIXELFORMAT_RGBA32,
			state.pixels.data(), static_cast<int>(state.stride));
		if (frame != nullptr) {
			SDL_SetSurfaceBlendMode(frame, SDL_BLENDMODE_NONE);
			for (const gfx::IntRect& area : state.dirty) {
				const SDL_Rect rect{area.x0, area.y0, area.Width(), area.Height()};
				SDL_BlitSurface(frame, &rect, surface, &rect);
			}
			SDL_DestroySurface(frame);
		}
	}

	std::vector<SDL_Rect> rects;
	for (const gfx::IntRect& area : state.dirty)
		rects.push_back({area.x0, area.y0, area.Width(), area.Height()});
	SDL_UpdateWindowSurfaceRects(state.window, rects.data(), static_cast<int>(rects.size()));
	state.dirty.clear();
}


} // namespace


// What the platform and its windows share; the windows keep it alive.
class sdl3::PlatformState {
public:
	std::thread::id			mainThread;
	std::uint32_t			wakeEvent = 0;
	std::atomic<bool>		wakePending = false;
	std::mutex				lock;
	std::deque<std::function<void()>> work;
	std::map<SDL_WindowID, std::weak_ptr<WindowState>> windows;

	bool OnMainThread() const
	{
		return std::this_thread::get_id() == mainThread;
	}

	void Wake()
	{
		// One pending wake event is enough.
		if (wakePending.exchange(true))
			return;
		SDL_Event event{};
		event.type = wakeEvent;
		SDL_PushEvent(&event);
	}

	void Queue(std::function<void()> task)
	{
		{
			std::scoped_lock guard(lock);
			work.push_back(std::move(task));
		}
		Wake();
	}

	void RunWork()
	{
		std::deque<std::function<void()>> tasks;
		{
			std::scoped_lock guard(lock);
			tasks.swap(work);
		}
		for (std::function<void()>& task : tasks)
			task();
	}

	// Runs \a task on the main thread and returns its result; from the
	// main thread directly.
	template<typename Task>
	auto OnMain(Task task) -> decltype(task())
	{
		if (OnMainThread())
			return task();
		std::packaged_task<decltype(task())()> packaged(std::move(task));
		auto result = packaged.get_future();
		auto shared = std::make_shared<decltype(packaged)>(std::move(packaged));
		Queue([shared] { (*shared)(); });
		return result.get();
	}
};


namespace {


// A window of SdlPlatform.
class SdlWindow final : public PlatformWindow {
public:
	SdlWindow(std::shared_ptr<sdl3::PlatformState> platform, std::shared_ptr<WindowState> state)
		:
		fPlatform(std::move(platform)),
		fState(std::move(state))
	{
	}

	~SdlWindow() override
	{
		const std::shared_ptr<WindowState> state = fState;
		const std::shared_ptr<sdl3::PlatformState> platform = fPlatform;
		_Main([state, platform] {
			std::scoped_lock lock(state->lock);
			if (state->window == nullptr)
				return;
			{
				std::scoped_lock guard(platform->lock);
				platform->windows.erase(state->id);
			}
			SDL_DestroyWindow(state->window);
			state->window = nullptr;
		});
	}

	void SetTitle(std::string_view title) override
	{
		_Main([state = fState, text = std::string(title)] {
			std::scoped_lock lock(state->lock);
			if (state->window != nullptr)
				SDL_SetWindowTitle(state->window, text.c_str());
		});
	}

	void Show() override
	{
		_Main([state = fState] {
			std::scoped_lock lock(state->lock);
			if (state->window != nullptr)
				SDL_ShowWindow(state->window);
		});
	}

	void Hide() override
	{
		_Main([state = fState] {
			std::scoped_lock lock(state->lock);
			if (state->window != nullptr)
				SDL_HideWindow(state->window);
		});
	}

	void MoveTo(gfx::Point where) override
	{
		_Main([state = fState, where] {
			std::scoped_lock lock(state->lock);
			if (state->window == nullptr)
				return;
			SDL_SetWindowPosition(state->window, static_cast<int>(std::lround(where.x)),
				static_cast<int>(std::lround(where.y)));
			UpdateGeometry(*state);
		});
	}

	void ResizeTo(gfx::Size size) override
	{
		_Main([state = fState, size] {
			std::scoped_lock lock(state->lock);
			if (state->window == nullptr)
				return;
			SDL_SetWindowSize(state->window, static_cast<int>(std::lround(size.w)),
				static_cast<int>(std::lround(size.h)));
			UpdateGeometry(*state);
		});
	}

	gfx::Rect Frame() const override
	{
		std::scoped_lock lock(fState->lock);
		return fState->frame;
	}

	float Scale() const override
	{
		std::scoped_lock lock(fState->lock);
		return fState->scale;
	}

	gfx::NativeWindow Native() const override
	{
		std::scoped_lock lock(fState->lock);
		return fState->native;
	}

	void SetTarget(BMessenger target) override
	{
		std::scoped_lock lock(fState->lock);
		fState->target = std::move(target);
	}

	std::expected<void, Error> Present(const gfx::CpuPixels& pixels,
		std::span<const gfx::IntRect> dirty) override
	{
		{
			std::scoped_lock lock(fState->lock);
			if (fState->window == nullptr)
				return Failure(ErrorCode::NotFound, "the window is closed");
			if (pixels.width != fState->width || pixels.height != fState->height
				|| pixels.format != fState->format) {
				fState->width = pixels.width;
				fState->height = pixels.height;
				fState->format = pixels.format;
				fState->stride = static_cast<std::size_t>(pixels.width) * 4;
				fState->pixels.assign(fState->stride * static_cast<std::size_t>(pixels.height),
					std::byte{0});
				fState->dirty.clear();
			}
			const PixelTarget staging{fState->width, fState->height, fState->stride,
				fState->format, fState->pixels};
			CopyPixels(pixels, staging, dirty);
			fState->dirty.insert(fState->dirty.end(), dirty.begin(), dirty.end());
			if (fState->showQueued)
				return {};
			fState->showQueued = true;
		}
		fPlatform->Queue([state = fState] { ShowFrame(*state); });
		return {};
	}

private:
	template<typename Task>
	void _Main(Task task)
	{
		if (fPlatform->OnMainThread())
			task();
		else
			fPlatform->Queue(std::move(task));
	}

	std::shared_ptr<sdl3::PlatformState> fPlatform;
	std::shared_ptr<WindowState> fState;
};


} // namespace


SdlPlatform::SdlPlatform()
	:
	fData(std::make_shared<sdl3::PlatformState>())
{
}


std::expected<std::unique_ptr<SdlPlatform>, Error>
SdlPlatform::Create()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
		return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
	std::unique_ptr<SdlPlatform> platform(new SdlPlatform());
	platform->fData->mainThread = std::this_thread::get_id();
	platform->fData->wakeEvent = SDL_RegisterEvents(1);
	if (platform->fData->wakeEvent == 0) {
		SDL_Quit();
		return Failure(ErrorCode::NotSupported, "SDL has no user events left");
	}
	return platform;
}


SdlPlatform::~SdlPlatform()
{
	fData->RunWork();
	SDL_Quit();
}


std::string_view
SdlPlatform::Name() const
{
	return "sdl3";
}


std::string_view
SdlPlatform::VideoDriver() const
{
	const char* driver = SDL_GetCurrentVideoDriver();
	return driver != nullptr ? driver : "";
}


std::expected<std::unique_ptr<PlatformWindow>, Error>
SdlPlatform::CreateWindow(const WindowSpec& spec)
{
	const std::shared_ptr<sdl3::PlatformState> data = fData;
	return fData->OnMain([data, spec]()
		-> std::expected<std::unique_ptr<PlatformWindow>, Error> {
		SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
		if (spec.resizable)
			flags |= SDL_WINDOW_RESIZABLE;
		if (!spec.decorated)
			flags |= SDL_WINDOW_BORDERLESS;
		SDL_Window* window = SDL_CreateWindow(spec.title.c_str(),
			static_cast<int>(std::lround(spec.frame.Width())),
			static_cast<int>(std::lround(spec.frame.Height())), flags);
		if (window == nullptr)
			return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
		SDL_SetWindowPosition(window, static_cast<int>(std::lround(spec.frame.x0)),
			static_cast<int>(std::lround(spec.frame.y0)));
		SDL_StartTextInput(window);

		auto state = std::make_shared<WindowState>();
		state->window = window;
		state->id = SDL_GetWindowID(window);
		state->target = spec.target;
		state->native = NativeOf(window);
		UpdateGeometry(*state);
		{
			std::scoped_lock guard(data->lock);
			data->windows[state->id] = state;
		}
		return std::make_unique<SdlWindow>(data, std::move(state));
	});
}


std::vector<DisplayInfo>
SdlPlatform::Displays() const
{
	return fData->OnMain([] {
		std::vector<DisplayInfo> displays;
		int count = 0;
		SDL_DisplayID* ids = SDL_GetDisplays(&count);
		const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
		for (int i = 0; i < count && ids != nullptr; i++) {
			SDL_Rect bounds{};
			if (!SDL_GetDisplayBounds(ids[i], &bounds))
				continue;
			const float scale = SDL_GetDisplayContentScale(ids[i]);
			DisplayInfo display{{static_cast<float>(bounds.x), static_cast<float>(bounds.y),
				static_cast<float>(bounds.x + bounds.w), static_cast<float>(bounds.y + bounds.h)},
				scale > 0 ? scale : 1, ids[i] == primary};
			if (display.primary)
				displays.insert(displays.begin(), display);
			else
				displays.push_back(display);
		}
		SDL_free(ids);
		return displays;
	});
}


void
SdlPlatform::ProcessEvents()
{
	fData->RunWork();
	fData->wakePending = false;

	std::vector<SDL_Event> events;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type != fData->wakeEvent)
			events.push_back(event);
	}

	if (!events.empty()) {
		sdl3::TranslationContext context;
		context.when = Clock::Default().Now();
		context.modifiers = sdl3::Modifiers(SDL_GetModState());
		context.buttons = sdl3::Buttons(SDL_GetMouseState(nullptr, nullptr));
		context.scale = [](SDL_WindowID id) {
			SDL_Window* window = SDL_GetWindowFromID(id);
			const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 1;
			return density > 0 ? density : 1.0f;
		};
		for (sdl3::TranslatedEvent& translated : sdl3::Translate(events, context)) {
			std::shared_ptr<WindowState> state;
			{
				std::scoped_lock guard(fData->lock);
				const auto found = fData->windows.find(translated.window);
				if (found != fData->windows.end())
					state = found->second.lock();
			}
			if (state == nullptr)
				continue;
			BMessenger target;
			{
				std::scoped_lock lock(state->lock);
				if (translated.message.what == kWindowResized
					|| translated.message.what == kWindowMoved) {
					UpdateGeometry(*state);
				}
				target = state->target;
			}
			(void)target.SendMessage(translated.message);
		}
	}

	fData->RunWork();
}


void
SdlPlatform::WaitForEvents()
{
	SDL_WaitEvent(nullptr);
}


void
SdlPlatform::Wake()
{
	fData->Wake();
}


void
SdlPlatform::RunOnMainThread(std::function<void()> work)
{
	if (fData->OnMainThread())
		work();
	else
		fData->Queue(std::move(work));
}


} // namespace ngui::platform
