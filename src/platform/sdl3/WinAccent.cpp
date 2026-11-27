/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "platform/sdl3/WinAccent.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>


namespace ngui::platform::sdl3 {


namespace {


class WinAccent final : public SystemAccent {
public:
	WinAccent(HKEY key, std::wstring value, Changed changed)
		:
		fKey(key),
		fValue(std::move(value)),
		fChanged(std::move(changed)),
		fKeyChanged(CreateEventW(nullptr, FALSE, FALSE, nullptr)),
		fStop(CreateEventW(nullptr, TRUE, FALSE, nullptr))
	{
		fColor = _Read();
		// armed before the thread runs, so no change after _Read() is missed
		_Arm();
		fThread = std::thread([this] { _Run(); });
	}

	~WinAccent() override
	{
		SetEvent(fStop);
		fThread.join();
		RegCloseKey(fKey);
		CloseHandle(fKeyChanged);
		CloseHandle(fStop);
	}

	std::optional<gfx::Color> Color() const override
	{
		std::scoped_lock lock(fLock);
		return fColor;
	}

private:
	// 0xAABBGGRR, std::nullopt without the value
	std::optional<gfx::Color> _Read() const
	{
		DWORD abgr = 0;
		DWORD size = sizeof(abgr);
		if (RegGetValueW(fKey, nullptr, fValue.c_str(), RRF_RT_REG_DWORD, nullptr, &abgr, &size)
				!= ERROR_SUCCESS) {
			return std::nullopt;
		}
		const auto byte = [abgr](int shift) {
			return static_cast<std::uint8_t>((abgr >> shift) & 0xFF);
		};
		return gfx::Color::FromRgba8(byte(0), byte(8), byte(16), 255);
	}

	void _Arm()
	{
		// asynchronous: fKeyChanged is signaled once; armed anew after each
		(void)RegNotifyChangeKeyValue(fKey, FALSE, REG_NOTIFY_CHANGE_LAST_SET, fKeyChanged,
			TRUE);
	}

	void _Run()
	{
		const HANDLE events[] = {fStop, fKeyChanged};
		while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
			_Arm();
			const std::optional<gfx::Color> color = _Read();
			{
				std::scoped_lock lock(fLock);
				if (fColor == color)
					continue;
				fColor = color;
			}
			fChanged();
		}
	}

	HKEY					fKey;
	std::wstring			fValue;
	Changed					fChanged;
	HANDLE					fKeyChanged;
	HANDLE					fStop;
	mutable std::mutex		fLock;
	std::optional<gfx::Color> fColor;
	std::thread				fThread;
};


} // namespace


std::unique_ptr<SystemAccent>
CreateWinAccent(const std::wstring& key, const std::wstring& value,
	SystemAccent::Changed changed)
{
	HKEY opened = nullptr;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_QUERY_VALUE | KEY_NOTIFY, &opened)
			!= ERROR_SUCCESS) {
		return nullptr;
	}
	return std::make_unique<WinAccent>(opened, value, std::move(changed));
}


std::unique_ptr<SystemAccent>
CreateSystemAccent(SystemAccent::Changed changed)
{
	return CreateWinAccent(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
		std::move(changed));
}


} // namespace ngui::platform::sdl3
