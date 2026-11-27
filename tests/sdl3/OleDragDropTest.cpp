/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// Drag & drop with other programs on Windows (module P4): the OLE objects
// of the SDL platform, driven directly (a real drag needs a user's mouse).


#include "platform/sdl3/OleDragDrop.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
// windows.h's macro would turn BMessenger::SendMessage() into SendMessageA()
#ifdef SendMessage
#undef SendMessage
#endif

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Looper.h>
#include <ngui/Message.h>


namespace {


using namespace ngui;
using namespace ngui::platform;
using namespace ngui::platform::sdl3;
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


// OLE on the test's thread while it lives.
class ScopedOle {
public:
	ScopedOle()
		:
		fInitialized(SUCCEEDED(OleInitialize(nullptr)))
	{
	}

	~ScopedOle()
	{
		if (fInitialized)
			OleUninitialize();
	}

private:
	bool fInitialized;
};


// A looper that keeps the messages it gets.
class Collector : public BLooper {
public:
	struct Received {
		std::mutex				lock;
		std::condition_variable	changed;
		std::vector<BMessage>	messages;
	};

	explicit Collector(std::shared_ptr<Received> received)
		:
		BLooper("collector"),
		fReceived(std::move(received))
	{
	}

	void MessageReceived(BMessage* message) override
	{
		{
			std::scoped_lock lock(fReceived->lock);
			fReceived->messages.push_back(*message);
		}
		fReceived->changed.notify_all();
	}

private:
	std::shared_ptr<Received> fReceived;
};


TEST(OleDragDropTest, DragsOfferTheirDataInWindowsFormats)
{
	// What is tested: The data object of a drag (MakeDataObject()) with
	// text/plain "héllo", a text/uri-list of the file C:\x y.txt and a PNG;
	// its formats asked for as other programs ask; and read back by MIME
	// type (ItemsOf()).
	// Success criteria: CF_UNICODETEXT is L"héllo", CF_HDROP names
	// "C:\x y.txt", the registered formats "image/png" and "PNG" give the
	// PNG's bytes; read back, the items are text/plain "héllo", the
	// text/uri-list with file:///C:/x%20y.txt and image/png.
	// Why it matters: Other programs on Windows read text, files and images
	// in their clipboard formats (module P4); what NewGUI drags must arrive
	// in the formats they know, and what they drag in must come back as
	// MIME data.
	//
	// Given
	const ScopedOle ole;
	const std::vector<std::byte> png = {std::byte{0x89}, std::byte{'P'}, std::byte{'N'},
		std::byte{'G'}};
	auto* data = static_cast<IDataObject*>(MakeDataObject({{"text/plain",
		Bytes("h\xC3\xA9llo")}, {"text/uri-list", Bytes("file:///C:/x%20y.txt\r\n")},
		{"image/png", png}}));
	const auto read = [data](CLIPFORMAT format) -> std::vector<std::byte> {
		FORMATETC wanted{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		STGMEDIUM medium{};
		if (data->GetData(&wanted, &medium) != S_OK)
			return {};
		const auto* bytes = static_cast<const std::byte*>(GlobalLock(medium.hGlobal));
		std::vector<std::byte> copy(bytes, bytes + GlobalSize(medium.hGlobal));
		GlobalUnlock(medium.hGlobal);
		ReleaseStgMedium(&medium);
		return copy;
	};

	// When
	const std::vector<std::byte> unicode = read(CF_UNICODETEXT);
	std::wstring file;
	{
		FORMATETC wanted{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
		STGMEDIUM medium{};
		if (data->GetData(&wanted, &medium) == S_OK) {
			auto* drop = static_cast<HDROP>(medium.hGlobal);
			file.resize(DragQueryFileW(drop, 0, nullptr, 0));
			(void)DragQueryFileW(drop, 0, file.data(), static_cast<UINT>(file.size() + 1));
			ReleaseStgMedium(&medium);
		}
	}
	const std::vector<std::byte> mime = read(static_cast<CLIPFORMAT>(
		RegisterClipboardFormatW(L"image/png")));
	const std::vector<std::byte> named = read(static_cast<CLIPFORMAT>(
		RegisterClipboardFormatW(L"PNG")));
	const std::vector<DragItem> items = ItemsOf(data);
	data->Release();

	// Then
	ASSERT_GE(unicode.size(), 12u);
	EXPECT_EQ(std::wstring(reinterpret_cast<const wchar_t*>(unicode.data())), L"h\u00e9llo");
	EXPECT_EQ(file, L"C:\\x y.txt");
	EXPECT_EQ(mime, png);
	EXPECT_EQ(named, png);
	ASSERT_EQ(items.size(), 3u);
	EXPECT_EQ(items[0].type, "text/plain");
	EXPECT_EQ(Text(items[0].data), "h\xC3\xA9llo");
	EXPECT_EQ(items[1].type, "text/uri-list");
	EXPECT_EQ(Text(items[1].data), "file:///C:/x%20y.txt\r\n");
	EXPECT_EQ(items[2].type, "image/png");
	EXPECT_EQ(items[2].data, png);
}


TEST(OleDragDropTest, TheDropTargetReportsDropsWithTheirData)
{
	// What is tested: The drop target of a window (MakeDropTarget()) with a
	// mapping that moves screen points by (-100, -50): a data object with
	// text and a PNG entering at (110, 60), moving to (130, 70) and dropped
	// there; then one that enters and leaves.
	// Success criteria: The window gets kDropBegin, kDropPosition at
	// (10, 10), kDropPosition at (30, 20), and at the drop kDropPosition,
	// kDropData with text/plain and image/png, kDropComplete, all at
	// (30, 20); the effect is a copy. The drag that leaves ends with
	// kDropComplete and no data.
	// Why it matters: Windows hands drops to the window's IDropTarget; the
	// window's views must get the drag's position and all its data by MIME
	// type, as from SDL on the other systems (module P4).
	//
	// Given
	const ScopedOle ole;
	auto received = std::make_shared<Collector::Received>();
	const BMessenger window = *BLooper::Run(std::make_unique<Collector>(received));
	auto* target = static_cast<IDropTarget*>(MakeDropTarget(window, [](long x, long y) {
		return gfx::Point{static_cast<float>(x - 100), static_cast<float>(y - 50)};
	}));
	auto* data = static_cast<IDataObject*>(MakeDataObject({{"text/plain", Bytes("hi")},
		{"image/png", Bytes("PNG!")}}));
	DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;

	// When
	(void)target->DragEnter(data, MK_LBUTTON, POINTL{110, 60}, &effect);
	(void)target->DragOver(MK_LBUTTON, POINTL{130, 70}, &effect);
	(void)target->Drop(data, 0, POINTL{130, 70}, &effect);
	const DWORD dropEffect = effect;
	(void)target->DragEnter(data, MK_LBUTTON, POINTL{110, 60}, &effect);
	(void)target->DragLeave();
	target->Release();
	data->Release();
	std::vector<BMessage> messages;
	{
		std::unique_lock lock(received->lock);
		(void)received->changed.wait_for(lock, 5s, [&] {
			return received->messages.size() >= 9;
		});
		messages = received->messages;
	}
	(void)window.SendMessage(kQuitRequested);
	(void)window.WaitForLooperQuit(5s);

	// Then
	ASSERT_EQ(messages.size(), 9u);
	EXPECT_EQ(messages[0].what, kDropBegin);
	EXPECT_EQ(messages[1].what, kDropPosition);
	EXPECT_EQ(messages[1].GetPoint("where", {}), (gfx::Point{10, 10}));
	EXPECT_EQ(messages[2].what, kDropPosition);
	EXPECT_EQ(messages[2].GetPoint("where", {}), (gfx::Point{30, 20}));
	EXPECT_EQ(messages[3].what, kDropPosition);
	EXPECT_EQ(messages[4].what, kDropData);
	EXPECT_EQ(messages[4].GetPoint("where", {}), (gfx::Point{30, 20}));
	const auto text = messages[4].FindData("text/plain", BTypeCode::Mime);
	ASSERT_TRUE(text.has_value());
	EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(text->data()), text->size()),
		"hi");
	EXPECT_TRUE(messages[4].HasData("image/png", BTypeCode::Mime));
	EXPECT_EQ(messages[5].what, kDropComplete);
	EXPECT_EQ(dropEffect, static_cast<DWORD>(DROPEFFECT_COPY));
	EXPECT_EQ(messages[6].what, kDropBegin);
	EXPECT_EQ(messages[7].what, kDropPosition);
	EXPECT_EQ(messages[8].what, kDropComplete);
	EXPECT_FALSE(messages[8].HasData("text/plain"));
}


} // namespace
