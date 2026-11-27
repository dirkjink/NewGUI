/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/WinNotifications.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
// windows.h names these as macros (SendMessageW ...); BMessenger has them
#undef SendMessage
#undef DispatchMessage


namespace ngui::platform::sdl3 {


namespace {


constexpr UINT kShowMessage = WM_APP + 2;
constexpr UINT kIconId = 1;
constexpr const wchar_t* kClassName = L"NGUINotifications";


std::wstring
Wide(std::string_view text)
{
	if (text.empty())
		return {};
	const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(),
		static_cast<int>(text.size()), nullptr, 0);
	std::wstring wide(static_cast<std::size_t>(std::max(length, 0)), L'\0');
	(void)MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
		wide.data(), length);
	return wide;
}


// \a text within \a limit characters, without cutting a surrogate pair.
std::wstring
Limited(std::wstring text, std::size_t limit)
{
	if (text.size() <= limit)
		return text;
	text.resize(limit);
	if (!text.empty() && text.back() >= 0xD800 && text.back() <= 0xDBFF)
		text.pop_back();
	return text;
}


template<std::size_t Size>
void
CopyTo(wchar_t (&field)[Size], const std::wstring& text)
{
	const std::size_t count = std::min(text.size(), Size - 1);
	std::copy_n(text.data(), count, field);
	field[count] = L'\0';
}


// An icon of RGBA pixels, not premultiplied, as Windows takes them (BGRA).
HICON
IconOf(const NotificationSpec& spec)
{
	const auto pixels = static_cast<std::size_t>(spec.iconWidth)
		* static_cast<std::size_t>(spec.iconHeight);
	if (spec.iconWidth <= 0 || spec.iconHeight <= 0 || spec.icon.size() != pixels * 4)
		return nullptr;
	BITMAPV5HEADER header{};
	header.bV5Size = sizeof(header);
	header.bV5Width = spec.iconWidth;
	header.bV5Height = -spec.iconHeight;	// top-down
	header.bV5Planes = 1;
	header.bV5BitCount = 32;
	header.bV5Compression = BI_BITFIELDS;
	header.bV5RedMask = 0x00FF0000;
	header.bV5GreenMask = 0x0000FF00;
	header.bV5BlueMask = 0x000000FF;
	header.bV5AlphaMask = 0xFF000000;
	void* bits = nullptr;
	HDC screen = GetDC(nullptr);
	HBITMAP color = CreateDIBSection(screen, reinterpret_cast<const BITMAPINFO*>(&header),
		DIB_RGB_COLORS, &bits, nullptr, 0);
	ReleaseDC(nullptr, screen);
	if (color == nullptr)
		return nullptr;
	auto* out = static_cast<std::uint8_t*>(bits);
	for (std::size_t i = 0; i < pixels; i++) {
		out[i * 4 + 0] = spec.icon[i * 4 + 2];
		out[i * 4 + 1] = spec.icon[i * 4 + 1];
		out[i * 4 + 2] = spec.icon[i * 4 + 0];
		out[i * 4 + 3] = spec.icon[i * 4 + 3];
	}
	// the alpha of the colors decides; the mask only has to be there
	HBITMAP mask = CreateBitmap(spec.iconWidth, spec.iconHeight, 1, 1, nullptr);
	ICONINFO info{};
	info.fIcon = TRUE;
	info.hbmColor = color;
	info.hbmMask = mask;
	HICON icon = CreateIconIndirect(&info);
	DeleteObject(color);
	if (mask != nullptr)
		DeleteObject(mask);
	return icon;
}


} // namespace


Balloon
BalloonOf(const NotificationSpec& spec)
{
	Balloon balloon;
	balloon.tip = Limited(Wide(spec.group.empty() ? spec.application : spec.group), 127);
	balloon.title = Limited(Wide(spec.title), 63);
	std::string text = spec.content;
	if (spec.kind == NotificationSpec::Kind::Progress) {
		const long percent = std::lround(std::clamp(spec.progress, 0.0f, 1.0f) * 100);
		text += (text.empty() ? "" : "\n") + std::to_string(percent) + " %";
	}
	// the shell shows no balloon without text
	balloon.text = Limited(text.empty() ? std::wstring(L" ") : Wide(text), 255);
	switch (spec.kind) {
		case NotificationSpec::Kind::Important:
			balloon.flags = NIIF_WARNING;
			break;
		case NotificationSpec::Kind::Error:
			balloon.flags = NIIF_ERROR;
			break;
		case NotificationSpec::Kind::Information:
		case NotificationSpec::Kind::Progress:
			balloon.flags = NIIF_INFO;
			break;
	}
	if (!spec.icon.empty() && spec.iconWidth > 0 && spec.iconHeight > 0) {
		balloon.flags = NIIF_USER;
		if (spec.iconWidth >= 32 && spec.iconHeight >= 32)
			balloon.flags |= NIIF_LARGE_ICON;
	}
	return balloon;
}


struct WinNotifications::Impl {
	std::thread				thread;
	HWND					window = nullptr;
	UINT					taskbarCreated = 0;
	HICON					appIcon = nullptr;
	bool					ownAppIcon = false;
	std::mutex				lock;
	std::deque<NotificationSpec> queue;
	std::atomic<std::size_t> shown = 0;
	// on the thread only: the balloon shown and what a click on it sends
	HICON					balloonIcon = nullptr;
	std::optional<std::pair<BMessenger, BMessage>> click;

	NOTIFYICONDATAW Data() const
	{
		NOTIFYICONDATAW data{};
		data.cbSize = sizeof(data);
		data.hWnd = window;
		data.uID = kIconId;
		return data;
	}

	bool AddIcon()
	{
		NOTIFYICONDATAW data = Data();
		data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
		data.uCallbackMessage = kNotificationIconMessage;
		data.hIcon = appIcon;
		if (!Shell_NotifyIconW(NIM_ADD, &data))
			return false;
		data.uVersion = NOTIFYICON_VERSION_4;
		return Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
	}

	void ShowNext()
	{
		std::deque<NotificationSpec> showing;
		{
			std::scoped_lock guard(lock);
			showing.swap(queue);
		}
		for (const NotificationSpec& spec : showing) {
			const Balloon balloon = BalloonOf(spec);
			NOTIFYICONDATAW data = Data();
			data.uFlags = NIF_INFO | NIF_TIP | NIF_SHOWTIP;
			CopyTo(data.szTip, balloon.tip);
			CopyTo(data.szInfoTitle, balloon.title);
			CopyTo(data.szInfo, balloon.text);
			data.dwInfoFlags = balloon.flags;
			HICON icon = (balloon.flags & NIIF_USER) != 0 ? IconOf(spec) : nullptr;
			if ((balloon.flags & NIIF_USER) != 0 && icon == nullptr)
				data.dwInfoFlags = NIIF_INFO;
			data.hBalloonIcon = icon;
			// the balloon before is gone with its click
			click.reset();
			if (Shell_NotifyIconW(NIM_MODIFY, &data) && spec.onClick.has_value())
				click.emplace(spec.target, *spec.onClick);
			if (balloonIcon != nullptr)
				DestroyIcon(balloonIcon);
			balloonIcon = icon;
			shown++;
		}
	}

	LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == kShowMessage) {
			ShowNext();
			return 0;
		}
		if (message == kNotificationIconMessage) {
			switch (LOWORD(lParam)) {
				case NIN_BALLOONUSERCLICK:
					if (click.has_value())
						(void)click->first.SendMessage(click->second);
					click.reset();
					break;
				case NIN_BALLOONTIMEOUT:
				case NIN_BALLOONHIDE:
					click.reset();
					break;
				default:
					break;
			}
			return 0;
		}
		if (taskbarCreated != 0 && message == taskbarCreated) {
			// Explorer started again: the icon is to be added anew
			(void)AddIcon();
			return 0;
		}
		if (message == WM_CLOSE) {
			NOTIFYICONDATAW data = Data();
			(void)Shell_NotifyIconW(NIM_DELETE, &data);
			DestroyWindow(window);
			return 0;
		}
		if (message == WM_DESTROY) {
			PostQuitMessage(0);
			return 0;
		}
		return DefWindowProcW(window, message, wParam, lParam);
	}

	static LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
	{
		auto* impl = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
		if (impl == nullptr)
			return DefWindowProcW(window, message, wParam, lParam);
		return impl->Handle(message, wParam, lParam);
	}

	// The thread: the window, the icon, then the window's messages.
	void Run(std::promise<bool>& started)
	{
		HINSTANCE instance = GetModuleHandleW(nullptr);
		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = &Procedure;
		windowClass.hInstance = instance;
		windowClass.lpszClassName = kClassName;
		// a second instance finds the class registered
		(void)RegisterClassExW(&windowClass);
		// a hidden top-level window (a message-only one would not hear that
		// the taskbar was made anew)
		window = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 0, 0,
			nullptr, nullptr, instance, nullptr);
		if (window == nullptr) {
			started.set_value(false);
			return;
		}
		SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
		taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
		// the program's own icon, else Windows' one for applications
		wchar_t path[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, path, MAX_PATH) != 0
			&& ExtractIconExW(path, 0, nullptr, &appIcon, 1) == 1 && appIcon != nullptr) {
			ownAppIcon = true;
		} else {
			// IDI_APPLICATION, whose macro is narrow without UNICODE
			appIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
		}
		if (!AddIcon()) {
			DestroyWindow(window);
			window = nullptr;
			started.set_value(false);
			return;
		}
		started.set_value(true);
		MSG message;
		while (GetMessageW(&message, nullptr, 0, 0) > 0) {
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}
		if (balloonIcon != nullptr)
			DestroyIcon(balloonIcon);
		if (ownAppIcon)
			DestroyIcon(appIcon);
	}
};


WinNotifications::WinNotifications(std::unique_ptr<Impl> impl)
	:
	fImpl(std::move(impl))
{
}


std::expected<std::unique_ptr<WinNotifications>, Error>
WinNotifications::Create()
{
	auto impl = std::make_unique<Impl>();
	std::promise<bool> started;
	std::future<bool> result = started.get_future();
	Impl* running = impl.get();
	impl->thread = std::thread([running, &started] { running->Run(started); });
	if (!result.get()) {
		impl->thread.join();
		return Failure(ErrorCode::NotSupported, "the shell takes no notification icon");
	}
	return std::unique_ptr<WinNotifications>(new WinNotifications(std::move(impl)));
}


WinNotifications::~WinNotifications()
{
	(void)PostMessageW(fImpl->window, WM_CLOSE, 0, 0);
	fImpl->thread.join();
}


void
WinNotifications::Show(const NotificationSpec& spec)
{
	{
		std::scoped_lock guard(fImpl->lock);
		fImpl->queue.push_back(spec);
	}
	(void)PostMessageW(fImpl->window, kShowMessage, 0, 0);
}


void*
WinNotifications::Window() const
{
	return fImpl->window;
}


std::size_t
WinNotifications::CountShown() const
{
	return fImpl->shown;
}


} // namespace ngui::platform::sdl3
