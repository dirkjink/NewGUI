/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <chrono>
#include <memory>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Bitmap.h>
#include <ngui/Looper.h>
#include <ngui/test/Driver.h>
#include <ngui/test/Image.h>
#include <ngui/test/ImageCompare.h>
#include <ngui/test/Render.h>

#include "support/FixedTextSystem.h"
#include "support/PixelBackend.h"


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A window of 120 × 80 with a view, on the pixel backend.
BMessenger
OpenWindow()
{
	auto window = std::make_unique<BWindow>(gfx::Rect{50, 50, 170, 130}, "Shot",
		BWindowLook::Titled);
	(void)window->UseBackend(std::make_unique<PixelBackend>());
	window->AddChild(std::make_unique<BView>(gfx::Rect{10, 20, 60, 50}, "inner",
		kFollowLeftTop, BViewFlag::WillDraw));
	window->Show();
	return *BLooper::Run(std::move(window));
}


gfx::Size
SizeOf(const std::expected<BBitmap, Error>& bitmap)
{
	if (!bitmap.has_value())
		return {0, 0};
	return {bitmap->Bounds().Width(), bitmap->Bounds().Height()};
}


TEST(WindowSnapshotTest, SnapshotsHaveTheirSizesAndErrors)
{
	// What is tested: Snapshots of a window of 120 × 80 from the test
	// thread (through the window's messenger and the driver): the whole
	// window at its scale, at scale 2, of an area of 30 × 20; the shown
	// frame; a view's snapshot on the window's thread; an empty area, an
	// unknown backend, a view without a window, a window that is gone.
	// Success criteria: 120 × 80, 240 × 160 and 30 × 20 pixels in
	// B_RGBA32; the frame shown 120 × 80; the view's snapshot 50 × 30, its
	// bounds. An empty area is InvalidArgument, an unknown backend
	// NotFound, a view without a window InvalidArgument; once the window is
	// gone, a snapshot and a capture are NotFound.
	// Why it matters: Screenshots for tests, reports and documentation come
	// from any thread and must not hang on a window that went away
	// (specification 19.1).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-snapshot-test");
	Driver driver(app);
	const BMessenger window = OpenWindow();
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	WindowRef ref = driver.Window("Shot");

	// When
	const auto whole = BWindow::Snapshot(window).get();
	BSnapshotOptions twice;
	twice.scale = 2;
	const auto doubled = BWindow::Snapshot(window, twice).get();
	BSnapshotOptions part;
	part.area = gfx::Rect{10, 10, 40, 30};
	const auto area = ref.Snapshot(part);
	const auto shown = ref.CaptureFrame();
	gfx::Size viewSize{0, 0};
	(void)ref.Find("inner").Run([&](BView& view) { viewSize = SizeOf(view.Snapshot()); });
	BSnapshotOptions empty;
	empty.area = gfx::Rect{10, 10, 10, 30};
	const auto emptyArea = ref.Snapshot(empty);
	BSnapshotOptions unknown;
	unknown.backend = "nonexistent";
	const auto unknownBackend = ref.Snapshot(unknown);
	BView loose({0, 0, 10, 10}, "loose", kFollowLeftTop, BViewFlag::WillDraw);
	const auto withoutWindow = loose.Snapshot();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	const auto gone = BWindow::Snapshot(window).get();
	const auto goneFrame = BWindow::CaptureFrame(window).get();
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(whole.has_value()) << whole.error().message;
	EXPECT_EQ(whole->ColorSpace(), BColorSpace::RGBA32);
	EXPECT_EQ(SizeOf(whole), (gfx::Size{120, 80}));
	EXPECT_EQ(SizeOf(doubled), (gfx::Size{240, 160}));
	EXPECT_EQ(SizeOf(area), (gfx::Size{30, 20}));
	EXPECT_EQ(SizeOf(shown), (gfx::Size{120, 80}));
	EXPECT_EQ(viewSize, (gfx::Size{50, 30}));
	ASSERT_FALSE(emptyArea.has_value());
	EXPECT_EQ(emptyArea.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(unknownBackend.has_value());
	EXPECT_EQ(unknownBackend.error().code, ErrorCode::NotFound);
	ASSERT_FALSE(withoutWindow.has_value());
	EXPECT_EQ(withoutWindow.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(gone.has_value());
	EXPECT_EQ(gone.error().code, ErrorCode::NotFound);
	ASSERT_FALSE(goneFrame.has_value());
	EXPECT_EQ(goneFrame.error().code, ErrorCode::NotFound);
}


TEST(WindowSnapshotTest, RecordingsDrawAsSnapshots)
{
	// What is tested: Recordings of the window of 120 × 80 for an export
	// (Record()): the whole window from the test thread, an area of 30 × 20,
	// the view of 50 × 30 on the window's thread; each rendered by the pixel
	// backend at scale 1 and set against the window's snapshot; the errors
	// of an empty area, an unknown backend, a view without a window and a
	// window that is gone.
	// Success criteria: The lists' bounds are the window's bounds, the area
	// and the view's bounds; the window's list renders to its snapshot,
	// pixel for pixel, the area's to the area's snapshot. An empty area is
	// InvalidArgument, an unknown backend NotFound, a view without a window
	// InvalidArgument, a window that is gone NotFound.
	// Why it matters: An export (SVG, PDF) and a printout show the window as
	// its snapshot does: the same views on the same background
	// (specification 19).
	//
	// Given
	TestEnvironment environment;
	FixedTextSystem text;
	TextSystem::SetDefault(&text);
	BApplication app("application/x-vnd.ngui-record-test");
	Driver driver(app);
	const BMessenger window = OpenWindow();
	ASSERT_TRUE(driver.WaitForIdle().has_value());
	PixelBackend pixels;

	// When
	const auto whole = BWindow::Record(window).get();
	const auto wholeShot = BWindow::Snapshot(window).get();
	BSnapshotOptions part;
	part.area = gfx::Rect{10, 10, 40, 30};
	const auto area = BWindow::Record(window, part).get();
	const auto areaShot = BWindow::Snapshot(window, part).get();
	gfx::Rect viewBounds;
	(void)driver.Window("Shot").Find("inner").Run([&](BView& view) {
		const auto recorded = view.Record();
		if (recorded.has_value())
			viewBounds = (*recorded)->Bounds();
	});
	BSnapshotOptions empty;
	empty.area = gfx::Rect{10, 10, 10, 30};
	const auto emptyArea = BWindow::Record(window, empty).get();
	BSnapshotOptions unknown;
	unknown.backend = "nonexistent";
	const auto unknownBackend = BWindow::Record(window, unknown).get();
	BView loose({0, 0, 10, 10}, "loose", kFollowLeftTop, BViewFlag::WillDraw);
	const auto withoutWindow = loose.Record();
	ASSERT_TRUE(window.SendMessage(kQuitRequested).has_value());
	ASSERT_TRUE(window.WaitForLooperQuit(5s).has_value());
	const auto gone = BWindow::Record(window).get();
	TextSystem::SetDefault(nullptr);

	// Then
	ASSERT_TRUE(whole.has_value()) << whole.error().message;
	ASSERT_TRUE(area.has_value()) << area.error().message;
	ASSERT_TRUE(wholeShot.has_value() && areaShot.has_value());
	EXPECT_EQ((*whole)->Bounds(), (gfx::Rect{0, 0, 120, 80}));
	EXPECT_EQ((*area)->Bounds(), (gfx::Rect{10, 10, 40, 30}));
	EXPECT_EQ(viewBounds, (gfx::Rect{0, 0, 50, 30}));
	const auto wholeImage = RenderToImage(pixels, **whole);
	ASSERT_TRUE(wholeImage.has_value());
	EXPECT_TRUE(CompareImages(Image::FromBitmap(*wholeShot), *wholeImage,
		Tolerance::Exact()).passed);
	const auto areaImage = RenderToImage(pixels, **area);
	ASSERT_TRUE(areaImage.has_value());
	EXPECT_TRUE(CompareImages(Image::FromBitmap(*areaShot), *areaImage,
		Tolerance::Exact()).passed);
	ASSERT_FALSE(emptyArea.has_value());
	EXPECT_EQ(emptyArea.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(unknownBackend.has_value());
	EXPECT_EQ(unknownBackend.error().code, ErrorCode::NotFound);
	ASSERT_FALSE(withoutWindow.has_value());
	EXPECT_EQ(withoutWindow.error().code, ErrorCode::InvalidArgument);
	ASSERT_FALSE(gone.has_value());
	EXPECT_EQ(gone.error().code, ErrorCode::NotFound);
}


} // namespace
