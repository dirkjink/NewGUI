/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */
#pragma once


#include <chrono>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <ngui/Application.h>
#include <ngui/Clock.h>
#include <ngui/Error.h>
#include <ngui/InterfaceDefs.h>
#include <ngui/Messenger.h>
#include <ngui/View.h>
#include <ngui/gfx/Rect.h>
#include <ngui/platform/HeadlessPlatform.h>


namespace ngui::test {


class Driver;
class WindowRef;


/*!	What a test fixes so that runs repeat (specification 20.3): a
	ManualClock as Clock::Default() (rule T1: time moves only by
	Driver::Advance()), English without a catalog. The text system stays
	the application's (FontTextSystem::CreateBundled() gives the same
	metrics everywhere). Restores the defaults when it goes; one at a time.
*/
class TestEnvironment {
public:
								TestEnvironment();
									/*!< Sets the clock and the language. */
								~TestEnvironment();
									/*!< Restores the system clock and the user's
										languages. */

								TestEnvironment(const TestEnvironment&) = delete;
			TestEnvironment&	operator=(const TestEnvironment&) = delete;

			ManualClock&		Clock();
									/*!< The clock of the test. */

private:
			ManualClock			fClock;
};


/*!	A view found by a query (specification 20.4). The view lives on its
	window's thread, so every question runs there; a view that is gone
	since gives nothing (std::nullopt, false, empty). Copyable.
*/
class ViewRef {
public:
								ViewRef() = default;
									/*!< Refers to nothing. */

			bool				IsValid() const;
									/*!< Whether the query found a view. */
			bool				Exists() const;
									/*!< Whether the view is still in its window. */
			std::string			Name() const;
									/*!< Its name. */
			std::optional<gfx::Rect> Frame() const;
									/*!< Its bounds in window coordinates. */
			bool				IsVisible() const;
									/*!< Neither it nor a parent is hidden. */
			bool				IsEnabled() const;
									/*!< A BControl's IsEnabled(), true for other views. */
			bool				HasFocus() const;
									/*!< Whether it is the focus of its window. */
			std::optional<std::int32_t> Value() const;
									/*!< A BControl's value. */
			std::string			Text() const;
									/*!< The text of a BTextView, BTextControl or
										BStringView, else a BControl's label. */
			std::string			Label() const;
									/*!< A BControl's label. */

			std::expected<void, Error> Run(const std::function<void(BView&)>& work) const;
									/*!< Runs \a work with the view on its window's thread;
										NotFound if it is gone. */

			template<typename T, typename Work>
			auto				With(Work work) const
									-> std::optional<decltype(work(std::declval<T&>()))>;
									/*!< \a work's result for the view as a \a T, on its
										window's thread; nullopt if it is gone or no \a T. */

private:
	friend class WindowRef;
	friend class Driver;

								ViewRef(BMessenger window, BView* view);

			BMessenger			fWindow;
			BView*				fView = nullptr;
};


/*!	A window of the application under test, on the headless platform.
	Copyable; a window that closed answers with nothing.
*/
class WindowRef {
public:
								WindowRef() = default;
									/*!< Refers to nothing. */

			bool				IsValid() const;
									/*!< Whether it refers to a window that is still
										there. */
			std::string			Title() const;
									/*!< Its title. */
			gfx::Rect			Frame() const;
									/*!< Its content area on the virtual screen. */
			platform::HeadlessWindow* PlatformWindow() const;
									/*!< Its window of the headless platform, nullptr if
										it closed. */
			std::expected<BBitmap, Error> Snapshot(const BSnapshotOptions& options = {}) const;
									/*!< BWindow::Snapshot() of the window (its popups'
										BWindow for a popup), waited for. */
			std::expected<BBitmap, Error> CaptureFrame() const;
									/*!< BWindow::CaptureFrame(), waited for. */

			ViewRef				FindIf(const std::function<bool(BView&)>& matches) const;
									/*!< The first view, depth first, that \a matches. */
			std::vector<ViewRef> FindAllIf(const std::function<bool(BView&)>& matches) const;
									/*!< All views that \a matches, depth first. */
			template<typename T = BView>
			ViewRef				Find(std::string_view name) const;
									/*!< The first \a T named \a name. */
			template<typename T = BView>
			std::vector<ViewRef> FindAll() const;
									/*!< All \a Ts. */
			ViewRef				FindByLabel(std::string_view label) const;
									/*!< The first view whose label (or a string view's
										text) is \a label. */
			ViewRef				FindByPath(std::string_view path) const;
									/*!< The view at \a path of names from the window's
										top views down, separated by "/" ("box/ok"). */

private:
	friend class Driver;

								WindowRef(Driver* driver, BMessenger target);

			Driver*				fDriver = nullptr;
			BMessenger			fTarget;
};


/*!	Drives an application in a test (specification 20.4): runs it on the
	headless platform on a thread of its own, feeds input the way the
	platform feeds real input (rule T2: hit testing, focus, event masks and
	mouse tracking take part), waits until it is idle (rule T3) and finds
	its windows and views.

	\code
	TestEnvironment environment;
	MyApplication app;
	Driver driver(app);
	WindowRef window = driver.Window("Settings");
	ASSERT_TRUE(driver.Click(window.Find<BButton>("ok")));
	ASSERT_TRUE(driver.WaitForIdle());
	\endcode

	The input methods return at once; WaitForIdle() waits for what they
	cause. Use from one thread.
*/
class Driver {
public:
	/*!	The longest WaitForIdle() waits by default. */
	static constexpr std::chrono::milliseconds kDefaultTimeout{5000};

	explicit					Driver(BApplication& app);
									/*!< Runs \a app on the headless platform (choosing it
										if \a app has no platform yet) on a thread of its
										own. InitCheck() tells whether that worked. */
								~Driver();
									/*!< Quits the application and waits for its thread. */

								Driver(const Driver&) = delete;
			Driver&				operator=(const Driver&) = delete;

			std::expected<void, Error> InitCheck() const;
									/*!< InvalidArgument if the application has another
										platform. */
			platform::HeadlessPlatform& Platform() const;
									/*!< The headless platform. */

			std::expected<void, Error> WaitForIdle(
									std::chrono::milliseconds timeout = kDefaultTimeout);
									/*!< Waits until the application is idle: no looper
										has a message queued or in hand, the platform has
										no work, and no timer of Clock::Default() is due
										(layout and drawing go as messages). TimedOut with
										a report of what is busy otherwise. */
			std::expected<void, Error> Advance(Clock::Time duration);
									/*!< Moves the TestEnvironment's clock forward (its
										timers run) and waits until idle; InvalidArgument
										without a ManualClock. */

			WindowRef			Window(std::string_view title);
									/*!< The first window with \a title, after waiting
										until idle; invalid if there is none. */
			std::vector<WindowRef> Windows();
									/*!< The windows (popups too), after waiting until
										idle. */

			std::expected<void, Error> Click(const ViewRef& view,
									std::int32_t buttons = 1, std::int32_t clicks = 1);
									/*!< Moves the mouse to the middle of \a view, presses
										and releases \a buttons, as a click of the user;
										its window is activated first, as a window system
										does. NotFound if the view is gone. */
			std::expected<void, Error> ClickAt(const WindowRef& window, gfx::Point where,
									std::int32_t buttons = 1, std::int32_t clicks = 1);
									/*!< A click at \a where in \a window (a popup's own
										coordinates for a popup). */
			std::expected<void, Error> Drag(const WindowRef& window, gfx::Point from,
									gfx::Point to, std::int32_t steps = 4);
									/*!< Presses at \a from, moves in \a steps to \a to,
										releases. */
			std::expected<void, Error> Scroll(const ViewRef& view, float dx, float dy);
									/*!< Turns the mouse wheel over \a view (positive \a dy
										scrolls down, as Haiku counts). */
			std::expected<void, Error> Type(const ViewRef& view, std::string_view text);
									/*!< Clicks into \a view, then types \a text. */
			std::expected<void, Error> Type(std::string_view text);
									/*!< Types \a text into the active window, one key per
										character (UTF-8). */
			std::expected<void, Error> Key(char key, BModifierFlags modifiers = {});
									/*!< Presses and releases a key of InterfaceDefs.h's
										keys (keys::kTab, keys::kEnter, an arrow, ...) or a
										character, with \a modifiers, in the active
										window. */

private:
	friend class WindowRef;

			platform::HeadlessWindow* _PlatformWindow(const BMessenger& target) const;
			platform::HeadlessWindow* _KeyWindow() const;
			std::expected<void, Error> _SendKey(platform::HeadlessWindow* window,
									std::string_view bytes, BModifierFlags modifiers);
			std::expected<void, Error> _Mouse(platform::HeadlessWindow* window,
									std::uint32_t what, gfx::Point where, std::int32_t buttons,
									std::int32_t clicks);
			std::string			_Report() const;

			BApplication&		fApp;
			platform::HeadlessPlatform* fPlatform = nullptr;
			std::thread			fRunner;
			std::optional<Error> fError;
};


// #pragma mark - templates


template<typename T, typename Work>
auto
ViewRef::With(Work work) const -> std::optional<decltype(work(std::declval<T&>()))>
{
	using Result = decltype(work(std::declval<T&>()));
	std::optional<Result> result;
	(void)Run([&](BView& view) {
		if (T* typed = dynamic_cast<T*>(&view))
			result = work(*typed);
	});
	return result;
}


template<typename T>
ViewRef
WindowRef::Find(std::string_view name) const
{
	const std::string wanted(name);
	return FindIf([&](BView& view) {
		if constexpr (std::is_same_v<T, BView>)
			return view.Name() == wanted;
		else
			return view.Name() == wanted && dynamic_cast<T*>(&view) != nullptr;
	});
}


template<typename T>
std::vector<ViewRef>
WindowRef::FindAll() const
{
	return FindAllIf([](BView& view) {
		if constexpr (std::is_same_v<T, BView>)
			return true;
		else
			return dynamic_cast<T*>(&view) != nullptr;
	});
}


} // namespace ngui::test
