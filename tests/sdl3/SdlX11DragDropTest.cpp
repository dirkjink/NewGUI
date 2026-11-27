/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drops of other programs on X11 (module P4): the test is the other program,
// an XDND source on a connection of its own, and drops on a window of SDL's
// X11 driver as file managers and browsers do. Needs an X server (Xvfb in
// the CI); without one the test is skipped.


#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "sdl3/RunningApp.h"
#include "support/ScopedEnvironment.h"

// after NewGUI's headers: Xlib defines Bool, Status, None and others as
// macros
#include <X11/Xatom.h>
#include <X11/Xlib.h>


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A window "Drops" whose view keeps what is dropped on it.
class DropWindow : public BWindow {
public:
	DropWindow(std::mutex& lock, BMessage& dropped, std::atomic<bool>& done)
		:
		BWindow({200, 200, 400, 320}, "Drops", BWindowLook::Titled)
	{
		class DropView : public BView {
		public:
			DropView(std::mutex& lock, BMessage& dropped, std::atomic<bool>& done)
				:
				BView(gfx::Rect{0, 0, 200, 120}, "drop", kFollowAll, BViewFlag::WillDraw),
				fLock(lock),
				fDropped(dropped),
				fDone(done)
			{
			}

			void MessageReceived(BMessage* message) override
			{
				if (message->what == kSimpleData || message->what == kMimeData) {
					std::scoped_lock guard(fLock);
					fDropped = *message;
					fDone = true;
					return;
				}
				BView::MessageReceived(message);
			}

		private:
			std::mutex& fLock;
			BMessage& fDropped;
			std::atomic<bool>& fDone;
		};
		AddChild(std::make_unique<DropView>(lock, dropped, done));
	}
};


// An XDND source (version 5) on a connection of its own.
class Source {
public:
	explicit Source(Display* display)
		:
		fDisplay(display),
		fWindow(XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 10, 10, 0, 0,
			0)),
		fSelection(Intern("XdndSelection"))
	{
	}

	~Source()
	{
		XDestroyWindow(fDisplay, fWindow);
		XCloseDisplay(fDisplay);
	}

	Atom Intern(const char* name) const
	{
		return XInternAtom(fDisplay, name, False);
	}

	void Send(Window target, const char* type, long l1, long l2, long l3, long l4)
	{
		XEvent event{};
		event.xclient.type = ClientMessage;
		event.xclient.display = fDisplay;
		event.xclient.window = target;
		event.xclient.message_type = Intern(type);
		event.xclient.format = 32;
		event.xclient.data.l[0] = static_cast<long>(fWindow);
		event.xclient.data.l[1] = l1;
		event.xclient.data.l[2] = l2;
		event.xclient.data.l[3] = l3;
		event.xclient.data.l[4] = l4;
		(void)XSendEvent(fDisplay, target, False, NoEventMask, &event);
		(void)XFlush(fDisplay);
	}

	// Offers \a types, more than three: in XdndTypeList.
	void Enter(Window target, const std::vector<Atom>& types)
	{
		(void)XSetSelectionOwner(fDisplay, fSelection, fWindow, CurrentTime);
		(void)XChangeProperty(fDisplay, fWindow, Intern("XdndTypeList"), XA_ATOM, 32,
			PropModeReplace, reinterpret_cast<const unsigned char*>(types.data()),
			static_cast<int>(types.size()));
		Send(target, "XdndEnter", (5L << 24) | 1, 0, 0, 0);
	}

	// The next client message of \a type, serving requests for the data
	// (\a serve: the bytes of a type) meanwhile; false after \a timeout.
	template<typename Serve>
	bool WaitFor(const char* type, Serve serve, XClientMessageEvent* message,
		std::chrono::milliseconds timeout = 5s)
	{
		const Atom wanted = Intern(type);
		const auto deadline = std::chrono::steady_clock::now() + timeout;
		while (std::chrono::steady_clock::now() < deadline) {
			if (XPending(fDisplay) == 0) {
				std::this_thread::sleep_for(5ms);
				continue;
			}
			XEvent event;
			XNextEvent(fDisplay, &event);
			if (event.type == ClientMessage && event.xclient.message_type == wanted) {
				*message = event.xclient;
				return true;
			}
			if (event.type != SelectionRequest)
				continue;
			const XSelectionRequestEvent& request = event.xselectionrequest;
			XEvent reply{};
			reply.xselection.type = SelectionNotify;
			reply.xselection.display = fDisplay;
			reply.xselection.requestor = request.requestor;
			reply.xselection.selection = request.selection;
			reply.xselection.target = request.target;
			reply.xselection.time = request.time;
			reply.xselection.property = None;
			char* name = XGetAtomName(fDisplay, request.target);
			const std::string data = serve(std::string(name));
			XFree(name);
			if (!data.empty()) {
				(void)XChangeProperty(fDisplay, request.requestor, request.property,
					request.target, 8, PropModeReplace,
					reinterpret_cast<const unsigned char*>(data.data()),
					static_cast<int>(data.size()));
				reply.xselection.property = request.property;
			}
			(void)XSendEvent(fDisplay, request.requestor, False, NoEventMask, &reply);
			(void)XFlush(fDisplay);
		}
		return false;
	}

	Display* Connection() const
	{
		return fDisplay;
	}

private:
	Display* fDisplay;
	Window fWindow;
	Atom fSelection;
};


TEST(SdlX11DragDropTest, AWindowTakesDropsWithAllTheirData)
{
	// What is tested: An application on the SDL platform with SDL's X11
	// driver and a window "Drops" with a view; the test, as another
	// program, drags onto it with XDND: four types (text/uri-list with
	// file:///tmp/a%20b.txt, UTF8_STRING "hi", text/plain "latin" and
	// image/png) in XdndTypeList, a position at (50, 40) of the window,
	// the drop, and the data on request.
	// Success criteria: The window answers the position with XdndStatus
	// (accepted) and the drop with XdndFinished (accepted, copy); the view
	// gets kSimpleData, WasDropped(), with the path "/tmp/a b.txt",
	// text/plain "hi" (UTF-8 before the other text) and the PNG's bytes as
	// image/png. Skipped without an X server.
	// Why it matters: SDL takes only files and text on X11; NewGUI's
	// windows must get images and other data from other programs too
	// (module P4), in the protocol file managers and browsers speak.
	//
	// Given
	Display* display = XOpenDisplay(nullptr);
	if (display == nullptr)
		GTEST_SKIP() << "no X server";
	Source source(display);
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	RunningApp app;
	if (app.Platform() == nullptr)
		GTEST_SKIP() << "no X11 video";
	std::mutex lock;
	BMessage dropped;
	std::atomic<bool> done = false;
	auto owned = std::make_unique<DropWindow>(lock, dropped, done);
	owned->Show();
	(void)BLooper::Run(std::move(owned));
	Window target = 0;
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (target == 0 && std::chrono::steady_clock::now() < deadline) {
		target = app.OnMain([] {
			Window found = 0;
			int count = 0;
			SDL_Window** windows = SDL_GetWindows(&count);
			for (int i = 0; windows != nullptr && i < count; i++) {
				const char* title = SDL_GetWindowTitle(windows[i]);
				if (title != nullptr && std::strcmp(title, "Drops") == 0
					&& (SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_HIDDEN) == 0) {
					found = static_cast<Window>(SDL_GetNumberProperty(
						SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
						0));
				}
			}
			SDL_free(windows);
			return found;
		});
		if (target == 0)
			std::this_thread::sleep_for(20ms);
	}
	ASSERT_NE(target, 0u);
	const auto serve = [](const std::string& type) -> std::string {
		if (type == "text/uri-list")
			return "file:///tmp/a%20b.txt\r\n";
		if (type == "UTF8_STRING")
			return "hi";
		if (type == "text/plain")
			return "latin";
		if (type == "image/png")
			return "\x89PNG";
		return {};
	};

	// When
	source.Enter(target, {source.Intern("text/uri-list"), source.Intern("text/plain"),
		source.Intern("UTF8_STRING"), source.Intern("image/png")});
	int rootX = 0;
	int rootY = 0;
	Window child = 0;
	(void)XTranslateCoordinates(display, target, DefaultRootWindow(display), 50, 40, &rootX,
		&rootY, &child);
	source.Send(target, "XdndPosition", 0, (static_cast<long>(rootX) << 16) | rootY,
		CurrentTime, static_cast<long>(source.Intern("XdndActionCopy")));
	XClientMessageEvent status{};
	const bool answered = source.WaitFor("XdndStatus", serve, &status);
	source.Send(target, "XdndDrop", 0, CurrentTime, 0, 0);
	XClientMessageEvent finished{};
	const bool ended = source.WaitFor("XdndFinished", serve, &finished);
	const auto waitEnd = std::chrono::steady_clock::now() + 5s;
	while (!done && std::chrono::steady_clock::now() < waitEnd)
		std::this_thread::sleep_for(10ms);

	// Then
	ASSERT_TRUE(answered);
	EXPECT_EQ(status.data.l[1] & 1, 1);
	ASSERT_TRUE(ended);
	EXPECT_EQ(finished.data.l[1] & 1, 1);
	EXPECT_EQ(static_cast<Atom>(finished.data.l[2]), source.Intern("XdndActionCopy"));
	ASSERT_TRUE(done);
	std::scoped_lock guard(lock);
	EXPECT_EQ(dropped.what, kSimpleData);
	EXPECT_TRUE(dropped.WasDropped());
	EXPECT_EQ(dropped.GetString("paths", ""), "/tmp/a b.txt");
	const auto text = dropped.FindData("text/plain", BTypeCode::Mime);
	ASSERT_TRUE(text.has_value());
	EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()),
		"hi");
	const auto png = dropped.FindData("image/png", BTypeCode::Mime);
	ASSERT_TRUE(png.has_value());
	EXPECT_EQ(png->size(), 4u);
}


} // namespace
