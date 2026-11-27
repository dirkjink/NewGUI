/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drag & drop with other programs on X11 (module P4): the test is the other
// program on a connection of its own. As an XDND source it drops on a
// window of SDL's X11 driver as file managers and browsers do; as an XDND
// target it takes a drag out of a NewGUI window, with the mouse moved and
// pressed through XTest as a user would. Needs an X server (Xvfb in the
// CI); without one the tests are skipped.


#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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
#include <X11/extensions/XTest.h>


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


// A window "Drags" whose view drags "hello" and a PNG when it is pressed.
class DragWindow : public BWindow {
public:
	DragWindow()
		:
		BWindow({200, 200, 400, 320}, "Drags", BWindowLook::Titled)
	{
		class DragView : public BView {
		public:
			DragView()
				:
				BView(gfx::Rect{0, 0, 200, 120}, "drag", kFollowAll, BViewFlag::WillDraw)
			{
			}

			void MouseDown(gfx::Point /*where*/) override
			{
				BMessage message(kSimpleData);
				const std::string_view hello = "hello";
				const std::string_view png = "\x89PNG";
				(void)message.AddData("text/plain", BTypeCode::Mime,
					std::as_bytes(std::span(hello)));
				(void)message.AddData("image/png", BTypeCode::Mime,
					std::as_bytes(std::span(png)));
				DragMessage(message, gfx::Rect{0, 0, 10, 10});
			}
		};
		AddChild(std::make_unique<DragView>());
	}
};


// An XDND target (version 5) of the test's own, a window at (600, 200) of
// the screen; it takes every drag, and asks for UTF8_STRING and image/png.
class DropTarget {
public:
	explicit DropTarget(Display* display)
		:
		fDisplay(display)
	{
		XSetWindowAttributes attributes{};
		attributes.override_redirect = True;
		fWindow = XCreateWindow(display, DefaultRootWindow(display), 600, 200, 200, 150, 0,
			CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
		const Atom version = 5;
		(void)XChangeProperty(display, fWindow, Intern("XdndAware"), XA_ATOM, 32,
			PropModeReplace, reinterpret_cast<const unsigned char*>(&version), 1);
		(void)XMapRaised(display, fWindow);
		(void)XSync(display, False);
	}

	~DropTarget()
	{
		XDestroyWindow(fDisplay, fWindow);
		XCloseDisplay(fDisplay);
	}

	Atom Intern(const char* name) const
	{
		return XInternAtom(fDisplay, name, False);
	}

	// Handles the XDND messages and data that came.
	void Pump()
	{
		while (XPending(fDisplay) > 0) {
			XEvent event;
			XNextEvent(fDisplay, &event);
			if (event.type == ClientMessage)
				_Message(event.xclient);
			else if (event.type == SelectionNotify)
				_Data(event.xselection);
		}
	}

	std::vector<std::string>	types;
	int							positions = 0;
	bool						dropped = false;
	bool						left = false;
	bool						finished = false;
	std::string					text;
	std::string					png;

private:
	void _Message(const XClientMessageEvent& message)
	{
		const Window source = static_cast<Window>(message.data.l[0]);
		if (message.message_type == Intern("XdndEnter")) {
			fSource = source;
			types.clear();
			Atom type = None;
			int format = 0;
			unsigned long count = 0;
			unsigned long after = 0;
			unsigned char* data = nullptr;
			if ((message.data.l[1] & 1) != 0
				&& XGetWindowProperty(fDisplay, source, Intern("XdndTypeList"), 0, 64, False,
					XA_ATOM, &type, &format, &count, &after, &data) == Success && data) {
				const auto* atoms = reinterpret_cast<const Atom*>(data);
				for (unsigned long i = 0; i < count; i++) {
					char* name = XGetAtomName(fDisplay, atoms[i]);
					types.emplace_back(name);
					XFree(name);
				}
			}
			if (data != nullptr)
				XFree(data);
		} else if (message.message_type == Intern("XdndPosition")) {
			positions++;
			_Send(Intern("XdndStatus"), 1, 0, static_cast<long>(Intern("XdndActionCopy")));
		} else if (message.message_type == Intern("XdndLeave")) {
			left = true;
		} else if (message.message_type == Intern("XdndDrop")) {
			dropped = true;
			fTime = static_cast<Time>(message.data.l[2]);
			_Ask("UTF8_STRING");
		}
	}

	void _Data(const XSelectionEvent& selection)
	{
		std::string bytes;
		if (selection.property != None) {
			Atom type = None;
			int format = 0;
			unsigned long count = 0;
			unsigned long after = 0;
			unsigned char* data = nullptr;
			if (XGetWindowProperty(fDisplay, fWindow, selection.property, 0, 1024, True,
					AnyPropertyType, &type, &format, &count, &after, &data) == Success
				&& data != nullptr) {
				bytes.assign(reinterpret_cast<const char*>(data), count);
			}
			if (data != nullptr)
				XFree(data);
		}
		if (selection.target == Intern("UTF8_STRING")) {
			text = bytes;
			_Ask("image/png");
			return;
		}
		png = bytes;
		_Send(Intern("XdndFinished"), 1, static_cast<long>(Intern("XdndActionCopy")), 0);
		finished = true;
	}

	void _Ask(const char* type)
	{
		(void)XConvertSelection(fDisplay, Intern("XdndSelection"), Intern(type),
			Intern("TEST_DATA"), fWindow, fTime);
		(void)XFlush(fDisplay);
	}

	void _Send(Atom type, long l1, long l2, long l4)
	{
		XEvent event{};
		event.xclient.type = ClientMessage;
		event.xclient.display = fDisplay;
		event.xclient.window = fSource;
		event.xclient.message_type = type;
		event.xclient.format = 32;
		event.xclient.data.l[0] = static_cast<long>(fWindow);
		event.xclient.data.l[1] = l1;
		event.xclient.data.l[2] = l2;
		event.xclient.data.l[4] = l4;
		(void)XSendEvent(fDisplay, fSource, False, NoEventMask, &event);
		(void)XFlush(fDisplay);
	}

	Display* fDisplay;
	Window fWindow = 0;
	Window fSource = 0;
	Time fTime = CurrentTime;
};


TEST(SdlX11DragDropTest, DragsGoToOtherPrograms)
{
	// What is tested: An application on the SDL platform with SDL's X11
	// driver and a window "Drags" whose view drags "hello" (text/plain)
	// and a PNG (image/png) when pressed; the test, as another program, has
	// an XDND target window at (600, 200) of the screen. Through XTest the
	// mouse is pressed in the view, moved in steps onto the target and
	// released there; the target accepts every position and asks for
	// UTF8_STRING and image/png after the drop.
	// Success criteria: The target hears XdndEnter with the types
	// text/plain;charset=utf-8, UTF8_STRING, text/plain and image/png in
	// XdndTypeList, at least one XdndPosition and the XdndDrop, no
	// XdndLeave; it gets "hello" and the PNG's four bytes. Skipped without
	// an X server or XTest.
	// Why it matters: A drag out of NewGUI's windows must reach other
	// programs on X11 as on Windows and macOS (module P4); SDL drags
	// nothing out.
	//
	// Given
	Display* display = XOpenDisplay(nullptr);
	if (display == nullptr)
		GTEST_SKIP() << "no X server";
	int event = 0;
	int error = 0;
	int major = 0;
	int minor = 0;
	if (!XTestQueryExtension(display, &event, &error, &major, &minor)) {
		XCloseDisplay(display);
		GTEST_SKIP() << "no XTest";
	}
	DropTarget target(display);
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "x11");
	RunningApp app;
	if (app.Platform() == nullptr)
		GTEST_SKIP() << "no X11 video";
	auto owned = std::make_unique<DragWindow>();
	owned->Show();
	(void)BLooper::Run(std::move(owned));
	Window window = 0;
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (window == 0 && std::chrono::steady_clock::now() < deadline) {
		window = app.OnMain([] {
			Window found = 0;
			int count = 0;
			SDL_Window** windows = SDL_GetWindows(&count);
			for (int i = 0; windows != nullptr && i < count; i++) {
				const char* title = SDL_GetWindowTitle(windows[i]);
				if (title != nullptr && std::strcmp(title, "Drags") == 0
					&& (SDL_GetWindowFlags(windows[i]) & SDL_WINDOW_HIDDEN) == 0) {
					found = static_cast<Window>(SDL_GetNumberProperty(
						SDL_GetWindowProperties(windows[i]), SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
						0));
				}
			}
			SDL_free(windows);
			return found;
		});
		if (window == 0)
			std::this_thread::sleep_for(20ms);
	}
	ASSERT_NE(window, 0u);
	// the window drawn and taking the mouse
	std::this_thread::sleep_for(200ms);
	int startX = 0;
	int startY = 0;
	Window child = 0;
	(void)XTranslateCoordinates(display, window, DefaultRootWindow(display), 50, 40, &startX,
		&startY, &child);
	const auto pause = [&target](std::chrono::milliseconds time) {
		const auto end = std::chrono::steady_clock::now() + time;
		while (std::chrono::steady_clock::now() < end) {
			target.Pump();
			std::this_thread::sleep_for(5ms);
		}
	};

	// When
	(void)XTestFakeMotionEvent(display, -1, startX, startY, CurrentTime);
	(void)XFlush(display);
	pause(100ms);
	(void)XTestFakeButtonEvent(display, 1, True, CurrentTime);
	(void)XFlush(display);
	pause(100ms);
	for (int step = 1; step <= 20; step++) {
		(void)XTestFakeMotionEvent(display, -1, startX + (700 - startX) * step / 20,
			startY + (270 - startY) * step / 20, CurrentTime);
		(void)XFlush(display);
		pause(40ms);
	}
	(void)XTestFakeButtonEvent(display, 1, False, CurrentTime);
	(void)XFlush(display);
	const auto end = std::chrono::steady_clock::now() + 5s;
	while (!target.finished && std::chrono::steady_clock::now() < end)
		pause(10ms);

	// Then
	EXPECT_EQ(target.types, (std::vector<std::string>{"text/plain;charset=utf-8",
		"UTF8_STRING", "text/plain", "image/png"}));
	EXPECT_GE(target.positions, 1);
	EXPECT_TRUE(target.dropped);
	EXPECT_FALSE(target.left);
	EXPECT_EQ(target.text, "hello");
	EXPECT_EQ(target.png, "\x89PNG");
}


} // namespace
