/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drag & drop with other programs on macOS (module P4): the pasteboard data
// of a drag, and drops on SDL's Cocoa windows. A real drag needs a user's
// mouse; the drop is handed to the window as AppKit hands it. Cocoa wants
// the process's main thread, so the application runs on the test's own
// thread and the test steps on another one; without a window server the
// window test is skipped.


#include <ngui/platform/SdlPlatform.h>

#import <AppKit/AppKit.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Clock.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "platform/sdl3/CocoaDragDrop.h"
#include "support/ScopedEnvironment.h"


// What AppKit hands a window's performDragOperation: of a drag, as far as
// NewGUI asks: the pasteboard and the point.
@interface NGUITestDraggingInfo : NSObject
@property(strong) NSPasteboard* draggingPasteboard;
@property NSPoint draggingLocation;
- (NSDragOperation)draggingSourceOperationMask;
@end


@implementation NGUITestDraggingInfo
- (NSDragOperation)draggingSourceOperationMask
{
	return NSDragOperationCopy;
}
@end


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::test;
using namespace std::chrono_literals;


std::vector<std::byte>
Bytes(std::string_view text)
{
	const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
	return std::vector<std::byte>(bytes, bytes + text.size());
}


std::string
Text(const std::vector<std::byte>& bytes)
{
	return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}


TEST(SdlCocoaDragDropTest, DragsWriteThePasteboardAsOtherProgramsReadIt)
{
	// What is tested: The data of a drag (text/plain "héllo", the file
	// /tmp/a b.txt as a text/uri-list, a PNG, text/html) written to a
	// pasteboard of its own (WriteDragItems()), read as other programs read
	// it, and read back by MIME type (ReadDragItems()).
	// Success criteria: The pasteboard has the string "héllo", the file
	// URL of /tmp/a b.txt, the PNG's bytes as public.png and the HTML; read
	// back, the items begin with text/uri-list (file:///tmp/a%20b.txt) and
	// text/plain and have image/png and text/html.
	// Why it matters: Programs on macOS read drags from NSPasteboard in its
	// types (module P4); what NewGUI drags must be there in those types, and
	// what they drag in must come back as MIME data.
	//
	// Given
	NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
	const std::vector<std::byte> png = {std::byte{0x89}, std::byte{'P'}, std::byte{'N'},
		std::byte{'G'}};

	// When
	sdl3::WriteDragItems({{"text/plain", Bytes("h\xC3\xA9llo")}, {"text/uri-list",
		Bytes("file:///tmp/a%20b.txt\r\n")}, {"image/png", png}, {"text/html",
		Bytes("<b>x</b>")}}, (__bridge void*)board);
	NSString* string = [board stringForType:NSPasteboardTypeString];
	NSArray<NSURL*>* files = [board readObjectsForClasses:@[NSURL.class]
		options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
	NSData* image = [board dataForType:NSPasteboardTypePNG];
	NSString* html = [board stringForType:NSPasteboardTypeHTML];
	const std::vector<DragItem> items = sdl3::ReadDragItems((__bridge void*)board);
	[board releaseGlobally];

	// Then
	EXPECT_TRUE([string isEqualToString:@"héllo"]);
	ASSERT_EQ(files.count, 1u);
	EXPECT_TRUE([files[0].path isEqualToString:@"/tmp/a b.txt"]);
	ASSERT_NE(image, nil);
	EXPECT_EQ(image.length, png.size());
	EXPECT_TRUE([html isEqualToString:@"<b>x</b>"]);
	const auto find = [&items](const char* type) -> const DragItem* {
		for (const DragItem& item : items) {
			if (item.type == type)
				return &item;
		}
		return nullptr;
	};
	ASSERT_GE(items.size(), 4u);
	EXPECT_EQ(items[0].type, "text/uri-list");
	EXPECT_EQ(Text(items[0].data), "file:///tmp/a%20b.txt\r\n");
	EXPECT_EQ(items[1].type, "text/plain");
	EXPECT_EQ(Text(items[1].data), "h\xC3\xA9llo");
	ASSERT_NE(find("image/png"), nullptr);
	EXPECT_EQ(find("image/png")->data, png);
	ASSERT_NE(find("text/html"), nullptr);
	EXPECT_EQ(Text(find("text/html")->data), "<b>x</b>");
}


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


TEST(SdlCocoaDragDropTest, AWindowTakesDropsWithAllTheirData)
{
	// What is tested: An application on the SDL platform with SDL's Cocoa
	// driver and a window "Drops" with a view; a drop of a file URL
	// (/tmp/a b.txt), a string and a PNG handed to its NSWindow
	// (performDragOperation:) at (50, 40) of its content, as AppKit hands it.
	// Success criteria: performDragOperation: takes it (YES); the view gets
	// kSimpleData with the path "/tmp/a b.txt", text/plain "hi" and the PNG
	// as image/png, WasDropped(). Skipped without a window server.
	// Why it matters: SDL takes only files and text on macOS; NewGUI's
	// windows must get images and other data from other programs too, after
	// the positions SDL reported during the drag (module P4).
	//
	// Given
	const ScopedEnvironment driver("SDL_VIDEODRIVER", "cocoa");
	auto platform = SdlPlatform::Create();
	if (!platform.has_value())
		GTEST_SKIP() << "no Cocoa video: " << platform.error().message;
	SdlPlatform* raw = platform->get();
	BApplication app("application/x-vnd.ngui-cocoa-drop-test");
	app.SetPulseRate(Clock::Time::zero());
	(void)app.UsePlatform(std::move(*platform));
	std::mutex lock;
	BMessage dropped;
	std::atomic<bool> done = false;
	bool taken = false;
	bool found = false;

	// When
	std::thread steps([&] {
		const auto deadline = std::chrono::steady_clock::now() + 10s;
		while (!AppMessenger().IsValid() && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		auto owned = std::make_unique<DropWindow>(lock, dropped, done);
		owned->Show();
		(void)BLooper::Run(std::move(owned));
		const auto onMain = [raw](std::function<void()> work) {
			std::promise<void> finished;
			raw->RunOnMainThread([&work, &finished] {
				work();
				finished.set_value();
			});
			finished.get_future().wait();
		};
		while (!found && std::chrono::steady_clock::now() < deadline) {
			onMain([&] {
				for (NSWindow* window in [NSApp windows]) {
					if (![window.title isEqualToString:@"Drops"])
						continue;
					found = true;
					NSPasteboard* board = [NSPasteboard pasteboardWithUniqueName];
					[board clearContents];
					const unsigned char png[] = {0x89, 'P', 'N', 'G'};
					NSPasteboardItem* item = [[NSPasteboardItem alloc] init];
					[item setString:@"hi" forType:NSPasteboardTypeString];
					[item setData:[NSData dataWithBytes:png length:4]
						forType:NSPasteboardTypePNG];
					[board writeObjects:@[[NSURL fileURLWithPath:@"/tmp/a b.txt"], item]];
					NGUITestDraggingInfo* info = [[NGUITestDraggingInfo alloc] init];
					info.draggingPasteboard = board;
					info.draggingLocation = NSMakePoint(50,
						window.contentView.frame.size.height - 40);
					// SDL's window class is the drag destination, not NSWindow
					taken = [(id<NSDraggingDestination>)window
						performDragOperation:(id<NSDraggingInfo>)info];
					[board releaseGlobally];
				}
			});
			if (!found)
				std::this_thread::sleep_for(50ms);
		}
		while (found && !done && std::chrono::steady_clock::now() < deadline)
			std::this_thread::sleep_for(10ms);
		(void)AppMessenger().SendMessage(kQuitRequested);
	});
	(void)app.Run();
	steps.join();

	// Then
	ASSERT_TRUE(found);
	EXPECT_TRUE(taken);
	ASSERT_TRUE(done);
	std::scoped_lock guard(lock);
	EXPECT_EQ(dropped.what, kSimpleData);
	EXPECT_TRUE(dropped.WasDropped());
	EXPECT_EQ(dropped.GetString("paths", ""), "/tmp/a b.txt");
	const auto text = dropped.FindData("text/plain", BTypeCode::Mime);
	ASSERT_TRUE(text.has_value());
	EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()),
		"hi");
	EXPECT_TRUE(dropped.HasData("image/png", BTypeCode::Mime));
}


} // namespace
