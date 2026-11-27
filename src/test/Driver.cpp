/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// The test driver of specification 20.4 over the headless platform: input
// as the platform's messages to the windows (as SdlEvents.cpp makes them),
// queries on the windows' threads (RunOnLooper()), the idle state from
// the loopers' ports (LooperPort::States() and Activity()).

#include <ngui/test/Driver.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Control.h>
#include <ngui/LocaleRoster.h>
#include <ngui/LookRegistry.h>
#include <ngui/PropertyInfo.h>
#include <ngui/StringView.h>
#include <ngui/TextControl.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>

#include "LooperPort.h"
#include "PlatformEvent.h"
#include "WindowList.h"


namespace ngui::test {


namespace {


using namespace std::chrono_literals;

// How long a query waits for a busy window.
constexpr std::chrono::microseconds kQueryTimeout = 5s;


// Whether \a view is \a wanted or below it.
bool
Contains(BView* view, const BView* wanted)
{
	if (view == wanted)
		return true;
	for (std::int32_t i = 0; i < view->CountChildren(); i++) {
		if (Contains(view->ChildAt(i), wanted))
			return true;
	}
	return false;
}


bool
InWindow(BWindow& window, const BView* wanted)
{
	for (std::int32_t i = 0; i < window.CountChildren(); i++) {
		if (Contains(window.ChildAt(i), wanted))
			return true;
	}
	return false;
}


// The views of \a view's tree that \a matches, depth first.
void
Collect(BView* view, const std::function<bool(BView&)>& matches, bool all,
	std::vector<BView*>* found)
{
	if (!all && !found->empty())
		return;
	if (matches(*view))
		found->push_back(view);
	for (std::int32_t i = 0; i < view->CountChildren(); i++)
		Collect(view->ChildAt(i), matches, all, found);
}


std::string
FourCharacters(std::uint32_t what)
{
	std::string text;
	for (int shift = 24; shift >= 0; shift -= 8) {
		const auto c = static_cast<char>((what >> shift) & 0xff);
		text.push_back(c >= 0x20 && c < 0x7f ? c : '?');
	}
	return text;
}


} // namespace


// #pragma mark - TestEnvironment


TestEnvironment::TestEnvironment()
{
	Clock::SetDefault(&fClock);
	BLocaleRoster::Default()->SetPreferredLanguages({"en"});
	BLocaleRoster::Default()->SetCatalog(nullptr);
	// Haiku's light colors, whatever the system or the environment chose
	BApplication::SetColorScheme(BColorScheme::Light);
}


TestEnvironment::~TestEnvironment()
{
	Clock::SetDefault(nullptr);
	BLocaleRoster::Default()->SetPreferredLanguages({});
	BApplication::SetColorScheme(BColorScheme::System);
}


ManualClock&
TestEnvironment::Clock()
{
	return fClock;
}


// #pragma mark - ViewRef


ViewRef::ViewRef(BMessenger window, BView* view)
	:
	fWindow(std::move(window)),
	fView(view)
{
}


bool
ViewRef::IsValid() const
{
	return fView != nullptr;
}


std::expected<void, Error>
ViewRef::Run(const std::function<void(BView&)>& work) const
{
	if (fView == nullptr)
		return Failure(ErrorCode::NotFound, "no view");
	bool found = false;
	BView* view = fView;
	auto ran = RunOnLooper(fWindow, [&](BLooper& looper) {
		auto* window = dynamic_cast<BWindow*>(&looper);
		if (window == nullptr || !InWindow(*window, view))
			return;
		found = true;
		work(*view);
	}, kQueryTimeout);
	if (!ran.has_value())
		return ran;
	if (!found)
		return Failure(ErrorCode::NotFound, "the view is gone");
	return {};
}


bool
ViewRef::Exists() const
{
	return Run([](BView&) {}).has_value();
}


std::string
ViewRef::Name() const
{
	std::string name;
	(void)Run([&](BView& view) { name = view.Name(); });
	return name;
}


std::optional<gfx::Rect>
ViewRef::Frame() const
{
	std::optional<gfx::Rect> frame;
	(void)Run([&](BView& view) { frame = view.ConvertToWindow(view.Bounds()); });
	return frame;
}


bool
ViewRef::IsVisible() const
{
	bool visible = false;
	(void)Run([&](BView& view) {
		visible = true;
		for (const BView* at = &view; at != nullptr; at = at->Parent()) {
			if (at->IsHidden())
				visible = false;
		}
	});
	return visible;
}


bool
ViewRef::IsEnabled() const
{
	bool enabled = false;
	(void)Run([&](BView& view) {
		const auto* control = dynamic_cast<const BControl*>(&view);
		enabled = control == nullptr || control->IsEnabled();
	});
	return enabled;
}


bool
ViewRef::HasFocus() const
{
	bool focus = false;
	(void)Run([&](BView& view) { focus = view.IsFocus(); });
	return focus;
}


std::optional<std::int32_t>
ViewRef::Value() const
{
	return With<BControl>([](BControl& control) { return control.Value(); });
}


std::string
ViewRef::Text() const
{
	std::string text;
	(void)Run([&](BView& view) {
		if (const auto* textView = dynamic_cast<const BTextView*>(&view))
			text = textView->Text();
		else if (const auto* textControl = dynamic_cast<const BTextControl*>(&view))
			text = textControl->Text();
		else if (const auto* stringView = dynamic_cast<const BStringView*>(&view))
			text = stringView->Text();
		else if (const auto* control = dynamic_cast<const BControl*>(&view))
			text = control->Label();
	});
	return text;
}


std::string
ViewRef::Label() const
{
	return With<BControl>([](BControl& control) { return control.Label(); }).value_or("");
}


// #pragma mark - WindowRef


WindowRef::WindowRef(Driver* driver, BMessenger target)
	:
	fDriver(driver),
	fTarget(std::move(target))
{
}


platform::HeadlessWindow*
WindowRef::PlatformWindow() const
{
	return fDriver != nullptr ? fDriver->_PlatformWindow(fTarget) : nullptr;
}


bool
WindowRef::IsValid() const
{
	return PlatformWindow() != nullptr;
}


std::string
WindowRef::Title() const
{
	platform::HeadlessWindow* window = PlatformWindow();
	return window != nullptr ? window->Title() : std::string();
}


gfx::Rect
WindowRef::Frame() const
{
	platform::HeadlessWindow* window = PlatformWindow();
	if (window == nullptr)
		return {0, 0, 0, 0};
	const gfx::Rect frame = window->Frame();
	// inside the frame NewGUI draws, if it draws one
	const platform::WindowFrameLayout layout = window->FrameLayout();
	if (layout.contentSize.w <= 0 && layout.contentSize.h <= 0)
		return frame;
	const gfx::Point origin = {frame.x0 + layout.contentOffset.x,
		frame.y0 + layout.contentOffset.y};
	return {origin.x, origin.y, origin.x + layout.contentSize.w,
		origin.y + layout.contentSize.h};
}


std::expected<BBitmap, Error>
WindowRef::Snapshot(const BSnapshotOptions& options) const
{
	return BWindow::Snapshot(fTarget, options).get();
}


std::expected<BBitmap, Error>
WindowRef::CaptureFrame() const
{
	return BWindow::CaptureFrame(fTarget).get();
}


std::vector<ViewRef>
WindowRef::FindAllIf(const std::function<bool(BView&)>& matches) const
{
	std::vector<ViewRef> refs;
	std::vector<BView*> found;
	BMessenger windowMessenger;
	(void)RunOnLooper(fTarget, [&](BLooper& looper) {
		auto* window = dynamic_cast<BWindow*>(&looper);
		if (window == nullptr)
			return;
		windowMessenger = BMessenger(window);
		for (std::int32_t i = 0; i < window->CountChildren(); i++)
			Collect(window->ChildAt(i), matches, true, &found);
	}, kQueryTimeout);
	for (BView* view : found)
		refs.push_back(ViewRef(windowMessenger, view));
	return refs;
}


ViewRef
WindowRef::FindIf(const std::function<bool(BView&)>& matches) const
{
	std::vector<BView*> found;
	BMessenger windowMessenger;
	(void)RunOnLooper(fTarget, [&](BLooper& looper) {
		auto* window = dynamic_cast<BWindow*>(&looper);
		if (window == nullptr)
			return;
		windowMessenger = BMessenger(window);
		for (std::int32_t i = 0; i < window->CountChildren() && found.empty(); i++)
			Collect(window->ChildAt(i), matches, false, &found);
	}, kQueryTimeout);
	return found.empty() ? ViewRef() : ViewRef(windowMessenger, found.front());
}


ngui::AccessibleTree
WindowRef::AccessibleTree() const
{
	ngui::AccessibleTree tree;
	(void)RunOnLooper(fTarget, [&](BLooper& looper) {
		if (const auto* window = dynamic_cast<const BWindow*>(&looper))
			tree = window->GetAccessibleTree();
	}, kQueryTimeout);
	return tree;
}


ViewRef
WindowRef::FindByLabel(std::string_view label) const
{
	const std::string wanted(label);
	return FindIf([&](BView& view) {
		if (const auto* control = dynamic_cast<const BControl*>(&view))
			return control->Label() == wanted;
		if (const auto* stringView = dynamic_cast<const BStringView*>(&view))
			return stringView->Text() == wanted;
		return false;
	});
}


ViewRef
WindowRef::FindByPath(std::string_view path) const
{
	std::vector<std::string> names;
	while (!path.empty()) {
		const std::size_t end = std::min(path.find('/'), path.size());
		names.emplace_back(path.substr(0, end));
		path.remove_prefix(std::min(end + 1, path.size()));
	}
	if (names.empty())
		return {};
	BView* found = nullptr;
	BMessenger windowMessenger;
	(void)RunOnLooper(fTarget, [&](BLooper& looper) {
		auto* window = dynamic_cast<BWindow*>(&looper);
		if (window == nullptr)
			return;
		windowMessenger = BMessenger(window);
		// each name among the children of the view before
		std::vector<BView*> level;
		for (std::int32_t i = 0; i < window->CountChildren(); i++)
			level.push_back(window->ChildAt(i));
		for (std::size_t n = 0; n < names.size(); n++) {
			BView* match = nullptr;
			for (BView* view : level) {
				if (view->Name() == names[n]) {
					match = view;
					break;
				}
			}
			if (match == nullptr)
				return;
			if (n + 1 == names.size()) {
				found = match;
				return;
			}
			level.clear();
			for (std::int32_t i = 0; i < match->CountChildren(); i++)
				level.push_back(match->ChildAt(i));
		}
	}, kQueryTimeout);
	return found == nullptr ? ViewRef() : ViewRef(windowMessenger, found);
}


// #pragma mark - Driver


Driver::Driver(BApplication& app)
	:
	fApp(app)
{
	if (app.Platform() == nullptr) {
		if (auto used = app.UsePlatform("headless"); !used.has_value()) {
			fError = used.error();
			return;
		}
	}
	fPlatform = dynamic_cast<platform::HeadlessPlatform*>(app.Platform());
	if (fPlatform == nullptr) {
		fError = Error{ErrorCode::InvalidArgument, "the application has another platform"};
		return;
	}
	fRunner = std::thread([this] { (void)fApp.Run(); });
	const auto deadline = std::chrono::steady_clock::now() + kDefaultTimeout;
	while (platform::Current() != fPlatform && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(100us);
}


Driver::~Driver()
{
	if (fRunner.joinable()) {
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
	}
}


std::expected<void, Error>
Driver::InitCheck() const
{
	if (fError.has_value())
		return std::unexpected(*fError);
	return {};
}


platform::HeadlessPlatform&
Driver::Platform() const
{
	return *fPlatform;
}


std::expected<void, Error>
Driver::WaitForIdle(std::chrono::milliseconds timeout)
{
	if (fPlatform == nullptr)
		return std::unexpected(fError.value_or(Error{ErrorCode::InvalidArgument, "no platform"}));
	const auto deadline = std::chrono::steady_clock::now() + timeout;
	while (true) {
		// Idle only if nothing happened while every part was looked at: a
		// message on its way from one looper to another would otherwise go
		// unseen.
		const std::uint64_t loopers = LooperPort::Activity();
		const std::uint64_t platform = fPlatform->Activity();
		bool idle = fPlatform->IsIdle();
		for (const LooperPort::State& state : LooperPort::States())
			idle = idle && state.queued == 0 && !state.dispatching;
		const std::optional<Clock::Time> next = Clock::Default().NextTimer();
		idle = idle && (!next.has_value() || *next > Clock::Default().Now());
		if (idle && loopers == LooperPort::Activity() && platform == fPlatform->Activity())
			return {};
		if (std::chrono::steady_clock::now() > deadline)
			return Failure(ErrorCode::TimedOut, "not idle after the timeout:\n" + _Report());
		std::this_thread::sleep_for(100us);
	}
}


std::expected<void, Error>
Driver::Advance(Clock::Time duration)
{
	auto* clock = dynamic_cast<ManualClock*>(&Clock::Default());
	if (clock == nullptr)
		return Failure(ErrorCode::InvalidArgument, "the clock is no ManualClock");
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	clock->Advance(duration);
	return WaitForIdle();
}


std::expected<void, Error>
Driver::PerformAccessibleAction(const WindowRef& window, std::uint64_t node,
	AccessibleAction action, const BMessage& data)
{
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	BMessage message(data);
	message.what = kAccessibleAction;
	(void)message.RemoveName("node");
	(void)message.RemoveName("action");
	(void)message.AddInt64("node", static_cast<std::int64_t>(node));
	(void)message.AddInt32("action", static_cast<std::int32_t>(action));
	BMessage reply;
	if (auto sent = window.fTarget.SendMessage(message, &reply); !sent.has_value())
		return sent;
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	return ScriptingResult(reply);
}


std::expected<void, Error>
Driver::Replay(const Recording& recording)
{
	if (dynamic_cast<ManualClock*>(&Clock::Default()) == nullptr)
		return Failure(ErrorCode::InvalidArgument, "the clock is no ManualClock");
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	if (!recording.start.look.empty()) {
		if (auto look = LookRegistry::Default().SetCurrent(recording.start.look);
				!look.has_value()) {
			return look;
		}
		if (auto idle = WaitForIdle(); !idle.has_value())
			return idle;
	}
	const std::vector<detail::WindowEntry> windows = detail::WindowEntries();
	if (windows.size() != recording.start.windows.size()) {
		return Failure(ErrorCode::NotFound, "the recording began with "
			+ std::to_string(recording.start.windows.size()) + " windows, there are "
			+ std::to_string(windows.size()));
	}
	std::vector<std::uint64_t> startSerials;
	for (const detail::WindowEntry& window : windows)
		startSerials.push_back(window.serial);
	const std::uint64_t firstNewSerial = detail::NextWindowSerial();

	Clock::Time elapsed{};
	for (std::size_t i = 0; i < recording.events.size(); i++) {
		const RecordedEvent& event = recording.events[i];
		if (event.time > elapsed) {
			if (auto advanced = Advance(event.time - elapsed); !advanced.has_value())
				return advanced;
			elapsed = event.time;
		}
		if (auto sent = _ReplayEvent(event, startSerials, firstNewSerial); !sent.has_value()) {
			return Failure(sent.error().code, "event " + std::to_string(i) + ": "
				+ sent.error().message);
		}
		if (auto idle = WaitForIdle(); !idle.has_value())
			return idle;
	}
	return {};
}


WindowRef
Driver::Window(std::string_view title)
{
	(void)WaitForIdle();
	for (WindowRef& window : Windows()) {
		if (window.Title() == title)
			return window;
	}
	return {};
}


std::vector<WindowRef>
Driver::Windows()
{
	(void)WaitForIdle();
	std::vector<WindowRef> windows;
	if (fPlatform == nullptr)
		return windows;
	for (platform::HeadlessWindow* window : fPlatform->Windows())
		windows.push_back(WindowRef(this, window->Target()));
	return windows;
}


std::expected<void, Error>
Driver::Click(const ViewRef& view, std::int32_t buttons, std::int32_t clicks)
{
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	const std::optional<gfx::Rect> frame = view.Frame();
	if (!frame.has_value())
		return Failure(ErrorCode::NotFound, "the view is gone");
	platform::HeadlessWindow* window = _PlatformWindow(view.fWindow);
	if (window == nullptr)
		return Failure(ErrorCode::NotFound, "the view's window is gone");
	const gfx::Point middle{(frame->x0 + frame->x1) / 2, (frame->y0 + frame->y1) / 2};
	window->Activate();
	if (auto moved = _Mouse(window, kMouseMoved, middle, 0, 0); !moved.has_value())
		return moved;
	if (auto down = _Mouse(window, kMouseDown, middle, buttons, clicks); !down.has_value())
		return down;
	return _Mouse(window, kMouseUp, middle, 0, 0);
}


std::expected<void, Error>
Driver::ClickAt(const WindowRef& windowRef, gfx::Point where, std::int32_t buttons,
	std::int32_t clicks)
{
	platform::HeadlessWindow* window = windowRef.PlatformWindow();
	if (window == nullptr)
		return Failure(ErrorCode::NotFound, "the window is gone");
	if (!window->IsPopup())
		window->Activate();
	if (auto moved = _Mouse(window, kMouseMoved, where, 0, 0); !moved.has_value())
		return moved;
	if (auto down = _Mouse(window, kMouseDown, where, buttons, clicks); !down.has_value())
		return down;
	return _Mouse(window, kMouseUp, where, 0, 0);
}


std::expected<void, Error>
Driver::Drag(const WindowRef& windowRef, gfx::Point from, gfx::Point to, std::int32_t steps)
{
	platform::HeadlessWindow* window = windowRef.PlatformWindow();
	if (window == nullptr)
		return Failure(ErrorCode::NotFound, "the window is gone");
	if (!window->IsPopup())
		window->Activate();
	if (auto moved = _Mouse(window, kMouseMoved, from, 0, 0); !moved.has_value())
		return moved;
	if (auto down = _Mouse(window, kMouseDown, from, 1, 1); !down.has_value())
		return down;
	steps = std::max(steps, 1);
	for (std::int32_t i = 1; i <= steps; i++) {
		const float t = static_cast<float>(i) / static_cast<float>(steps);
		const gfx::Point at{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t};
		if (auto moved = _Mouse(window, kMouseMoved, at, 1, 0); !moved.has_value())
			return moved;
	}
	return _Mouse(window, kMouseUp, to, 0, 0);
}


std::expected<void, Error>
Driver::Scroll(const ViewRef& view, float dx, float dy)
{
	if (auto idle = WaitForIdle(); !idle.has_value())
		return idle;
	const std::optional<gfx::Rect> frame = view.Frame();
	platform::HeadlessWindow* window = _PlatformWindow(view.fWindow);
	if (!frame.has_value() || window == nullptr)
		return Failure(ErrorCode::NotFound, "the view is gone");
	const gfx::Point middle{(frame->x0 + frame->x1) / 2, (frame->y0 + frame->y1) / 2};
	if (auto moved = _Mouse(window, kMouseMoved, middle, 0, 0); !moved.has_value())
		return moved;
	BMessage wheel(kMouseWheelChanged);
	(void)wheel.AddInt64("when", Clock::Default().Now().count());
	(void)wheel.AddFloat("be:wheel_delta_x", dx);
	(void)wheel.AddFloat("be:wheel_delta_y", dy);
	return window->Send(wheel);
}


std::expected<void, Error>
Driver::Type(const ViewRef& view, std::string_view text)
{
	if (auto clicked = Click(view); !clicked.has_value())
		return clicked;
	return Type(text);
}


std::expected<void, Error>
Driver::Type(std::string_view text)
{
	platform::HeadlessWindow* window = _KeyWindow();
	if (window == nullptr)
		return Failure(ErrorCode::NotFound, "no window to type into");
	// a key per character: a lead byte and its continuation bytes
	std::size_t i = 0;
	while (i < text.size()) {
		std::size_t end = i + 1;
		while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
			end++;
		if (auto sent = _SendKey(window, text.substr(i, end - i), {}); !sent.has_value())
			return sent;
		i = end;
	}
	return {};
}


std::expected<void, Error>
Driver::Key(char key, BModifierFlags modifiers)
{
	platform::HeadlessWindow* window = _KeyWindow();
	if (window == nullptr)
		return Failure(ErrorCode::NotFound, "no window for the key");
	return _SendKey(window, std::string_view(&key, 1), modifiers);
}


platform::HeadlessWindow*
Driver::_PlatformWindow(const BMessenger& target) const
{
	if (fPlatform == nullptr)
		return nullptr;
	for (platform::HeadlessWindow* window : fPlatform->Windows()) {
		if (window->Target() == target)
			return window;
	}
	return nullptr;
}


// Sends \a event of a recording to where it went: its window, its popup or
// the application.
std::expected<void, Error>
Driver::_ReplayEvent(const RecordedEvent& event, const std::vector<std::uint64_t>& startSerials,
	std::uint64_t firstNewSerial)
{
	if (event.window < 0) {
		BMessage message(event.message);
		PlatformEventMark::Set(message);
		return AppMessenger().SendMessage(message);
	}
	const auto number = static_cast<std::uint64_t>(event.window);
	const std::uint64_t serial = number < startSerials.size()
		? startSerials[static_cast<std::size_t>(number)]
		: firstNewSerial + (number - startSerials.size());
	BMessenger target;
	for (const detail::WindowEntry& window : detail::WindowEntries()) {
		if (window.serial == serial)
			target = window.messenger;
	}
	if (!target.IsValid()) {
		return Failure(ErrorCode::NotFound, "window " + std::to_string(event.window)
			+ " of the recording is not there");
	}
	platform::HeadlessWindow* platformWindow = _PlatformWindow(target);
	if (event.popup >= 0) {
		// the window's open popups, in the order they opened
		std::vector<platform::HeadlessWindow*> popups;
		if (fPlatform != nullptr && platformWindow != nullptr) {
			for (platform::HeadlessWindow* window : fPlatform->Windows()) {
				if (window->IsPopup() && window->Parent() == platformWindow)
					popups.push_back(window);
			}
		}
		if (static_cast<std::size_t>(event.popup) >= popups.size()) {
			return Failure(ErrorCode::NotFound, "popup " + std::to_string(event.popup)
				+ " of window " + std::to_string(event.window) + " is not open");
		}
		return popups[static_cast<std::size_t>(event.popup)]->Send(event.message);
	}
	if (platformWindow == nullptr) {
		BMessage message(event.message);
		PlatformEventMark::Set(message);
		return target.SendMessage(message);
	}
	// the window system's own idea of the window, as before such an event
	if (event.message.what == kWindowMoved)
		platformWindow->MoveTo(event.message.GetPoint("where", platformWindow->Frame().LeftTop()));
	if (event.message.what == kWindowResized) {
		const gfx::Rect frame = platformWindow->Frame();
		platformWindow->ResizeTo({event.message.GetFloat("width", frame.Width()),
			event.message.GetFloat("height", frame.Height())});
	}
	return platformWindow->Send(event.message);
}


// The window keys go to: the active one, else the first that is no popup.
platform::HeadlessWindow*
Driver::_KeyWindow() const
{
	if (fPlatform == nullptr)
		return nullptr;
	if (platform::HeadlessWindow* active = fPlatform->ActiveWindow())
		return active;
	for (platform::HeadlessWindow* window : fPlatform->Windows()) {
		if (!window->IsPopup())
			return window;
	}
	return nullptr;
}


// A key down and up, as SdlEvents.cpp makes them: "key" 0 (no scan code),
// the bytes and modifiers.
std::expected<void, Error>
Driver::_SendKey(platform::HeadlessWindow* window, std::string_view bytes,
	BModifierFlags modifiers)
{
	for (const std::uint32_t what : {kKeyDown, kKeyUp}) {
		BMessage key(what);
		(void)key.AddInt64("when", Clock::Default().Now().count());
		(void)key.AddInt32("key", 0);
		(void)key.AddInt32("modifiers", static_cast<std::int32_t>(modifiers.Raw()));
		(void)key.AddString("bytes", bytes);
		if (what == kKeyDown)
			(void)key.AddInt32("be:key_repeat", 0);
		if (auto sent = window->Send(key); !sent.has_value())
			return sent;
	}
	return {};
}


// A mouse message as SdlEvents.cpp makes it.
std::expected<void, Error>
Driver::_Mouse(platform::HeadlessWindow* window, std::uint32_t what, gfx::Point where,
	std::int32_t buttons, std::int32_t clicks)
{
	BMessage message(what);
	(void)message.AddInt64("when", Clock::Default().Now().count());
	// in the window: with a frame NewGUI draws, the content lies inside it
	const gfx::Point offset = window->FrameLayout().contentOffset;
	(void)message.AddPoint("where", {where.x + offset.x, where.y + offset.y});
	(void)message.AddInt32("buttons", buttons);
	(void)message.AddInt32("modifiers", 0);
	if (what == kMouseDown)
		(void)message.AddInt32("clicks", clicks);
	return window->Send(message);
}


// What keeps the application busy, for a WaitForIdle() that times out.
std::string
Driver::_Report() const
{
	std::ostringstream report;
	for (const LooperPort::State& state : LooperPort::States()) {
		if (state.queued == 0 && !state.dispatching)
			continue;
		report << "  looper \"" << state.name << "\":";
		if (state.dispatching)
			report << " handling " << FourCharacters(state.handling) << ";";
		if (state.queued > 0) {
			report << " " << state.queued << " queued (";
			for (std::size_t i = 0; i < state.waiting.size() && i < 8; i++)
				report << (i > 0 ? ", " : "") << FourCharacters(state.waiting[i]);
			report << (state.waiting.size() > 8 ? ", ...)" : ")");
		}
		report << "\n";
	}
	if (fPlatform != nullptr && !fPlatform->IsIdle())
		report << "  the platform has work\n";
	const std::optional<Clock::Time> next = Clock::Default().NextTimer();
	if (next.has_value() && *next <= Clock::Default().Now())
		report << "  a timer is due at " << next->count() << " us\n";
	return report.str();
}


} // namespace ngui::test
