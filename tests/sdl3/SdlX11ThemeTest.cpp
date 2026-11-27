/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// The system's light or dark colors on Linux (module G6): SDL reads them
// from the desktop's portal (org.freedesktop.portal.Settings, namespace
// org.freedesktop.appearance, key color-scheme) and hears their change
// (SettingChanged). The test is the portal, on a session bus of its own;
// SDL's X11 driver needs an X server (Xvfb in the CI). Without either, or
// when a real portal owns the name, the test is skipped.


#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

#include <dbus/dbus.h>
#include <gtest/gtest.h>

#include <ngui/Application.h>
#include <ngui/Palette.h>

#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


constexpr const char* kPortalInterface = "org.freedesktop.portal.Settings";


// The desktop's portal, as far as SDL asks it: Read() of the color scheme
// (1 dark, 2 light) and SettingChanged when it changes. Answers on a thread
// of its own, since SDL asks while the application starts.
class Portal {
public:
	explicit Portal(std::uint32_t scheme)
		:
		fScheme(scheme)
	{
		DBusError error;
		dbus_error_init(&error);
		fConnection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
		dbus_error_free(&error);
		if (fConnection == nullptr)
			return;
		dbus_connection_set_exit_on_disconnect(fConnection, FALSE);
		fOwner = dbus_bus_request_name(fConnection, "org.freedesktop.portal.Desktop",
			DBUS_NAME_FLAG_DO_NOT_QUEUE, &error) == DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER;
		dbus_error_free(&error);
		if (fOwner)
			fThread = std::thread([this] { _Serve(); });
	}

	~Portal()
	{
		fStop = true;
		if (fThread.joinable())
			fThread.join();
		if (fConnection != nullptr) {
			dbus_connection_close(fConnection);
			dbus_connection_unref(fConnection);
		}
	}

	bool IsOwner() const
	{
		return fOwner;
	}

	// The user switches the system: SettingChanged with \a scheme.
	void Switch(std::uint32_t scheme)
	{
		fScheme = scheme;
		fSwitch = true;
	}

private:
	void _Serve()
	{
		while (!fStop) {
			(void)dbus_connection_read_write(fConnection, 20);
			while (DBusMessage* message = dbus_connection_pop_message(fConnection)) {
				if (dbus_message_is_method_call(message, kPortalInterface, "Read"))
					_Answer(message);
				dbus_message_unref(message);
			}
			if (fSwitch.exchange(false))
				_Changed();
		}
	}

	// Read() answers <<u>>: the value in a variant in a variant.
	void _Answer(DBusMessage* call)
	{
		DBusMessage* reply = dbus_message_new_method_return(call);
		DBusMessageIter args;
		DBusMessageIter outer;
		DBusMessageIter inner;
		const dbus_uint32_t scheme = fScheme;
		dbus_message_iter_init_append(reply, &args);
		(void)dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "v", &outer);
		(void)dbus_message_iter_open_container(&outer, DBUS_TYPE_VARIANT, "u", &inner);
		(void)dbus_message_iter_append_basic(&inner, DBUS_TYPE_UINT32, &scheme);
		(void)dbus_message_iter_close_container(&outer, &inner);
		(void)dbus_message_iter_close_container(&args, &outer);
		(void)dbus_connection_send(fConnection, reply, nullptr);
		dbus_message_unref(reply);
		dbus_connection_flush(fConnection);
	}

	void _Changed()
	{
		DBusMessage* signal = dbus_message_new_signal("/org/freedesktop/portal/desktop",
			kPortalInterface, "SettingChanged");
		DBusMessageIter args;
		DBusMessageIter variant;
		const char* name = "org.freedesktop.appearance";
		const char* key = "color-scheme";
		const dbus_uint32_t scheme = fScheme;
		dbus_message_iter_init_append(signal, &args);
		(void)dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &name);
		(void)dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &key);
		(void)dbus_message_iter_open_container(&args, DBUS_TYPE_VARIANT, "u", &variant);
		(void)dbus_message_iter_append_basic(&variant, DBUS_TYPE_UINT32, &scheme);
		(void)dbus_message_iter_close_container(&args, &variant);
		(void)dbus_connection_send(fConnection, signal, nullptr);
		dbus_message_unref(signal);
		dbus_connection_flush(fConnection);
	}

	DBusConnection* fConnection = nullptr;
	bool fOwner = false;
	std::atomic<std::uint32_t> fScheme;
	std::atomic<bool> fSwitch = false;
	std::atomic<bool> fStop = false;
	std::thread fThread;
};


// Waits until the application's colors are \a palette; false after 5 s.
bool
WaitForColors(const Palette& palette)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (Palette::Current() != palette && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(10ms);
	return Palette::Current() == palette;
}


TEST(SdlX11ThemeTest, TheColorsFollowTheDesktop)
{
	// What is tested: The desktop's portal says dark (color-scheme 1); an
	// application on SDL's X11 driver with the colors of the system's
	// choice (BColorScheme::System); then the user switches the desktop to
	// light (SettingChanged with 2).
	// Success criteria: The platform reports Dark and the application shows
	// Haiku's dark colors; after the switch it reports Light and shows the
	// light ones, without being restarted. Skipped without an X server or a
	// session bus, or when a real portal runs.
	// Why it matters: By default NewGUI follows the user's light or dark
	// choice, also while it runs (as decided for this block); on Linux the
	// desktop tells it through its portal.
	//
	// Given
	if (std::getenv("DBUS_SESSION_BUS_ADDRESS") == nullptr || std::getenv("DISPLAY") == nullptr)
		GTEST_SKIP() << "no session bus or no X server";
	Portal portal(1);
	if (!portal.IsOwner())
		GTEST_SKIP() << "a portal of the desktop runs";
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	BApplication::SetColorScheme(BColorScheme::System);

	// When
	RunningApp app;
	if (app.Platform() == nullptr)
		GTEST_SKIP() << "no X11 video";
	const bool dark = WaitForColors(Palette::Dark());
	const platform::SystemColorScheme reportedDark = app.Platform()->ColorScheme();
	portal.Switch(2);
	const bool light = WaitForColors(Palette::Default());
	const platform::SystemColorScheme reportedLight = app.Platform()->ColorScheme();

	// Then
	EXPECT_TRUE(dark);
	EXPECT_EQ(reportedDark, platform::SystemColorScheme::Dark);
	EXPECT_TRUE(light);
	EXPECT_EQ(reportedLight, platform::SystemColorScheme::Light);
}


} // namespace
