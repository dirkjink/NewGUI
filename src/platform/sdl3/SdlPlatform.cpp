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
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/LayoutUtils.h>
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
	// The window a popup belongs to; SDL places popups relative to it.
	std::weak_ptr<WindowState> parent;
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
	// The size of SDL's window surface the last frame went into: SDL makes
	// a new, empty one when the window's size changes.
	int						surfaceWidth = 0;
	int						surfaceHeight = 0;
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
	// A popup's position is relative to its parent's content area.
	if (const std::shared_ptr<WindowState> parent = state.parent.lock()) {
		std::scoped_lock lock(parent->lock);
		x += static_cast<int>(std::lround(parent->frame.x0));
		y += static_cast<int>(std::lround(parent->frame.y0));
	}
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

	// A new surface (the window's size changed, under Wayland after the
	// frame was drawn) holds nothing yet: all of the frame goes into it.
	if (surface->w != state.surfaceWidth || surface->h != state.surfaceHeight) {
		state.surfaceWidth = surface->w;
		state.surfaceHeight = surface->h;
		state.dirty.assign(1, gfx::IntRect{0, 0, state.width, state.height});
	}

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
	// the cursors made so far (main thread only): system ones by Haiku's
	// BCursorID, the image cursor shown last
	std::map<std::uint8_t, SDL_Cursor*> systemCursors;
	SDL_Cursor*				imageCursor = nullptr;

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

	// Shows \a shape; on the main thread.
	void ApplyCursor(const CursorShape& shape)
	{
		if (shape.image != nullptr) {
			const CursorImage& image = *shape.image;
			if (image.width <= 0 || image.height <= 0
				|| image.rgba.size() != static_cast<std::size_t>(image.width)
					* static_cast<std::size_t>(image.height) * 4) {
				return;
			}
			std::vector<std::uint8_t> pixels = image.rgba;
			SDL_Surface* surface = SDL_CreateSurfaceFrom(image.width, image.height,
				SDL_PIXELFORMAT_RGBA32, pixels.data(), image.width * 4);
			if (surface == nullptr)
				return;
			SDL_Cursor* cursor = SDL_CreateColorCursor(surface,
				static_cast<int>(image.hotspot.x), static_cast<int>(image.hotspot.y));
			SDL_DestroySurface(surface);
			if (cursor == nullptr)
				return;
			SDL_SetCursor(cursor);
			if (imageCursor != nullptr)
				SDL_DestroyCursor(imageCursor);
			imageCursor = cursor;
			return;
		}
		if (SDL_Cursor* cursor = SystemCursor(shape.system))
			SDL_SetCursor(cursor);
	}

	// The SDL cursor of a BCursorID value, made on first use; nullptr if SDL
	// has none.
	SDL_Cursor* SystemCursor(std::uint8_t id)
	{
		const auto found = systemCursors.find(id);
		if (found != systemCursors.end())
			return found->second;
		SDL_Cursor* cursor = nullptr;
		if (id == 12) {
			// B_CURSOR_ID_NO_CURSOR: a transparent pixel (SDL has no such
			// system cursor; hiding it would mix with HideCursor())
			const std::uint8_t clear[4] = {0, 0, 0, 0};
			SDL_Surface* surface = SDL_CreateSurfaceFrom(1, 1, SDL_PIXELFORMAT_RGBA32,
				const_cast<std::uint8_t*>(clear), 4);
			if (surface != nullptr) {
				cursor = SDL_CreateColorCursor(surface, 0, 0);
				SDL_DestroySurface(surface);
			}
		} else
			cursor = SDL_CreateSystemCursor(SystemCursorFor(id));
		if (cursor != nullptr)
			systemCursors[id] = cursor;
		return cursor;
	}

	// The nearest SDL cursor of a BCursorID value.
	static SDL_SystemCursor SystemCursorFor(std::uint8_t id)
	{
		switch (id) {
			case 2:		// IBeam
			case 10:	// IBeamHorizontal
				return SDL_SYSTEM_CURSOR_TEXT;
			case 5:		// CrossHair
				return SDL_SYSTEM_CURSOR_CROSSHAIR;
			case 6:		// FollowLink
				return SDL_SYSTEM_CURSOR_POINTER;
			case 7:		// Grab
			case 8:		// Grabbing
			case 11:	// Move
				return SDL_SYSTEM_CURSOR_MOVE;
			case 13:	// NotAllowed
				return SDL_SYSTEM_CURSOR_NOT_ALLOWED;
			case 14:	// Progress
				return SDL_SYSTEM_CURSOR_PROGRESS;
			case 15:	// ResizeNorth
				return SDL_SYSTEM_CURSOR_N_RESIZE;
			case 16:	// ResizeEast
				return SDL_SYSTEM_CURSOR_E_RESIZE;
			case 17:	// ResizeSouth
				return SDL_SYSTEM_CURSOR_S_RESIZE;
			case 18:	// ResizeWest
				return SDL_SYSTEM_CURSOR_W_RESIZE;
			case 19:	// ResizeNorthEast
				return SDL_SYSTEM_CURSOR_NE_RESIZE;
			case 20:	// ResizeNorthWest
				return SDL_SYSTEM_CURSOR_NW_RESIZE;
			case 21:	// ResizeSouthEast
				return SDL_SYSTEM_CURSOR_SE_RESIZE;
			case 22:	// ResizeSouthWest
				return SDL_SYSTEM_CURSOR_SW_RESIZE;
			case 23:	// ResizeNorthSouth
				return SDL_SYSTEM_CURSOR_NS_RESIZE;
			case 24:	// ResizeEastWest
				return SDL_SYSTEM_CURSOR_EW_RESIZE;
			case 25:	// ResizeNorthEastSouthWest
				return SDL_SYSTEM_CURSOR_NESW_RESIZE;
			case 26:	// ResizeNorthWestSouthEast
				return SDL_SYSTEM_CURSOR_NWSE_RESIZE;
			default:
				// SystemDefault; SDL has no context menu, copy, link, help
				// or zoom cursor
				return SDL_SYSTEM_CURSOR_DEFAULT;
		}
	}

	// Frees the cursors; on the main thread, before SDL_Quit().
	void DestroyCursors()
	{
		for (const auto& [id, cursor] : systemCursors)
			SDL_DestroyCursor(cursor);
		systemCursors.clear();
		if (imageCursor != nullptr)
			SDL_DestroyCursor(imageCursor);
		imageCursor = nullptr;
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

	void SetTextInputArea(gfx::Rect rect, int32 cursor) override
	{
		_Main([state = fState, rect, cursor] {
			std::scoped_lock lock(state->lock);
			if (state->window == nullptr)
				return;
			const SDL_Rect area{static_cast<int>(std::lround(rect.x0)),
				static_cast<int>(std::lround(rect.y0)),
				static_cast<int>(std::lround(rect.Width())),
				static_cast<int>(std::lround(rect.Height()))};
			(void)SDL_SetTextInputArea(state->window, &area, cursor);
		});
	}

	void Activate() override
	{
		_Main([state = fState] {
			std::scoped_lock lock(state->lock);
			if (state->window != nullptr)
				(void)SDL_RaiseWindow(state->window);
		});
	}

	void ClearComposition() override
	{
		_Main([state = fState] {
			std::scoped_lock lock(state->lock);
			if (state->window != nullptr)
				(void)SDL_ClearComposition(state->window);
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
			// A popup is placed relative to its parent's content area.
			gfx::Point position = where;
			if (const std::shared_ptr<WindowState> parent = state->parent.lock()) {
				std::scoped_lock parentLock(parent->lock);
				position = {where.x - parent->frame.x0, where.y - parent->frame.y0};
			}
			SDL_SetWindowPosition(state->window, static_cast<int>(std::lround(position.x)),
				static_cast<int>(std::lround(position.y)));
			UpdateGeometry(*state);
		});
	}

	void SetSizeLimits(gfx::Size min, gfx::Size max) override
	{
		_Main([state = fState, min, max] {
			std::scoped_lock lock(state->lock);
			if (state->window == nullptr)
				return;
			// SDL: 0 is no limit; sizes beyond int are no limit either.
			const auto limit = [](float size) {
				return size >= kSizeUnlimited || !(size > 0) ? 0
					: static_cast<int>(std::lround(size));
			};
			SDL_SetWindowMinimumSize(state->window, limit(min.w), limit(min.h));
			SDL_SetWindowMaximumSize(state->window, limit(max.w), limit(max.h));
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

	// What the platform keeps of the window.
	const std::shared_ptr<WindowState>& State() const
	{
		return fState;
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
	fData->DestroyCursors();
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
		std::shared_ptr<WindowState> parent;
		if (spec.popup) {
			const auto* parentWindow = dynamic_cast<const SdlWindow*>(spec.parent);
			if (parentWindow == nullptr)
				return Failure(ErrorCode::InvalidArgument, "a popup needs an SDL parent window");
			parent = parentWindow->State();
		}

		SDL_Window* window = nullptr;
		if (parent != nullptr) {
			// SDL places a popup relative to its parent's content area; it
			// gets no keyboard focus.
			gfx::Point offset;
			SDL_Window* parentHandle = nullptr;
			{
				std::scoped_lock lock(parent->lock);
				parentHandle = parent->window;
				offset = {spec.frame.x0 - parent->frame.x0, spec.frame.y0 - parent->frame.y0};
			}
			if (parentHandle == nullptr)
				return Failure(ErrorCode::InvalidArgument, "the parent window is closed");
			// Not focusable: SDL would give a popup menu the keyboard focus
			// (under Wayland), and the window it belongs to would lose it,
			// which ends the menus' tracking. The keys go to that window.
			// A tooltip takes no input at all (SDL gives it no input region
			// under Wayland).
			window = SDL_CreatePopupWindow(parentHandle,
				static_cast<int>(std::lround(offset.x)), static_cast<int>(std::lround(offset.y)),
				static_cast<int>(std::lround(spec.frame.Width())),
				static_cast<int>(std::lround(spec.frame.Height())),
				(spec.tooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU)
					| SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN
					| SDL_WINDOW_HIGH_PIXEL_DENSITY);
			if (window == nullptr)
				return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
		} else {
			SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
			if (spec.resizable)
				flags |= SDL_WINDOW_RESIZABLE;
			if (!spec.decorated)
				flags |= SDL_WINDOW_BORDERLESS;
			window = SDL_CreateWindow(spec.title.c_str(),
				static_cast<int>(std::lround(spec.frame.Width())),
				static_cast<int>(std::lround(spec.frame.Height())), flags);
			if (window == nullptr)
				return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
			SDL_SetWindowPosition(window, static_cast<int>(std::lround(spec.frame.x0)),
				static_cast<int>(std::lround(spec.frame.y0)));
			SDL_StartTextInput(window);
		}

		auto state = std::make_shared<WindowState>();
		state->parent = parent;
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
	// The order matters (a lost wake-up hung the main thread): first take
	// all events, the wake events among them; then reset the flag, so that
	// any Wake() from now on pushes a wake event again; only then look at
	// the work here and, in the looper, at its messages. Work or a message
	// that comes after that check has its wake event in the queue, and
	// WaitForEvents() returns at once. (Resetting the flag before taking
	// the events left it set after its wake event was taken, and later
	// Wake()s pushed nothing.)
	std::vector<SDL_Event> events;
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		if (event.type != fData->wakeEvent)
			events.push_back(event);
	}
	fData->wakePending = false;
	fData->RunWork();

	// The window system shows a window anew or at another size: the last
	// frame goes into it again, whole (SDL's new window surface is empty;
	// under Wayland the size arrives after the frame was drawn).
	for (const SDL_Event& shown : events) {
		if (shown.type != SDL_EVENT_WINDOW_EXPOSED
			&& shown.type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
			&& shown.type != SDL_EVENT_WINDOW_RESIZED) {
			continue;
		}
		std::shared_ptr<WindowState> state;
		{
			std::scoped_lock guard(fData->lock);
			const auto found = fData->windows.find(shown.window.windowID);
			if (found != fData->windows.end())
				state = found->second.lock();
		}
		if (state == nullptr)
			continue;
		{
			std::scoped_lock lock(state->lock);
			if (state->pixels.empty())
				continue;
			state->dirty.assign(1, gfx::IntRect{0, 0, state->width, state->height});
		}
		ShowFrame(*state);
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
			if (translated.window == sdl3::kApplicationEvent) {
				(void)AppMessenger().SendMessage(translated.message);
				continue;
			}
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
SdlPlatform::SetCursor(const CursorShape& cursor)
{
	// The cursor is SDL's, on the main thread; the caller does not wait.
	const std::shared_ptr<sdl3::PlatformState> data = fData;
	RunOnMainThread([data, cursor] { data->ApplyCursor(cursor); });
}


void
SdlPlatform::SetCursorVisible(bool visible)
{
	RunOnMainThread([visible] {
		if (visible)
			SDL_ShowCursor();
		else
			SDL_HideCursor();
	});
}


namespace {


// What a file dialog needs until SDL calls back: the filters (SDL reads
// them until then) and the callback.
struct FileDialogRequest {
	std::vector<std::string>			names;
	std::vector<std::string>			patterns;
	std::vector<SDL_DialogFileFilter>	filters;
	std::function<void(FileDialogResult)> done;

	static void Callback(void* userdata, const char* const* files, int /*filter*/)
	{
		const std::unique_ptr<FileDialogRequest> request(
			static_cast<FileDialogRequest*>(userdata));
		if (files == nullptr) {
			request->done(Failure(ErrorCode::IoError, SDL_GetError()));
			return;
		}
		std::vector<std::string> paths;
		for (const char* const* file = files; *file != nullptr; file++)
			paths.emplace_back(*file);
		request->done(std::move(paths));
	}
};


} // namespace


std::expected<void, Error>
SdlPlatform::ShowFileDialog(const FileDialogSpec& spec,
	std::function<void(FileDialogResult)> done)
{
	if (!done)
		return Failure(ErrorCode::InvalidArgument, "no callback");
	auto request = std::make_unique<FileDialogRequest>();
	for (const auto& [name, pattern] : spec.filters) {
		request->names.push_back(name);
		request->patterns.push_back(pattern);
	}
	for (std::size_t i = 0; i < request->names.size(); i++)
		request->filters.push_back({request->names[i].c_str(), request->patterns[i].c_str()});
	request->done = std::move(done);

	std::shared_ptr<WindowState> parent;
	if (const auto* window = dynamic_cast<const SdlWindow*>(spec.parent))
		parent = window->State();

	// SDL shows dialogs from the main thread; the caller does not wait. The
	// callback may come on another thread (zenity's under Linux).
	FileDialogRequest* raw = request.release();
	RunOnMainThread([spec, parent, raw] {
		SDL_FileDialogType type = SDL_FILEDIALOG_OPENFILE;
		if (spec.kind == FileDialogSpec::Kind::Save)
			type = SDL_FILEDIALOG_SAVEFILE;
		else if (spec.kind == FileDialogSpec::Kind::Folder)
			type = SDL_FILEDIALOG_OPENFOLDER;
		const SDL_PropertiesID properties = SDL_CreateProperties();
		if (!raw->filters.empty()) {
			(void)SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_FILTERS_POINTER,
				raw->filters.data());
			(void)SDL_SetNumberProperty(properties, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER,
				static_cast<Sint64>(raw->filters.size()));
		}
		if (parent != nullptr) {
			SDL_Window* handle = nullptr;
			{
				std::scoped_lock lock(parent->lock);
				handle = parent->window;
			}
			if (handle != nullptr) {
				(void)SDL_SetPointerProperty(properties, SDL_PROP_FILE_DIALOG_WINDOW_POINTER,
					handle);
			}
		}
		const auto setString = [&](const char* name, const std::string& value) {
			if (!value.empty())
				(void)SDL_SetStringProperty(properties, name, value.c_str());
		};
		setString(SDL_PROP_FILE_DIALOG_LOCATION_STRING, spec.location);
		setString(SDL_PROP_FILE_DIALOG_TITLE_STRING, spec.title);
		setString(SDL_PROP_FILE_DIALOG_ACCEPT_STRING, spec.acceptLabel);
		setString(SDL_PROP_FILE_DIALOG_CANCEL_STRING, spec.cancelLabel);
		(void)SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, spec.many);
		SDL_ShowFileDialogWithProperties(type, &FileDialogRequest::Callback, raw, properties);
		SDL_DestroyProperties(properties);
	});
	return {};
}


void
SdlPlatform::RunOnMainThread(std::function<void()> work)
{
	if (fData->OnMainThread())
		work();
	else
		fData->Queue(std::move(work));
}


namespace {


// SDL's names of plain text, besides Haiku's "text/plain".
bool
IsTextMimeType(std::string_view mime)
{
	return mime.starts_with("text/plain") || mime == "UTF8_STRING" || mime == "TEXT"
		|| mime == "STRING";
}


// What SDL_SetClipboardData() serves: the data, and the MIME types in the
// form SDL wants them.
struct ClipboardOffer {
	ClipboardData			data;
	std::vector<std::string> mimeTypes;

	static const void* Serve(void* userdata, const char* mimeType, std::size_t* size)
	{
		const auto* offer = static_cast<const ClipboardOffer*>(userdata);
		std::string_view wanted = mimeType != nullptr ? mimeType : "";
		if (IsTextMimeType(wanted))
			wanted = "text/plain";
		for (const auto& [mime, bytes] : offer->data) {
			if (mime == wanted) {
				*size = bytes.size();
				return bytes.data();
			}
		}
		*size = 0;
		return nullptr;
	}

	static void Cleanup(void* userdata)
	{
		delete static_cast<ClipboardOffer*>(userdata);
	}
};


} // namespace


std::expected<void, Error>
SdlPlatform::SetClipboard(const ClipboardData& data)
{
	return fData->OnMain([&data]() -> std::expected<void, Error> {
		bool done = false;
		if (data.empty())
			done = SDL_ClearClipboardData();
		else if (data.size() == 1 && data.front().first == "text/plain") {
			const std::vector<std::byte>& bytes = data.front().second;
			const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			done = SDL_SetClipboardText(text.c_str());
		} else {
			auto offer = std::make_unique<ClipboardOffer>();
			offer->data = data;
			for (const auto& entry : data) {
				offer->mimeTypes.push_back(entry.first);
				if (entry.first == "text/plain")
					offer->mimeTypes.emplace_back("text/plain;charset=utf-8");
			}
			std::vector<const char*> mimeTypes;
			for (const std::string& mime : offer->mimeTypes)
				mimeTypes.push_back(mime.c_str());
			ClipboardOffer* raw = offer.release();
			done = SDL_SetClipboardData(&ClipboardOffer::Serve, &ClipboardOffer::Cleanup, raw,
				mimeTypes.data(), mimeTypes.size());
			if (!done)
				ClipboardOffer::Cleanup(raw);
		}
		if (!done)
			return Failure(ErrorCode::IoError, SDL_GetError());
		return {};
	});
}


std::expected<ClipboardData, Error>
SdlPlatform::Clipboard() const
{
	return fData->OnMain([]() -> std::expected<ClipboardData, Error> {
		ClipboardData result;
		if (SDL_HasClipboardText()) {
			char* text = SDL_GetClipboardText();
			if (text != nullptr) {
				const std::string_view view(text);
				const auto* bytes = reinterpret_cast<const std::byte*>(view.data());
				result.emplace_back("text/plain",
					std::vector<std::byte>(bytes, bytes + view.size()));
				SDL_free(text);
			}
		}

		std::size_t count = 0;
		char** mimeTypes = SDL_GetClipboardMimeTypes(&count);
		for (std::size_t i = 0; mimeTypes != nullptr && i < count; i++) {
			const std::string_view mime(mimeTypes[i]);
			if (IsTextMimeType(mime))
				continue;
			std::size_t size = 0;
			void* data = SDL_GetClipboardData(mimeTypes[i], &size);
			if (data == nullptr)
				continue;
			const auto* bytes = static_cast<const std::byte*>(data);
			result.emplace_back(std::string(mime), std::vector<std::byte>(bytes, bytes + size));
			SDL_free(data);
		}
		SDL_free(mimeTypes);
		return result;
	});
}


} // namespace ngui::platform
