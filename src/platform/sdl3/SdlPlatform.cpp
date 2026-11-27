/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/SdlPlatform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/LayoutUtils.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/platform/CpuPresenter.h>

#include "Environment.h"
#include "PlatformEvent.h"
#include "SdlEvents.h"
#include "platform/sdl3/DropPayload.h"
#include "platform/sdl3/FrameHit.h"
#ifdef NGUI_WITH_ACCESSKIT
#include "platform/sdl3/AccessKitBridge.h"
#endif
#ifdef NGUI_WITH_X11_DND
#include "platform/sdl3/X11DragDrop.h"
#endif
#ifdef NGUI_WITH_DBUS_NOTIFICATIONS
#include "platform/sdl3/DbusNotifications.h"
#endif
#ifdef _WIN32
#include "platform/sdl3/WinNotifications.h"
#endif
#ifdef __APPLE__
#include "platform/sdl3/CocoaDragDrop.h"
#include "platform/sdl3/CocoaNotifications.h"
#include "platform/sdl3/CocoaMenuBar.h"
#endif
#ifdef _WIN32
#include "platform/sdl3/OleDragDrop.h"
#endif


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

	// Phase G: a window whose lists the main thread renders on the GPU,
	// through the window's OpenGL context (SDL allows OpenGL only there).
	// gl and gpuSurface are used on the main thread only; the window
	// thread hands over the next frame (PresentList()).
	SDL_GLContext			gl = nullptr;
	gfx::SurfaceRef			gpuSurface;
	gfx::DisplayListRef		pendingFrame;
	gfx::Color				pendingClear = {1, 1, 1, 1};
	bool					renderQueued = false;
	// the frame shown last, for ReadShownFrame(); main thread only
	gfx::DisplayListRef		shownFrame;
	gfx::Color				shownClear = {1, 1, 1, 1};
#ifdef NGUI_WITH_ACCESSKIT
	// assistive technology (windows, not popups); made and freed on the
	// main thread
	std::unique_ptr<sdl3::AccessKitBridge> accessibility;
#endif
#ifdef _WIN32
	// NewGUI's OLE drop target in place of SDL's (module P4; windows, not
	// popups); main thread
	void*					dropTarget = nullptr;
#endif
#ifdef NGUI_WITH_X11_DND
	// an X11 window (not a popup) that takes drops and drags out with XDND
	// (module P4)
	bool					x11Drops = false;
#endif
	// the frame its BWindow draws (BWindowFrames::Look), for SDL's hit test,
	// under a lock of its own: SDL asks while it handles events, when the
	// main thread may hold the window's
	std::mutex				frameLock;
	WindowFrameLayout		frameLayout;
	bool					hitTest = false;
#ifdef __APPLE__
	// an NSWindow of SDL's Cocoa driver (not a popup): its menus go into
	// the application's menu bar (module P3), as its BWindow passed them on
	bool					cocoa = false;
	std::vector<SystemMenuItem> systemMenus;
#endif
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


// SDL's hit test of a window with NewGUI's frame; main thread.
SDL_HitTestResult SDLCALL
FrameHitTest(SDL_Window* /*window*/, const SDL_Point* area, void* data)
{
	auto* state = static_cast<WindowState*>(data);
	std::scoped_lock lock(state->frameLock);
	return sdl3::HitOf(state->frameLayout,
		{static_cast<float>(area->x), static_cast<float>(area->y)});
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
	// SDL may keep a surface of the size before (GNOME's Mutter under
	// Wayland, a window resized before it was first shown): one that does
	// not fit the window's pixels is made anew, or the frame would be cut
	int pixelWidth = 0;
	int pixelHeight = 0;
	if (SDL_GetWindowSizeInPixels(state.window, &pixelWidth, &pixelHeight)
		&& (surface->w != pixelWidth || surface->h != pixelHeight)) {
		(void)SDL_DestroyWindowSurface(state.window);
		surface = SDL_GetWindowSurface(state.window);
		if (surface == nullptr)
			return;
	}

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
	// a drop with all its data (DropPayload), pushed by the Cocoa drop target
	std::uint32_t			dropDataEvent = 0;
	std::atomic<bool>		wakePending = false;
	std::mutex				lock;
	std::deque<std::function<void()>> work;
	std::map<SDL_WindowID, std::weak_ptr<WindowState>> windows;
	// the cursors made so far (main thread only): system ones by Haiku's
	// BCursorID, the image cursor shown last
	std::map<std::uint8_t, SDL_Cursor*> systemCursors;
	SDL_Cursor*				imageCursor = nullptr;
	// Phase G: whether new windows render on the GPU (NGUI_RENDERER=gpu,
	// SetGpuRendering()); the backend that renders them and the fonts it
	// has, main thread only, made on first use
	std::atomic<bool>		gpuWanted = false;
	std::unique_ptr<gfx::Backend> gpuBackend;
	bool					gpuBackendTried = false;
	std::unordered_set<gfx::FontId> gpuFonts;
	bool					gpuWarned = false;
#ifdef __APPLE__
	// the window whose menus the menu bar shows (main thread only)
	std::weak_ptr<WindowState> menuOwner;
#endif
	// the system's notifications (module P5), made on first use, under
	// notifyLock: the desktop's over D-Bus on Linux, the icon in the
	// notification area on Windows, UNUserNotificationCenter on macOS
	std::mutex				notifyLock;
#ifdef NGUI_WITH_DBUS_NOTIFICATIONS
	std::unique_ptr<sdl3::DbusNotifications> notifications;
#elif defined(_WIN32)
	std::unique_ptr<sdl3::WinNotifications> notifications;
#elif defined(__APPLE__)
	std::unique_ptr<sdl3::CocoaNotifications> notifications;
#endif

	// The backend with GPU surfaces that renders GPU windows, or nullptr;
	// on the main thread.
	gfx::Backend* GpuBackend()
	{
		if (!gpuBackendTried) {
			gpuBackendTried = true;
			auto backend = gfx::BackendRegistry::Default().CreatePreferred({});
			if (backend.has_value() && (*backend)->Capabilities().gpuSurfaces)
				gpuBackend = std::move(*backend);
		}
		return gpuBackend.get();
	}

	// Hands the fonts of the text system to the GPU backend, those it does
	// not have yet; on the main thread.
	void AddGpuFonts()
	{
		for (const FontFile& font : TextSystem::Default().Fonts()) {
			if (gpuFonts.insert(font.id).second)
				(void)gpuBackend->AddFont(font.id, font.data, font.faceIndex);
		}
	}

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
// Phase G: draws \a frame on \a clear into the back buffer of a GPU
// window, its OpenGL context made current, the surface fitted to the
// window's size; on the main thread. False if it cannot.
bool
DrawGpuFrame(sdl3::PlatformState& platform, WindowState& state, const gfx::DisplayList& frame,
	const gfx::Color& clear)
{
	gfx::Backend* backend = platform.GpuBackend();
	if (backend == nullptr || state.window == nullptr || state.gl == nullptr
		|| state.gpuSurface == nullptr || !SDL_GL_MakeCurrent(state.window, state.gl)) {
		return false;
	}
	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(state.window, &width, &height);
	float scale = 1;
	{
		std::scoped_lock lock(state.lock);
		scale = state.scale;
	}
	const gfx::Size device = {static_cast<float>(width), static_cast<float>(height)};
	if (state.gpuSurface->DeviceSize() != device || state.gpuSurface->Scale() != scale)
		backend->ResizeSurface(*state.gpuSurface, device, scale);
	platform.AddGpuFonts();
	backend->BeginFrame(*state.gpuSurface, clear);
	backend->Render(frame, {0, 0});
	backend->EndFrame(*state.gpuSurface);
	return true;
}


// Phase G: renders the frame the window handed over last and shows it; on
// the main thread.
void
ShowGpuFrame(sdl3::PlatformState& platform, WindowState& state)
{
	gfx::DisplayListRef frame;
	gfx::Color clear;
	{
		std::scoped_lock lock(state.lock);
		state.renderQueued = false;
		frame = std::move(state.pendingFrame);
		clear = state.pendingClear;
	}
	if (frame == nullptr || !DrawGpuFrame(platform, state, *frame, clear))
		return;
	SDL_GL_SwapWindow(state.window);
	state.shownFrame = std::move(frame);
	state.shownClear = clear;
}


// Phase G: what a GPU window shows: its last frame drawn again into the
// back buffer and read with glReadPixels(), past the backend, the top row
// first; on the main thread.
std::expected<ShownFrame, Error>
ReadGpuFrame(sdl3::PlatformState& platform, WindowState& state)
{
	if (state.shownFrame == nullptr)
		return Failure(ErrorCode::NotFound, "no frame shown yet");
	if (!DrawGpuFrame(platform, state, *state.shownFrame, state.shownClear))
		return Failure(ErrorCode::NotSupported, "the window cannot draw on the GPU");

	using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
	using Finish = void (*)();
	using Bind = void (*)(unsigned, unsigned);
	using PixelStore = void (*)(unsigned, int);
	const auto read = reinterpret_cast<ReadPixels>(SDL_GL_GetProcAddress("glReadPixels"));
	const auto finish = reinterpret_cast<Finish>(SDL_GL_GetProcAddress("glFinish"));
	const auto bindFramebuffer = reinterpret_cast<Bind>(
		SDL_GL_GetProcAddress("glBindFramebuffer"));
	const auto bindBuffer = reinterpret_cast<Bind>(SDL_GL_GetProcAddress("glBindBuffer"));
	const auto pixelStore = reinterpret_cast<PixelStore>(SDL_GL_GetProcAddress("glPixelStorei"));
	if (read == nullptr || finish == nullptr || bindFramebuffer == nullptr
		|| bindBuffer == nullptr || pixelStore == nullptr) {
		return Failure(ErrorCode::NotSupported, "OpenGL lacks glReadPixels()");
	}
	// the state the backend may have left: the default framebuffer, no
	// pixel buffer, tight rows
	bindFramebuffer(0x8D40, 0);		// GL_FRAMEBUFFER
	bindBuffer(0x88EB, 0);			// GL_PIXEL_PACK_BUFFER
	pixelStore(0x0D02, 0);			// GL_PACK_ROW_LENGTH
	pixelStore(0x0D05, 4);			// GL_PACK_ALIGNMENT
	finish();

	const gfx::Size device = state.gpuSurface->DeviceSize();
	ShownFrame shown;
	shown.width = static_cast<std::int32_t>(device.w);
	shown.height = static_cast<std::int32_t>(device.h);
	const auto stride = static_cast<std::size_t>(shown.width) * 4;
	std::vector<std::byte> bottomUp(stride * static_cast<std::size_t>(shown.height));
	read(0, 0, shown.width, shown.height, 0x1908, 0x1401, bottomUp.data());	// RGBA, bytes
	shown.rgba.resize(bottomUp.size());
	for (std::int32_t y = 0; y < shown.height; y++) {
		std::copy_n(bottomUp.begin() + static_cast<std::ptrdiff_t>(stride)
			* (shown.height - 1 - y), stride, shown.rgba.begin()
			+ static_cast<std::ptrdiff_t>(stride) * y);
	}
	return shown;
}


// A window the CPU presenter draws, popup if \a parent is given (\a x and
// \a y then relative to it). Without a graphics context of SDL: under macOS
// SDL gives a window without one of OpenGL, Metal or Vulkan an OpenGL
// context, which fails where there is no OpenGL (the offscreen driver
// without EGL).
SDL_Window*
CreateCpuWindow(const std::string& title, int width, int height, SDL_WindowFlags flags,
	SDL_Window* parent = nullptr, int x = 0, int y = 0)
{
	const SDL_PropertiesID properties = SDL_CreateProperties();
	if (properties == 0)
		return nullptr;
	(void)SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());
	(void)SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
	(void)SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
	(void)SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
		static_cast<Sint64>(flags));
	(void)SDL_SetBooleanProperty(properties,
		SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
	if (parent != nullptr) {
		(void)SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, parent);
		(void)SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
		(void)SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
	}
	SDL_Window* window = SDL_CreateWindowWithProperties(properties);
	SDL_DestroyProperties(properties);
	return window;
}


// Phase G: a window with an OpenGL context and a GPU surface over it, or
// why there is none; on the main thread.
struct GpuWindow {
	SDL_Window*				window = nullptr;
	SDL_GLContext			gl = nullptr;
	gfx::SurfaceRef			surface;
};

std::expected<GpuWindow, std::string>
MakeGpuWindow(sdl3::PlatformState& platform, const WindowSpec& spec, SDL_WindowFlags flags)
{
	gfx::Backend* backend = platform.GpuBackend();
	if (backend == nullptr)
		return std::unexpected("no backend with GPU surfaces");
	// what the backend wraps: RGBA8 with a stencil, double buffered
	(void)SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	(void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	GpuWindow made;
	made.window = SDL_CreateWindow(spec.title.c_str(),
		static_cast<int>(std::lround(spec.frame.Width())),
		static_cast<int>(std::lround(spec.frame.Height())), flags | SDL_WINDOW_OPENGL);
	if (made.window == nullptr)
		return std::unexpected(std::string("SDL: ") + SDL_GetError());
	made.gl = SDL_GL_CreateContext(made.window);
	if (made.gl == nullptr) {
		std::string error = std::string("SDL: ") + SDL_GetError();
		SDL_DestroyWindow(made.window);
		return std::unexpected(std::move(error));
	}
	// no waiting for the display: the main thread also handles the events
	(void)SDL_GL_SetSwapInterval(0);

	int width = 0;
	int height = 0;
	SDL_GetWindowSizeInPixels(made.window, &width, &height);
	const float density = SDL_GetWindowPixelDensity(made.window);
	const float scale = density > 0 ? density : 1;
	gfx::NativeWindow native = NativeOf(made.window);
	native.size = {static_cast<float>(width) / scale, static_cast<float>(height) / scale};
	native.scale = scale;
	native.glProcAddress = SDL_GL_GetProcAddress;
	made.surface = backend->CreateWindowSurface(native, gfx::SurfaceKind::Gpu);
	if (made.surface == nullptr) {
		(void)SDL_GL_DestroyContext(made.gl);
		SDL_DestroyWindow(made.window);
		return std::unexpected("the backend cannot draw into the window's OpenGL context");
	}
	return made;
}


#ifdef __APPLE__
// Puts the menus of \a state's window into the application's menu bar: when
// it got the focus, or when it changed them and is the window in front (or
// none was yet); main thread.
void
ShowMenusOf(sdl3::PlatformState& platform, const std::shared_ptr<WindowState>& state,
	bool focused)
{
	const std::shared_ptr<WindowState> owner = platform.menuOwner.lock();
	if (!focused && owner != nullptr && owner != state)
		return;
	std::vector<SystemMenuItem> menus;
	BMessenger target;
	{
		std::scoped_lock lock(state->lock);
		if (state->window == nullptr || !state->cocoa)
			return;
		menus = state->systemMenus;
		target = state->target;
	}
	platform.menuOwner = state;
	sdl3::InstallMenuBar(menus, target);
}
#endif


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
#ifdef NGUI_WITH_ACCESSKIT
			// before the window it subclasses
			state->accessibility.reset();
#endif
#ifdef _WIN32
			if (state->dropTarget != nullptr) {
				sdl3::RevokeDropTarget(reinterpret_cast<void*>(state->native.window),
					state->dropTarget);
				state->dropTarget = nullptr;
			}
#endif
#ifdef NGUI_WITH_X11_DND
			if (state->x11Drops)
				sdl3::ForgetX11Drops(state->window);
#endif
#ifdef __APPLE__
			if (state->cocoa)
				sdl3::ForgetCocoaDrops(state->window);
			// its menus leave the menu bar with it
			if (platform->menuOwner.lock() == state) {
				platform->menuOwner.reset();
				sdl3::InstallMenuBar({}, BMessenger());
			}
#endif
			// the GPU surface with its context current, then the context
			if (state->gl != nullptr) {
				(void)SDL_GL_MakeCurrent(state->window, state->gl);
				state->gpuSurface.reset();
				state->pendingFrame.reset();
				state->shownFrame.reset();
				(void)SDL_GL_DestroyContext(state->gl);
				state->gl = nullptr;
			}
			SDL_DestroyWindow(state->window);
			state->window = nullptr;
		});
	}

	bool CanDragToSystem() const override
	{
#ifdef _WIN32
		std::scoped_lock lock(fState->lock);
		return fState->dropTarget != nullptr;
#elif defined(__APPLE__)
		std::scoped_lock lock(fState->lock);
		return fState->cocoa;
#elif defined(NGUI_WITH_X11_DND)
		std::scoped_lock lock(fState->lock);
		return fState->x11Drops;
#else
		return false;
#endif
	}

	std::expected<void, Error> StartSystemDrag(std::vector<DragItem> items) override
	{
#ifdef _WIN32
		if (!CanDragToSystem())
			return Failure(ErrorCode::NotSupported, "no drags to other applications");
		// OLE's modal drag on the main thread; the release it takes comes to
		// the window afterwards
		_Main([state = fState, items = std::move(items)] {
			sdl3::RunSystemDrag(items);
			float x = 0;
			float y = 0;
			(void)SDL_GetGlobalMouseState(&x, &y);
			BMessage up(kMouseUp);
			BMessenger target;
			{
				std::scoped_lock lock(state->lock);
				const float scale = state->scale > 0 ? state->scale : 1;
				(void)up.AddPoint("where", {x / scale - state->frame.x0,
					y / scale - state->frame.y0});
				target = state->target;
			}
			(void)up.AddInt32("buttons", 0);
			PlatformEventMark::Set(up);
			(void)target.SendMessage(up);
		});
		return {};
#elif defined(__APPLE__)
		if (!CanDragToSystem())
			return Failure(ErrorCode::NotSupported, "no drags to other applications");
		// an NSDraggingSession from the window's content view; it runs on
		// its own, and gives the window the release when it ends
		_Main([state = fState, items = std::move(items)] {
			SDL_Window* window = nullptr;
			BMessenger target;
			{
				std::scoped_lock lock(state->lock);
				window = state->window;
				target = state->target;
			}
			if (window != nullptr)
				(void)sdl3::StartCocoaDrag(window, items, target);
		});
		return {};
#elif defined(NGUI_WITH_X11_DND)
		if (!CanDragToSystem())
			return Failure(ErrorCode::NotSupported, "no drags to other applications");
		// XDND from the window, following the pointer while SDL keeps it
		// grabbed; the release comes to the window as usual
		_Main([state = fState, items = std::move(items)] {
			SDL_Window* window = nullptr;
			{
				std::scoped_lock lock(state->lock);
				window = state->window;
			}
			if (window != nullptr)
				(void)sdl3::StartX11Drag(window, items);
		});
		return {};
#else
		(void)items;
		return Failure(ErrorCode::NotSupported, "no drags to other applications");
#endif
	}

	bool HasSystemMenuBar() const override
	{
#ifdef __APPLE__
		std::scoped_lock lock(fState->lock);
		return fState->cocoa;
#else
		return false;
#endif
	}

	void SetFrameLayout(const WindowFrameLayout& layout) override
	{
		bool install = false;
		{
			std::scoped_lock lock(fState->frameLock);
			fState->frameLayout = layout;
			install = !fState->hitTest;
			fState->hitTest = true;
		}
		if (!install)
			return;
		// the window system moves and resizes by the frame's areas
		_Main([state = fState] {
			SDL_Window* window = nullptr;
			{
				std::scoped_lock lock(state->lock);
				window = state->window;
			}
			if (window != nullptr)
				(void)SDL_SetWindowHitTest(window, &FrameHitTest, state.get());
		});
	}

	void SetSystemMenuBar(std::vector<SystemMenuItem> menus) override
	{
#ifdef __APPLE__
		{
			std::scoped_lock lock(fState->lock);
			fState->systemMenus = std::move(menus);
		}
		_Main([state = fState, platform = fPlatform] { ShowMenusOf(*platform, state, false); });
#else
		(void)menus;
#endif
	}

	bool DrawsLists() const override
	{
		std::scoped_lock lock(fState->lock);
		return fState->gl != nullptr;
	}

	std::expected<void, Error> PresentList(gfx::DisplayListRef frame,
		const gfx::Color& clear) override
	{
		{
			std::scoped_lock lock(fState->lock);
			if (fState->window == nullptr)
				return Failure(ErrorCode::NotFound, "the window is closed");
			if (fState->gl == nullptr)
				return Failure(ErrorCode::NotSupported, "the window draws on the CPU");
			// only the newest frame is drawn
			fState->pendingFrame = std::move(frame);
			fState->pendingClear = clear;
			if (fState->renderQueued)
				return {};
			fState->renderQueued = true;
		}
		fPlatform->Queue([state = fState, platform = fPlatform] {
			ShowGpuFrame(*platform, *state);
		});
		return {};
	}

	std::expected<ShownFrame, Error> ReadShownFrame() override
	{
		return fPlatform->OnMain([state = fState, platform = fPlatform] {
			return ReadGpuFrame(*platform, *state);
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

	bool IsAccessibilityActive() const override
	{
#ifdef NGUI_WITH_ACCESSKIT
		return fState->accessibility != nullptr && fState->accessibility->IsActive();
#else
		return false;
#endif
	}

	void UpdateAccessibility(const AccessibleTree& tree) override
	{
#ifdef NGUI_WITH_ACCESSKIT
		if (fState->accessibility == nullptr)
			return;
		float scale = 1;
		{
			std::scoped_lock lock(fState->lock);
			scale = fState->scale;
		}
		fState->accessibility->SetTree(tree, scale);
		_Main([state = fState] {
			sdl3::AccessKitBridge* bridge = nullptr;
			{
				std::scoped_lock lock(state->lock);
				if (state->window != nullptr)
					bridge = state->accessibility.get();
			}
			// without the window's lock: the adapter may call back
			if (bridge != nullptr)
				bridge->Push();
		});
#else
		(void)tree;
#endif
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
			// With one side limited, SDL tells X11's window manager 0 for
			// the other, which it takes as 0 (GNOME's Mutter then keeps the
			// window at its minimum height): the largest side X11 has instead.
			int maxWidth = limit(max.w);
			int maxHeight = limit(max.h);
			constexpr int kLargestSide = 32767;
			if (maxWidth == 0 && maxHeight != 0)
				maxWidth = kLargestSide;
			else if (maxHeight == 0 && maxWidth != 0)
				maxHeight = kLargestSide;
			SDL_SetWindowMaximumSize(state->window, maxWidth, maxHeight);
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
		// The window system may give another size (GNOME's Mutter refuses
		// one larger than the screen without its top bar); the window
		// hears the size it got.
		_Main([state = fState, size] {
			SDL_Window* window = nullptr;
			{
				std::scoped_lock lock(state->lock);
				window = state->window;
			}
			if (window == nullptr)
				return;
			(void)SDL_SyncWindow(window);
			int width = 0;
			int height = 0;
			if (!SDL_GetWindowSize(window, &width, &height)
				|| (width == static_cast<int>(std::lround(size.w))
					&& height == static_cast<int>(std::lround(size.h)))) {
				return;
			}
			BMessenger target;
			float scale = 1;
			{
				std::scoped_lock lock(state->lock);
				UpdateGeometry(*state);
				target = state->target;
				scale = state->scale > 0 ? state->scale : 1;
			}
			BMessage resized(kWindowResized);
			(void)resized.AddFloat("width", static_cast<float>(width));
			(void)resized.AddFloat("height", static_cast<float>(height));
			(void)resized.AddFloat("be:scale", scale);
			PlatformEventMark::Set(resized);
			(void)target.SendMessage(resized);
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


bool
SdlPlatform::Register(PlatformRegistry& registry, int priority)
{
	return registry.Register("sdl3", priority,
		[]() -> std::expected<std::unique_ptr<Platform>, Error> {
			auto platform = Create();
			if (!platform.has_value())
				return std::unexpected(platform.error());
			return std::unique_ptr<Platform>(std::move(*platform));
		});
}


std::expected<std::unique_ptr<SdlPlatform>, Error>
SdlPlatform::Create()
{
	if (!SDL_Init(SDL_INIT_VIDEO))
		return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
	std::unique_ptr<SdlPlatform> platform(new SdlPlatform());
	platform->fData->mainThread = std::this_thread::get_id();
	// Phase G on request, until it is the default
	if (detail::EnvironmentVariable("NGUI_RENDERER").value_or("") == "gpu")
		platform->fData->gpuWanted = true;
	platform->fData->wakeEvent = SDL_RegisterEvents(1);
	platform->fData->dropDataEvent = SDL_RegisterEvents(1);
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
		SDL_GLContext gl = nullptr;
		gfx::SurfaceRef gpuSurface;
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
			window = CreateCpuWindow(spec.title,
				static_cast<int>(std::lround(spec.frame.Width())),
				static_cast<int>(std::lround(spec.frame.Height())),
				(spec.tooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU)
					| SDL_WINDOW_NOT_FOCUSABLE | SDL_WINDOW_HIDDEN
					| SDL_WINDOW_HIGH_PIXEL_DENSITY, parentHandle,
				static_cast<int>(std::lround(offset.x)), static_cast<int>(std::lround(offset.y)));
			if (window == nullptr)
				return Failure(ErrorCode::NotSupported, std::string("SDL: ") + SDL_GetError());
		} else {
			SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
			if (spec.resizable)
				flags |= SDL_WINDOW_RESIZABLE;
			if (!spec.decorated)
				flags |= SDL_WINDOW_BORDERLESS;
			// NewGUI's frame: the desktop shows beside the tab
			if (spec.transparent)
				flags |= SDL_WINDOW_TRANSPARENT;
			// Phase G on request; without OpenGL on the CPU, said once (a
			// transparent window keeps the CPU's surface for now)
			if (data->gpuWanted && !spec.transparent) {
				auto made = MakeGpuWindow(*data, spec, flags);
				if (made.has_value()) {
					window = made->window;
					gl = made->gl;
					gpuSurface = std::move(made->surface);
				} else if (!data->gpuWarned) {
					data->gpuWarned = true;
					std::fprintf(stderr, "ngui: no GPU rendering (%s), drawing on the CPU\n",
						made.error().c_str());
				}
			}
			if (window == nullptr) {
				window = CreateCpuWindow(spec.title,
					static_cast<int>(std::lround(spec.frame.Width())),
					static_cast<int>(std::lround(spec.frame.Height())), flags);
			}
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
		state->gl = gl;
		state->gpuSurface = std::move(gpuSurface);
#ifdef __APPLE__
		state->cocoa = !spec.popup && SDL_GetPointerProperty(SDL_GetWindowProperties(window),
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr) != nullptr;
		// drops with all their data (module P4)
		if (state->cocoa)
			sdl3::AcceptCocoaDrops(window, data->dropDataEvent);
#endif
#ifdef NGUI_WITH_X11_DND
		// drops with all their data (module P4)
		state->x11Drops = !spec.popup && state->native.kind == gfx::NativeWindow::Kind::X11
			&& sdl3::AcceptX11Drops(window, data->dropDataEvent);
#endif
#ifdef _WIN32
		// drops with all their data, drags out to other programs (module P4)
		if (!spec.popup && state->native.window != 0) {
			state->dropTarget = sdl3::RegisterDropTarget(
				reinterpret_cast<void*>(state->native.window), spec.target,
				[window] { return SDL_GetWindowPixelDensity(window); });
		}
#endif
		UpdateGeometry(*state);
#ifdef NGUI_WITH_ACCESSKIT
		// while the window is hidden, as the Windows adapter requires
		if (!spec.popup)
			state->accessibility = sdl3::AccessKitBridge::Create(window, spec.target, spec.title);
#endif
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
			// without the system's panels (GNOME's top bar, a task bar)
			SDL_Rect usable{};
			if (SDL_GetDisplayUsableBounds(ids[i], &usable) && usable.w > 0 && usable.h > 0) {
				display.usable = {static_cast<float>(usable.x), static_cast<float>(usable.y),
					static_cast<float>(usable.x + usable.w),
					static_cast<float>(usable.y + usable.h)};
			}
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
		context.dropData = fData->dropDataEvent;
		context.scale = [](SDL_WindowID id) {
			SDL_Window* window = SDL_GetWindowFromID(id);
			const float density = window != nullptr ? SDL_GetWindowPixelDensity(window) : 1;
			return density > 0 ? density : 1.0f;
		};
		std::vector<sdl3::TranslatedEvent> messages = sdl3::Translate(events, context);
		// the payloads of drops are translated now
		for (const SDL_Event& taken : events) {
			if (fData->dropDataEvent != 0 && taken.type == fData->dropDataEvent)
				delete static_cast<sdl3::DropPayload*>(taken.user.data1);
		}
		for (sdl3::TranslatedEvent& translated : messages) {
			// events of the platform, for Record & Replay
			PlatformEventMark::Set(translated.message);
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
#ifdef NGUI_WITH_ACCESSKIT
					if (state->accessibility != nullptr) {
						const gfx::Rect& frame = state->frame;
						const double scale = state->scale;
						state->accessibility->SetBounds(frame.x0 * scale, frame.y0 * scale,
							frame.x1 * scale, frame.y1 * scale);
					}
#endif
				}
#ifdef NGUI_WITH_ACCESSKIT
				if (translated.message.what == kWindowActivated
					&& state->accessibility != nullptr) {
					state->accessibility->SetFocused(
						translated.message.GetBool("active", false));
				}
#endif
				target = state->target;
			}
#ifdef __APPLE__
			// the window in front shows its menus in the menu bar
			if (translated.message.what == kWindowActivated
				&& translated.message.GetBool("active", false)) {
				ShowMenusOf(*fData, state, true);
			}
#endif
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
	// The portal's comes from the event loop, on the main thread.
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
SdlPlatform::SetGpuRendering(bool gpu)
{
	fData->gpuWanted = gpu;
}


bool
SdlPlatform::GpuRendering() const
{
	return fData->gpuWanted;
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


SystemColorScheme
SdlPlatform::ColorScheme() const
{
	return fData->OnMain([] {
		switch (SDL_GetSystemTheme()) {
			case SDL_SYSTEM_THEME_LIGHT:
				return SystemColorScheme::Light;
			case SDL_SYSTEM_THEME_DARK:
				return SystemColorScheme::Dark;
			default:
				return SystemColorScheme::Unknown;
		}
	});
}


bool
SdlPlatform::HasTransparentWindows() const
{
	const std::string_view driver = VideoDriver();
	if (driver == "wayland" || driver == "windows" || driver == "cocoa")
		return true;
#ifdef NGUI_WITH_X11_DND
	if (driver == "x11")
		return sdl3::X11HasCompositor();
#endif
	return false;
}


std::expected<void, Error>
SdlPlatform::ShowNotification(const NotificationSpec& spec)
{
#if defined(NGUI_WITH_DBUS_NOTIFICATIONS) || defined(_WIN32) || defined(__APPLE__)
	std::scoped_lock lock(fData->notifyLock);
	if (fData->notifications == nullptr) {
#ifdef _WIN32
		auto connected = sdl3::WinNotifications::Create();
#elif defined(__APPLE__)
		auto connected = sdl3::CocoaNotifications::Create();
#else
		auto connected = sdl3::DbusNotifications::Create();
#endif
		if (!connected.has_value())
			return std::unexpected(connected.error());
		fData->notifications = std::move(*connected);
	}
	fData->notifications->Show(spec);
	return {};
#else
	(void)spec;
	return Failure(ErrorCode::NotSupported, "no notifications");
#endif
}


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
