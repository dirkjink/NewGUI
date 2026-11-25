/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>

#include <gtest/gtest.h>

#include <ngui/AppDefs.h>
#include <ngui/Bitmap.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/MenuField.h>
#include <ngui/MenuItem.h>
#include <ngui/PopUpMenu.h>
#include <ngui/TextControl.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/test/Driver.h>
#include <ngui/test/GTest.h>
#include <ngui/test/Image.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;
using namespace ngui::test;
using namespace std::chrono_literals;


// A form drawn by Skia: a box with a button, a checked check box, a text
// field with text, and a menu field with a pop-up menu.
class FormWindow : public BWindow {
public:
	FormWindow()
		:
		BWindow({100, 100, 340, 260}, "Form", BWindowLook::Titled)
	{
		(void)UseBackend(std::make_unique<gfx::SkiaBackend>());
		auto box = std::make_unique<BBox>(gfx::Rect{10, 10, 230, 150}, "box");
		box->AddChild(std::make_unique<BButton>(gfx::Rect{10, 10, 90, 35}, "ok", "OK",
			std::make_unique<BMessage>(1)));
		auto check = std::make_unique<BCheckBox>(gfx::Rect{10, 45, 200, 65}, "check",
			"Save automatically", nullptr);
		check->SetValue(kControlOn);
		box->AddChild(std::move(check));
		box->AddChild(std::make_unique<BTextControl>(gfx::Rect{10, 75, 210, 100}, "name",
			"Name:", "Haiku", nullptr));
		auto popUp = std::make_unique<BPopUpMenu>("size");
		for (const char* label : {"Small", "Medium", "Large"})
			popUp->AddItem(std::make_unique<BMenuItem>(label, std::make_unique<BMessage>(2)));
		popUp->ItemAt(1)->SetMarked(true);
		auto field = std::make_unique<BMenuField>(gfx::Rect{10, 108, 200, 132}, "size",
			"Size:", std::move(popUp), true);
		field->SetDivider(40);
		box->AddChild(std::move(field));
		AddChild(std::move(box));
	}
};


// The application with the bundled fonts, its form open.
class FormApp {
public:
	FormApp()
		:
		fText(std::move(*text::FontTextSystem::CreateBundled())),
		fApp("application/x-vnd.ngui-snapshot-skia-test")
	{
		(void)gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
		TextSystem::SetDefault(fText.get());
		fDriver = std::make_unique<Driver>(fApp);
		auto window = std::make_unique<FormWindow>();
		window->Show();
		fWindow = *BLooper::Run(std::move(window));
		(void)fDriver->WaitForIdle();
	}

	~FormApp()
	{
		(void)fWindow.SendMessage(kQuitRequested);
		(void)fWindow.WaitForLooperQuit(5s);
		fDriver.reset();
		TextSystem::SetDefault(nullptr);
	}

	Driver& Drive()
	{
		return *fDriver;
	}

	WindowRef Window()
	{
		return fDriver->Window("Form");
	}

private:
	TestEnvironment fEnvironment;
	std::unique_ptr<text::FontTextSystem> fText;
	BApplication fApp;
	std::unique_ptr<Driver> fDriver;
	BMessenger fWindow;
};


// Whether \a part is \a whole's pixels from (\a left, \a top) on.
bool
IsPartOf(const Image& part, const Image& whole, std::int32_t left, std::int32_t top)
{
	if (part.IsEmpty())
		return false;
	for (std::int32_t y = 0; y < part.Height(); y++) {
		for (std::int32_t x = 0; x < part.Width(); x++) {
			if (!(part.At(x, y) == whole.At(left + x, top + y)))
				return false;
		}
	}
	return true;
}


TEST(SnapshotSkiaTest, SnapshotsAreWhatTheWindowShows)
{
	// What is tested: A form drawn by Skia: the frame it shows
	// (CaptureFrame()) and its snapshot; a snapshot of the OK button's area;
	// the box's own snapshot (BView::Snapshot()); a snapshot at scale 2; the
	// snapshot as a PNG; the window against its reference snapshot
	// (EXPECT_IMAGE_SNAPSHOT with the window).
	// Success criteria: The snapshot is the frame shown, pixel for pixel
	// (rule S2). The area and the box are exactly those parts of it. At
	// scale 2 it is twice as large. The PNG, decoded, is the snapshot. The
	// window matches SnapshotSkiaTest_Form.be.skia.1x.png within the text
	// tolerance.
	// Why it matters: Screenshots must show what users see, so that tests,
	// reports and documentation can rely on them (specification 19.1).
	//
	// Given
	FormApp app;
	WindowRef window = app.Window();

	// When
	const auto shown = window.CaptureFrame();
	const auto snapshot = window.Snapshot();
	const std::optional<gfx::Rect> ok = window.Find("ok").Frame();
	const std::optional<gfx::Rect> boxFrame = window.Find("box").Frame();
	BSnapshotOptions button;
	button.area = ok;
	const auto buttonShot = window.Snapshot(button);
	std::optional<std::expected<BBitmap, Error>> boxShot;
	(void)window.Find("box").Run([&](BView& box) { boxShot = box.Snapshot(); });
	BSnapshotOptions twice;
	twice.scale = 2;
	const auto doubled = window.Snapshot(twice);
	ASSERT_TRUE(snapshot.has_value()) << snapshot.error().message;
	const auto png = EncodePng(*snapshot, "skia");

	// Then
	ASSERT_TRUE(shown.has_value()) << shown.error().message;
	const Image image = Image::FromBitmap(*snapshot);
	EXPECT_EQ(Image::FromBitmap(*shown), image);
	ASSERT_TRUE(ok.has_value());
	ASSERT_TRUE(buttonShot.has_value());
	EXPECT_TRUE(IsPartOf(Image::FromBitmap(*buttonShot), image,
		static_cast<std::int32_t>(ok->x0), static_cast<std::int32_t>(ok->y0)));
	ASSERT_TRUE(boxFrame.has_value());
	ASSERT_TRUE(boxShot.has_value() && boxShot->has_value());
	EXPECT_TRUE(IsPartOf(Image::FromBitmap(**boxShot), image,
		static_cast<std::int32_t>(boxFrame->x0), static_cast<std::int32_t>(boxFrame->y0)));
	ASSERT_TRUE(doubled.has_value());
	EXPECT_EQ(doubled->Bounds().Width(), 2 * snapshot->Bounds().Width());
	ASSERT_TRUE(png.has_value()) << png.error().message;
	const auto decoded = DecodePng(*png);
	ASSERT_TRUE(decoded.has_value());
	EXPECT_EQ(*decoded, image);
	EXPECT_IMAGE_SNAPSHOT(window, "Form",
		{.look = "be", .backend = "skia", .tolerance = Tolerance::Text()});
}


TEST(SnapshotSkiaTest, PopupsAreLaidOverWhenAsked)
{
	// What is tested: The form with its menu field's pop-up menu open
	// (clicked by the driver): a snapshot without and one with its popups.
	// Success criteria: Both have the window's size. Outside the menu they
	// are the same; inside it they differ (the menu is laid over), as
	// rule S3 has it.
	// Why it matters: A screenshot of an open menu shows the menu, as the
	// user sees it, only when asked for.
	//
	// Given
	FormApp app;
	WindowRef window = app.Window();
	ASSERT_TRUE(app.Drive().Click(window.Find("size")).has_value());
	ASSERT_TRUE(app.Drive().WaitForIdle().has_value());
	gfx::Rect menu{0, 0, 0, 0};
	for (WindowRef& other : app.Drive().Windows()) {
		platform::HeadlessWindow* platformWindow = other.PlatformWindow();
		if (platformWindow != nullptr && platformWindow->IsPopup())
			menu = platformWindow->Frame();
	}

	// When
	const auto without = window.Snapshot();
	BSnapshotOptions popups;
	popups.includePopups = true;
	const auto with = window.Snapshot(popups);

	// Then
	ASSERT_GT(menu.Width(), 0);
	ASSERT_TRUE(without.has_value());
	ASSERT_TRUE(with.has_value());
	const Image plain = Image::FromBitmap(*without);
	const Image laid = Image::FromBitmap(*with);
	ASSERT_EQ(plain.Width(), laid.Width());
	ASSERT_EQ(plain.Height(), laid.Height());
	const gfx::Rect frame = window.Frame();
	const gfx::Rect inside{menu.x0 - frame.x0, menu.y0 - frame.y0, menu.x1 - frame.x0,
		menu.y1 - frame.y0};
	int differentInside = 0;
	int differentOutside = 0;
	for (std::int32_t y = 0; y < plain.Height(); y++) {
		for (std::int32_t x = 0; x < plain.Width(); x++) {
			if (plain.At(x, y) == laid.At(x, y))
				continue;
			const bool in = static_cast<float>(x) >= inside.x0
				&& static_cast<float>(x) < inside.x1 && static_cast<float>(y) >= inside.y0
				&& static_cast<float>(y) < inside.y1;
			(in ? differentInside : differentOutside)++;
		}
	}
	EXPECT_GT(differentInside, 100);
	EXPECT_EQ(differentOutside, 0);
}


} // namespace
