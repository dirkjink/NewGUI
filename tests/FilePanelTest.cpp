/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/FilePanel.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/Messenger.h>
#include <ngui/Window.h>

#include "support/FakePlatform.h"


namespace {


using namespace ngui;
using namespace std::chrono_literals;
using test::FakePlatform;


constexpr std::uint32_t kShowPanel = FourCharCode("SHOW");


// The messages a looper received, shared with the test.
struct Inbox {
	std::mutex lock;
	std::condition_variable changed;
	std::vector<BMessage> messages;

	// The messages, once there are \a count (or after 5 s).
	std::vector<BMessage> WaitFor(std::size_t count)
	{
		std::unique_lock guard(lock);
		changed.wait_for(guard, 5s, [&] { return messages.size() >= count; });
		return messages;
	}
};


// A looper that keeps what it receives in an Inbox.
class InboxLooper : public BLooper {
public:
	explicit InboxLooper(std::shared_ptr<Inbox> inbox)
		:
		BLooper("inbox"),
		fInbox(std::move(inbox))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		{
			std::scoped_lock guard(fInbox->lock);
			fInbox->messages.push_back(*message);
		}
		fInbox->changed.notify_all();
	}

private:
	std::shared_ptr<Inbox> fInbox;
};


// A window that shows the panel it is given when it gets kShowPanel, on
// its own thread.
class PanelWindow : public BWindow {
public:
	explicit PanelWindow(BFilePanel* panel)
		:
		BWindow({0, 0, 100, 100}, "panel"),
		fPanel(panel)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kShowPanel) {
			(void)fPanel->Show();
			(void)message->SendReply(BMessage(kShowPanel));
			return;
		}
		BWindow::MessageReceived(message);
	}

private:
	BFilePanel* fPanel;
};


// A running application on the fake platform, with an inbox looper.
class RunningPanels {
public:
	RunningPanels()
		:
		fApp("application/x-vnd.ngui-file-panel-test"),
		fInbox(std::make_shared<Inbox>())
	{
		fApp.SetPulseRate(Clock::Time::zero());
		auto fake = std::make_unique<FakePlatform>();
		fPlatform = fake.get();
		EXPECT_TRUE(fApp.UsePlatform(std::move(fake)).has_value());
		fRunner = std::thread([this] { (void)fApp.Run(); });
		const auto deadline = std::chrono::steady_clock::now() + 5s;
		while (platform::Current() == nullptr && std::chrono::steady_clock::now() < deadline)
			std::this_thread::yield();
		fTarget = *BLooper::Run(std::make_unique<InboxLooper>(fInbox));
	}

	~RunningPanels()
	{
		(void)fTarget.SendMessage(kQuitRequested);
		(void)fTarget.WaitForLooperQuit(5s);
		(void)AppMessenger().SendMessage(kQuitRequested);
		fRunner.join();
	}

	FakePlatform& Platform()
	{
		return *fPlatform;
	}

	BMessenger Target() const
	{
		return fTarget;
	}

	Inbox& Received()
	{
		return *fInbox;
	}

private:
	BApplication fApp;
	FakePlatform* fPlatform = nullptr;
	std::thread fRunner;
	std::shared_ptr<Inbox> fInbox;
	BMessenger fTarget;
};


TEST(FilePanelTest, AnOpenPanelSendsThePathsThenCancel)
{
	// What is tested: An open panel for several files, starting in /home,
	// with a title, button labels and a filter, shown twice while its
	// dialog shows; the dialog answered with two paths.
	// Success criteria: One system dialog: Open, many, at /home, with the
	// title, labels and filter. While it shows, IsShowing() is true and a
	// second Show() opens no second dialog. The target gets
	// kRefsReceived with both paths in "paths" (UTF-8 kept), then kCancel
	// with "old_what" kRefsReceived and "source" the panel, as Haiku's panel
	// does when it closes; then IsShowing() is false.
	// Why it matters: Applications open documents through BFilePanel, as in
	// Haiku, now with the system's dialog.
	//
	// Given
	RunningPanels app;
	BFilePanel panel(BFilePanelMode::Open, app.Target(), "/home");
	panel.SetTitle("Open a picture");
	panel.SetButtonLabel(BFilePanelButton::Default, "Load");
	panel.SetButtonLabel(BFilePanelButton::Cancel, "Never mind");
	panel.AddFilter("Pictures", "png;jpg");

	// When
	ASSERT_TRUE(panel.Show().has_value());
	const bool showing = panel.IsShowing();
	ASSERT_TRUE(panel.Show().has_value());
	const std::vector<platform::FileDialogSpec> dialogs = app.Platform().Dialogs();
	ASSERT_TRUE(app.Platform().AnswerDialog(0,
		std::vector<std::string>{"/home/a.png", "/home/b ä.jpg"}));
	const std::vector<BMessage> received = app.Received().WaitFor(2);

	// Then
	EXPECT_TRUE(showing);
	EXPECT_FALSE(panel.IsShowing());
	ASSERT_EQ(dialogs.size(), 1u);
	EXPECT_EQ(dialogs[0].kind, platform::FileDialogSpec::Kind::Open);
	EXPECT_TRUE(dialogs[0].many);
	EXPECT_EQ(dialogs[0].location, "/home");
	EXPECT_EQ(dialogs[0].title, "Open a picture");
	EXPECT_EQ(dialogs[0].acceptLabel, "Load");
	EXPECT_EQ(dialogs[0].cancelLabel, "Never mind");
	ASSERT_EQ(dialogs[0].filters.size(), 1u);
	EXPECT_EQ(dialogs[0].filters[0].second, "png;jpg");
	EXPECT_EQ(dialogs[0].parent, nullptr);
	ASSERT_EQ(received.size(), 2u);
	EXPECT_EQ(received[0].what, kRefsReceived);
	EXPECT_EQ(received[0].GetString("paths", ""), "/home/a.png");
	EXPECT_EQ(received[0].FindString("paths", 1).value_or(""), "/home/b ä.jpg");
	EXPECT_EQ(received[1].what, kCancel);
	EXPECT_EQ(received[1].GetInt32("old_what", 0), static_cast<std::int32_t>(kRefsReceived));
	EXPECT_EQ(received[1].GetPointer("source", nullptr), &panel);
}


TEST(FilePanelTest, ASavePanelSendsFolderAndName)
{
	// What is tested: A save panel with its own message (what 'SVME'),
	// starting in /tmp/x and suggesting "out.txt": answered with a path;
	// shown again and cancelled; shown again and failing.
	// Success criteria: The dialog is Save, not many, at /tmp/x/out.txt.
	// The choice comes in the panel's message with "directory" /tmp/x/sub
	// and "name" out.txt, then kCancel with "old_what" 'SVME'. A cancelled
	// dialog sends only kCancel; a failing one kCancel with "error".
	// Why it matters: Saving goes through B_SAVE_REQUESTED's fields, as in
	// Haiku; a program learns that the panel closed either way.
	//
	// Given
	RunningPanels app;
	constexpr std::uint32_t kSaveMe = FourCharCode("SVME");
	BFilePanel panel(BFilePanelMode::Save, app.Target(), "/tmp/x", {}, true,
		std::make_unique<BMessage>(kSaveMe));
	panel.SetSaveText("out.txt");

	// When
	ASSERT_TRUE(panel.Show().has_value());
	ASSERT_TRUE(app.Platform().AnswerDialog(0, std::vector<std::string>{"/tmp/x/sub/out.txt"}));
	(void)app.Received().WaitFor(2);
	ASSERT_TRUE(panel.Show().has_value());
	ASSERT_TRUE(app.Platform().AnswerDialog(1, std::vector<std::string>{}));
	(void)app.Received().WaitFor(3);
	ASSERT_TRUE(panel.Show().has_value());
	ASSERT_TRUE(app.Platform().AnswerDialog(2, Failure(ErrorCode::IoError, "no portal")));
	const std::vector<BMessage> received = app.Received().WaitFor(4);
	const std::vector<platform::FileDialogSpec> dialogs = app.Platform().Dialogs();

	// Then
	ASSERT_EQ(dialogs.size(), 3u);
	EXPECT_EQ(dialogs[0].kind, platform::FileDialogSpec::Kind::Save);
	EXPECT_FALSE(dialogs[0].many);
	EXPECT_EQ(dialogs[0].location, "/tmp/x/out.txt");
	ASSERT_EQ(received.size(), 4u);
	EXPECT_EQ(received[0].what, kSaveMe);
	EXPECT_EQ(received[0].GetString("directory", ""), "/tmp/x/sub");
	EXPECT_EQ(received[0].GetString("name", ""), "out.txt");
	EXPECT_EQ(received[1].what, kCancel);
	EXPECT_EQ(received[1].GetInt32("old_what", 0), static_cast<std::int32_t>(kSaveMe));
	EXPECT_EQ(received[2].what, kCancel);
	EXPECT_EQ(received[3].what, kCancel);
	EXPECT_EQ(received[3].GetString("error", ""), "no portal");
}


TEST(FilePanelTest, FoldersHidingModalityAndNoPlatform)
{
	// What is tested: An open panel for directories only; a panel hidden
	// while its dialog shows, then answered; a panel destroyed while its
	// dialog shows, then answered; a modal panel shown from a window's
	// thread; a panel shown without a platform.
	// Success criteria: Directories alone make a folder dialog. A hidden
	// panel's answer is dropped (nothing arrives). A destroyed panel's
	// answer still reaches the target. The modal panel's dialog is modal to
	// the window that showed it. Without a platform Show() is NotSupported.
	// Why it matters: Panels live in windows and may go away before the
	// user answers; the system's dialog must not outlive them unsafely.
	//
	// Given
	auto app = std::make_unique<RunningPanels>();
	BFilePanel folders(BFilePanelMode::Open, app->Target(), {}, BNodeFlavor::Directory);
	BFilePanel hidden(BFilePanelMode::Open, app->Target());
	auto destroyed = std::make_unique<BFilePanel>(BFilePanelMode::Open, app->Target());
	BFilePanel modal(BFilePanelMode::Open, app->Target(), {}, {}, true, nullptr, true);
	const BMessenger window = *BLooper::Run(std::make_unique<PanelWindow>(&modal));

	// When
	ASSERT_TRUE(folders.Show().has_value());
	ASSERT_TRUE(hidden.Show().has_value());
	hidden.Hide();
	ASSERT_TRUE(app->Platform().AnswerDialog(1, std::vector<std::string>{"/hidden"}));
	ASSERT_TRUE(destroyed->Show().has_value());
	destroyed.reset();
	ASSERT_TRUE(app->Platform().AnswerDialog(2, std::vector<std::string>{"/kept"}));
	const std::vector<BMessage> received = app->Received().WaitFor(2);
	BMessage reply;
	ASSERT_TRUE(window.SendMessage(BMessage(kShowPanel), &reply).has_value());
	const std::vector<platform::FileDialogSpec> dialogs = app->Platform().Dialogs();
	const std::vector<test::FakeWindow*> windows = app->Platform().Windows();
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);
	app.reset();
	BFilePanel orphan;
	const auto withoutPlatform = orphan.Show();

	// Then
	ASSERT_EQ(dialogs.size(), 4u);
	EXPECT_EQ(dialogs[0].kind, platform::FileDialogSpec::Kind::Folder);
	ASSERT_EQ(received.size(), 2u);
	EXPECT_EQ(received[0].GetString("paths", ""), "/kept");
	EXPECT_EQ(received[1].what, kCancel);
	ASSERT_FALSE(windows.empty());
	EXPECT_EQ(dialogs[3].parent, windows.back());
	ASSERT_FALSE(withoutPlatform.has_value());
	EXPECT_EQ(withoutPlatform.error().code, ErrorCode::NotSupported);
}


} // namespace
