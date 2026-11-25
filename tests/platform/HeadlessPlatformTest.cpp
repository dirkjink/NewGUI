/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/platform/HeadlessPlatform.h>

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Clipboard.h>
#include <ngui/Cursor.h>
#include <ngui/FilePanel.h>
#include <ngui/Looper.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/platform/PlatformRegistry.h>

#include "support/PixelBackend.h"
#include "support/RecordingLooper.h"


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace std::chrono_literals;
using test::LooperRecord;
using test::PixelBackend;


// Sets an environment variable for the life of the object.
class ScopedEnvironment {
public:
	ScopedEnvironment(const char* name, const char* value)
		:
		fName(name)
	{
#ifdef _WIN32
		(void)_putenv_s(name, value);
#else
		(void)setenv(name, value, 1);
#endif
	}

	~ScopedEnvironment()
	{
#ifdef _WIN32
		(void)_putenv_s(fName.c_str(), "");
#else
		(void)unsetenv(fName.c_str());
#endif
	}

private:
	std::string fName;
};


// A view that records clicks, keys and activations.
class RecordView : public BView {
public:
	RecordView(gfx::Rect frame, std::shared_ptr<LooperRecord> record)
		:
		BView(frame, "view", kFollowLeftTop, BViewFlag::WillDraw),
		fRecord(std::move(record))
	{
	}

	void MouseDown(gfx::Point where) override
	{
		fRecord->Add("down " + std::to_string(static_cast<int>(where.x)) + ","
			+ std::to_string(static_cast<int>(where.y)));
	}

	void WindowActivated(bool active) override
	{
		fRecord->Add(active ? "active" : "inactive");
	}

private:
	std::shared_ptr<LooperRecord> fRecord;
};


// The messages a looper gets, for the file panel.
class InboxLooper : public BLooper {
public:
	explicit InboxLooper(std::shared_ptr<LooperRecord> record)
		:
		BLooper("inbox"),
		fRecord(std::move(record))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		fRecord->Add(std::to_string(message->what) + " "
			+ std::string(message->GetString("paths", "")));
	}

private:
	std::shared_ptr<LooperRecord> fRecord;
};


// An application running on a headless platform of \a options.
class HeadlessApp {
public:
	explicit HeadlessApp(const HeadlessPlatform::Options& options = {})
		:
		fApp("application/x-vnd.ngui-headless-test")
	{
		fApp.SetPulseRate(Clock::Time::zero());
		auto platform = std::make_unique<HeadlessPlatform>(options);
		fPlatform = platform.get();
		EXPECT_TRUE(fApp.UsePlatform(std::move(platform)).has_value());
		fRunner = std::thread([this] { (void)fApp.Run(); });
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (platform::Current() != fPlatform && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
	}

	~HeadlessApp()
	{
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
	}

	HeadlessPlatform& Platform()
	{
		return *fPlatform;
	}

private:
	BApplication fApp;
	HeadlessPlatform* fPlatform = nullptr;
	std::thread fRunner;
};


// Waits until \a done holds; false after 5 s.
template<typename Done>
bool
WaitUntil(Done done)
{
	const auto deadline = std::chrono::steady_clock::now() + 5s;
	while (!done()) {
		if (std::chrono::steady_clock::now() > deadline)
			return false;
		std::this_thread::sleep_for(1ms);
	}
	return true;
}


TEST(HeadlessPlatformTest, PlatformsAreChosenByName)
{
	// What is tested: The default registry; a registry with a platform
	// that cannot start (priority 10), one that can (5) and one by name
	// only (-1): CreatePreferred() without a name, with a name, and with
	// NGUI_PLATFORM set; registering a name twice or an empty one.
	// Success criteria: The default registry has "headless", which
	// creates a platform of that name; an unknown name is NotFound.
	// CreatePreferred() skips the one that cannot start and never takes the
	// one by name only on its own; a name or NGUI_PLATFORM chooses it.
	// Taken and empty names are refused.
	// Why it matters: Programs choose their window system by name, tests
	// run headless (specification 20.2) — but a program without a display
	// must not run unseen.
	//
	// Given
	PlatformRegistry registry;
	const auto make = [](bool works) {
		return [works]() -> std::expected<std::unique_ptr<Platform>, Error> {
			if (!works)
				return Failure(ErrorCode::NotSupported, "no display");
			return std::make_unique<HeadlessPlatform>();
		};
	};
	ASSERT_TRUE(registry.Register("broken", 10, make(false)));
	ASSERT_TRUE(registry.Register("works", 5, make(true)));
	ASSERT_TRUE(registry.Register("hidden", -1, make(true)));

	// When
	const auto headless = PlatformRegistry::Default().Create("headless");
	const auto unknown = PlatformRegistry::Default().Create("nonexistent");
	const auto preferred = registry.CreatePreferred();
	const auto named = registry.CreatePreferred("hidden");
	std::vector<std::string> names = registry.Names();
	bool fromEnvironment = false;
	{
		const ScopedEnvironment environment("NGUI_PLATFORM", "hidden");
		fromEnvironment = registry.CreatePreferred().has_value();
	}
	const auto afterwards = registry.CreatePreferred();

	// Then
	ASSERT_TRUE(headless.has_value());
	EXPECT_EQ((*headless)->Name(), "headless");
	ASSERT_FALSE(unknown.has_value());
	EXPECT_EQ(unknown.error().code, ErrorCode::NotFound);
	EXPECT_TRUE(preferred.has_value());
	EXPECT_TRUE(named.has_value());
	EXPECT_EQ(names, (std::vector<std::string>{"broken", "works", "hidden"}));
	EXPECT_TRUE(fromEnvironment);
	EXPECT_TRUE(afterwards.has_value());
	EXPECT_FALSE(registry.Register("works", 1, make(true)));
	EXPECT_FALSE(registry.Register("", 1, make(true)));
	PlatformRegistry onlyHidden;
	ASSERT_TRUE(onlyHidden.Register("hidden", -1, make(true)));
	ASSERT_FALSE(onlyHidden.CreatePreferred().has_value());
}


TEST(HeadlessPlatformTest, WindowsGetEventsAndKeepTheirFrames)
{
	// What is tested: A window of 120 × 80 with a view, on a headless
	// screen at scale 2, chosen by BApplication::UsePlatform("headless"):
	// its platform window, its first frame, a click sent through it, two
	// activations, and a close request.
	// Success criteria: The platform has one shown window with the title,
	// frame and scale; the frame shown is 240 × 160 device pixels; the
	// click reaches the view at (10, 20) in its coordinates; activating the
	// window tells its views; the close request closes the window, and the
	// platform's list is empty afterwards.
	// Why it matters: Applications and the toolkit run in CI without a
	// window system, events taking the way of real ones (specification
	// 20.2, rule T2).
	//
	// Given
	bool chosen = false;
	{
		// an application chooses it by its name
		BApplication byName("application/x-vnd.ngui-headless-name");
		chosen = byName.UsePlatform("headless").has_value()
			&& byName.Platform()->Name() == "headless";
	}
	HeadlessApp app({{800, 600}, 2});
	auto record = std::make_shared<LooperRecord>();
	auto owned = std::make_unique<BWindow>(gfx::Rect{30, 40, 150, 120}, "Headless",
		BWindowLook::Titled);
	ASSERT_TRUE(owned->UseBackend(std::make_unique<PixelBackend>()).has_value());
	owned->AddChild(std::make_unique<RecordView>(gfx::Rect{0, 0, 120, 80}, record));
	owned->Show();
	const BMessenger messenger = *BLooper::Run(std::move(owned));

	// When
	ASSERT_TRUE(WaitUntil([&] {
		const auto windows = app.Platform().Windows();
		return windows.size() == 1 && windows[0]->CountFrames() > 0;
	}));
	HeadlessWindow* platformWindow = app.Platform().Windows()[0];
	const std::string title = platformWindow->Title();
	const bool shown = platformWindow->IsShown();
	const gfx::Rect frame = platformWindow->Frame();
	const float scale = platformWindow->Scale();
	const auto shownFrame = platformWindow->LastFrame();
	BMessage click(kMouseDown);
	(void)click.AddPoint("where", {10, 20});
	(void)click.AddInt32("buttons", 1);
	(void)click.AddInt32("clicks", 1);
	ASSERT_TRUE(platformWindow->Send(click).has_value());
	platformWindow->Activate();
	const bool clicked = record->WaitForEntry("down 10,20", 1);
	const bool activated = record->WaitForEntry("active", 1);
	ASSERT_TRUE(platformWindow->RequestClose().has_value());
	ASSERT_TRUE(messenger.WaitForLooperQuit(5s).has_value());
	const bool gone = WaitUntil([&] { return app.Platform().Windows().empty(); });

	// Then
	EXPECT_TRUE(chosen);
	EXPECT_EQ(title, "Headless");
	EXPECT_TRUE(shown);
	EXPECT_EQ(frame, (gfx::Rect{30, 40, 150, 120}));
	EXPECT_EQ(scale, 2.0f);
	ASSERT_NE(shownFrame, nullptr);
	EXPECT_EQ(shownFrame->width, 240);
	EXPECT_EQ(shownFrame->height, 160);
	EXPECT_TRUE(clicked);
	EXPECT_TRUE(activated);
	EXPECT_TRUE(gone);
}


TEST(HeadlessPlatformTest, ClipboardDialogsCursorAndScreenAreVirtual)
{
	// What is tested: The system clipboard written by BClipboard and by the
	// platform; a BFilePanel shown without an answer and with one; the
	// application's cursor; the screen of the options.
	// Success criteria: What BClipboard commits is the platform's
	// clipboard, and what the platform holds BClipboard reads. The panel
	// without an answer only sends kCancel; with an answer of
	// "/tmp/a.txt" kRefsReceived with that path, then kCancel; the platform
	// kept both dialogs. The I-beam cursor is the platform's. The screen is
	// 800 × 600 at scale 2, primary.
	// Why it matters: Tests must not touch the user's clipboard or wait for
	// a dialog nobody answers (specification 20.2 and 20.10).
	//
	// Given
	HeadlessApp app({{800, 600}, 2});
	auto record = std::make_shared<LooperRecord>();
	const BMessenger inbox = *BLooper::Run(std::make_unique<InboxLooper>(record));
	BClipboard& clipboard = BClipboard::System();

	// When
	ASSERT_TRUE(clipboard.Lock());
	ASSERT_TRUE(clipboard.Clear().has_value());
	const std::string text = "copied";
	ASSERT_TRUE(clipboard.Data()->AddData("text/plain", BTypeCode::Mime,
		std::as_bytes(std::span(text.data(), text.size()))).has_value());
	ASSERT_TRUE(clipboard.Commit().has_value());
	clipboard.Unlock();
	const auto platformClipboard = app.Platform().Clipboard();
	const std::string pasted = "pasted";
	const auto bytes = std::as_bytes(std::span(pasted.data(), pasted.size()));
	ASSERT_TRUE(app.Platform().SetClipboard({{"text/plain",
		std::vector<std::byte>(bytes.begin(), bytes.end())}}).has_value());
	std::string read;
	if (clipboard.Lock()) {
		if (auto data = clipboard.Data()->FindData("text/plain", BTypeCode::Mime))
			read.assign(reinterpret_cast<const char*>(data->data()), data->size());
		clipboard.Unlock();
	}
	BFilePanel panel(BFilePanelMode::Open, inbox);
	ASSERT_TRUE(panel.Show().has_value());
	const bool cancelled = record->WaitFor(1);
	app.Platform().SetFileDialogAnswer([](const FileDialogSpec&) {
		return FileDialogResult(std::vector<std::string>{"/tmp/a.txt"});
	});
	ASSERT_TRUE(panel.Show().has_value());
	const bool answered = record->WaitFor(3);
	BApplication::SetCursor(BCursor(BCursorID::IBeam));
	const bool cursor = WaitUntil([&] {
		return app.Platform().Cursor().system == static_cast<std::uint8_t>(BCursorID::IBeam);
	});
	const std::vector<DisplayInfo> displays = app.Platform().Displays();
	(void)inbox.SendMessage(kQuitRequested);
	(void)inbox.WaitForLooperQuit(5s);

	// Then
	ASSERT_TRUE(platformClipboard.has_value());
	ASSERT_EQ(platformClipboard->size(), 1u);
	EXPECT_EQ(platformClipboard->front().first, "text/plain");
	EXPECT_EQ(platformClipboard->front().second.size(), text.size());
	EXPECT_EQ(read, "pasted");
	EXPECT_TRUE(cancelled);
	EXPECT_TRUE(answered);
	EXPECT_EQ(record->Received(), (std::vector<std::string>{std::to_string(kCancel) + " ",
		std::to_string(kRefsReceived) + " /tmp/a.txt", std::to_string(kCancel) + " "}));
	EXPECT_EQ(app.Platform().FileDialogs().size(), 2u);
	EXPECT_TRUE(cursor);
	ASSERT_EQ(displays.size(), 1u);
	EXPECT_EQ(displays[0].frame, (gfx::Rect{0, 0, 800, 600}));
	EXPECT_EQ(displays[0].scale, 2.0f);
	EXPECT_TRUE(displays[0].primary);
}


} // namespace
