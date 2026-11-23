/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// A window with a button and a label in the BeOS look: a BButton tracks
// the click as Haiku's does and sends its message to the window, which
// counts the clicks in a BStringView; Enter clicks it too, as it is the
// default button. Closing the window quits the application. It shows the
// toolkit end to end: the platform's events as messages to the window,
// distributed to views; the controls drawing through the look into their
// Canvas; the window putting the DisplayLists together, Skia rendering
// them, the presenter copying the changed area into the window; text
// shaped by the text subsystem with the bundled Noto Sans, drawn by Skia
// with the same font file; the layout keeping both centered when the
// window is resized (BLayoutBuilder). Below them a BSlider drives a
// BStatusBar while its thumb moves (its modification message).


#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Button.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/Slider.h>
#include <ngui/StatusBar.h>
#include <ngui/StringView.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/platform/SdlPlatform.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;


constexpr std::uint32_t kClicked = FourCharCode("CLCK");
constexpr std::uint32_t kProgress = FourCharCode("PRGS");


// The window: counts the clicks of its button in its label; shows the
// value of its slider in its status bar.
class CounterWindow : public BWindow {
public:
	CounterWindow()
		:
		BWindow(gfx::Rect{100, 100, 420, 340}, "NewGUI", BWindowLook::Titled,
			BWindowFlag::QuitOnWindowClose | BWindowFlag::AutoUpdateSizeLimits)
	{
		auto label = std::make_unique<BStringView>("label", "Not clicked yet");
		fLabel = label.get();
		auto button = std::make_unique<BButton>("Click me",
			std::make_unique<BMessage>(kClicked));
		BButton* clickMe = button.get();
		auto slider = std::make_unique<BSlider>("slider", "Progress",
			std::make_unique<BMessage>(kProgress), 0, 100, BOrientation::Horizontal);
		slider->SetModificationMessage(std::make_unique<BMessage>(kProgress));
		slider->SetLimitLabels("0 %", "100 %");
		slider->SetHashMarks(BHashMarkLocation::BottomOrRight);
		slider->SetHashMarkCount(11);
		fSlider = slider.get();
		auto status = std::make_unique<BStatusBar>("status", "Progress: ", " %");
		status->SetTo(0, {}, "0");
		fStatus = status.get();

		// The label and the button stay centered, the slider and the status
		// bar take the width; the window cannot become smaller than they and
		// the window insets.
		BLayoutBuilder::Group<>(this, BOrientation::Vertical)
			.SetInsets(kUseWindowInsets)
			.AddGlue()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(std::move(label))
				.AddGlue()
			.End()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(std::move(button))
				.AddGlue()
			.End()
			.AddGlue()
			.Add(std::move(slider))
			.Add(std::move(status));

		clickMe->MakeDefault(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kProgress) {
			const std::int32_t value = fSlider->Value();
			fStatus->SetTo(static_cast<float>(value), {}, std::to_string(value));
			return;
		}
		if (message->what != kClicked) {
			BWindow::MessageReceived(message);
			return;
		}
		fClicks++;
		fLabel->SetText(fClicks == 1 ? "Clicked once"
			: "Clicked " + std::to_string(fClicks) + " times");
	}

private:
	BStringView* fLabel = nullptr;
	BSlider* fSlider = nullptr;
	BStatusBar* fStatus = nullptr;
	int fClicks = 0;
};


class ButtonApp : public BApplication {
public:
	ButtonApp(int argc, char** argv)
		:
		BApplication("application/x-vnd.ngui-example-button", argc, argv)
	{
	}

	void ReadyToRun() override
	{
		auto window = std::make_unique<CounterWindow>();
		window->Show();
		(void)BLooper::Run(std::move(window));
	}
};


} // namespace


int
main(int argc, char** argv)
{
	gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	// The text system of all windows; it lives until the end of main().
	auto fonts = text::FontTextSystem::CreateBundled();
	if (!fonts.has_value()) {
		std::fprintf(stderr, "%s\n", fonts.error().message.c_str());
		return 1;
	}
	TextSystem::SetDefault(fonts->get());
	auto platform = platform::SdlPlatform::Create();
	if (!platform.has_value()) {
		std::fprintf(stderr, "%s\n", platform.error().message.c_str());
		return 1;
	}
	ButtonApp app(argc, argv);
	if (!app.UsePlatform(std::move(*platform)).has_value())
		return 1;
	const int result = app.Run();
	TextSystem::SetDefault(nullptr);
	return result;
}
