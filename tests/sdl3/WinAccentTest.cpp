/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The accent color on Windows (module G6), from the registry as the desktop
// window manager keeps it (AccentColor, 0xAABBGGRR). The test writes a key of
// its own below HKEY_CURRENT_USER, not the system's, and removes it.


#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <ngui/gfx/Color.h>

#include "platform/sdl3/WinAccent.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>


namespace {


using namespace ngui;
using namespace std::chrono_literals;


// A key of the test's own, removed with everything in it at the end.
class TestKey {
public:
	TestKey()
		:
		fPath(L"Software\\NewGUI-Tests\\Accent-" + std::to_wstring(GetCurrentProcessId()))
	{
		(void)RegCreateKeyExW(HKEY_CURRENT_USER, fPath.c_str(), 0, nullptr, 0, KEY_SET_VALUE,
			nullptr, &fKey, nullptr);
	}

	~TestKey()
	{
		if (fKey != nullptr)
			RegCloseKey(fKey);
		(void)RegDeleteTreeW(HKEY_CURRENT_USER, fPath.c_str());
	}

	TestKey(const TestKey&) = delete;
	TestKey& operator=(const TestKey&) = delete;

	const std::wstring& Path() const
	{
		return fPath;
	}

	void Set(DWORD abgr)
	{
		(void)RegSetValueExW(fKey, L"AccentColor", 0, REG_DWORD,
			reinterpret_cast<const BYTE*>(&abgr), sizeof(abgr));
	}

	void Remove()
	{
		(void)RegDeleteValueW(fKey, L"AccentColor");
	}

private:
	std::wstring fPath;
	HKEY fKey = nullptr;
};


// Waits until \a changes reaches \a count; false after 5 s.
bool
WaitForChanges(const std::atomic<int>& changes, int count)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (changes < count && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(5ms);
	return changes >= count;
}


TEST(WinAccentTest, TheRegistrysAccentColorIsHeard)
{
	// What is tested: A key with AccentColor 0xFFE48435 (blue, 53, 132, 228,
	// as 0xAABBGGRR); NewGUI reads it (CreateWinAccent()); the user picks
	// 0xFF7EC22E (green, 46, 194, 126), then the value is removed.
	// Success criteria: The color is the blue one at once; after each change
	// the callback ran once more and the color is the green one, then none.
	// Why it matters: SDL does not report the accent color; this is how
	// NewGUI learns it on Windows and hears the user change it, in the byte
	// order the desktop window manager writes.
	//
	// Given
	TestKey key;
	key.Set(0xFFE48435);
	std::atomic<int> changes = 0;

	// When
	const std::unique_ptr<platform::sdl3::SystemAccent> accent = platform::sdl3::CreateWinAccent(
		key.Path(), L"AccentColor", [&changes] { ++changes; });
	ASSERT_NE(accent, nullptr);
	const std::optional<gfx::Color> blue = accent->Color();
	key.Set(0xFF7EC22E);
	const bool heardGreen = WaitForChanges(changes, 1);
	const std::optional<gfx::Color> green = accent->Color();
	key.Remove();
	const bool heardNone = WaitForChanges(changes, 2);
	const std::optional<gfx::Color> none = accent->Color();

	// Then
	EXPECT_EQ(blue, gfx::Color::FromRgba8(53, 132, 228, 255));
	EXPECT_TRUE(heardGreen);
	EXPECT_EQ(green, gfx::Color::FromRgba8(46, 194, 126, 255));
	EXPECT_TRUE(heardNone);
	EXPECT_EQ(none, std::nullopt);
}


TEST(WinAccentTest, WithoutTheKeyThereIsNoAccent)
{
	// What is tested: A key that does not exist.
	// Success criteria: CreateWinAccent() returns nullptr.
	// Why it matters: Without the desktop window manager's key NewGUI keeps
	// Haiku's own colors instead of failing.
	//
	// When
	const std::unique_ptr<platform::sdl3::SystemAccent> accent = platform::sdl3::CreateWinAccent(
		L"Software\\NewGUI-Tests\\NoSuchKey", L"AccentColor", [] {});

	// Then
	EXPECT_EQ(accent, nullptr);
}


} // namespace
