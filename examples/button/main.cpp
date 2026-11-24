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
// window is resized (BLayoutBuilder). Below them, in a BBox, a BSlider
// drives a BStatusBar while its thumb moves (its modification message);
// a BScrollView shows a list of check boxes that is longer than its room;
// a BSplitView puts both side by side with a splitter to drag, on the
// first page of a BTabView whose second page says what this is. A BMenuBar
// on top resets the counter, quits, or shows that page (its menus open in
// popups of the window; Alt+Escape opens them from the keyboard, Alt+R
// resets).


#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Box.h>
#include <ngui/Button.h>
#include <ngui/CheckBox.h>
#include <ngui/GroupView.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/Menu.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuItem.h>
#include <ngui/ScrollView.h>
#include <ngui/Slider.h>
#include <ngui/StatusBar.h>
#include <ngui/StringView.h>
#include <ngui/TabView.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/platform/SdlPlatform.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;


constexpr std::uint32_t kClicked = FourCharCode("CLCK");
constexpr std::uint32_t kProgress = FourCharCode("PRGS");
constexpr std::uint32_t kReset = FourCharCode("RSET");
constexpr std::uint32_t kAbout = FourCharCode("ABOT");


// The window: counts the clicks of its button in its label; shows the
// value of its slider in its status bar.
class CounterWindow : public BWindow {
public:
	CounterWindow()
		:
		BWindow(gfx::Rect{100, 100, 620, 440}, "NewGUI", BWindowLook::Titled,
			BWindowFlag::QuitOnWindowClose | BWindowFlag::AutoUpdateSizeLimits)
	{
		auto label = std::make_unique<BStringView>("label", "Not clicked yet");
		fLabel = label.get();
		auto button = std::make_unique<BButton>("Click me",
			std::make_unique<BMessage>(kClicked));
		BButton* clickMe = button.get();
		auto slider = std::make_unique<BSlider>("slider", "Value",
			std::make_unique<BMessage>(kProgress), 0, 100, BOrientation::Horizontal);
		slider->SetModificationMessage(std::make_unique<BMessage>(kProgress));
		slider->SetLimitLabels("0 %", "100 %");
		slider->SetHashMarks(BHashMarkLocation::BottomOrRight);
		slider->SetHashMarkCount(11);
		fSlider = slider.get();
		auto status = std::make_unique<BStatusBar>("status", "Progress: ", " %");
		status->SetTo(0, {}, "0");
		fStatus = status.get();

		// The slider and the status bar in a labelled box.
		auto progress = std::make_unique<BGroupView>(BOrientation::Vertical);
		BLayoutBuilder::Group<>(progress.get())
			.SetInsets(kUseItemInsets)
			.Add(std::move(slider))
			.Add(std::move(status));
		auto box = std::make_unique<BBox>("progress box");
		box->SetLabel("Progress");
		box->AddChild(std::move(progress));

		// A list of options, longer than the room for it: the scroll view
		// sets up its bar from the list's preferred size.
		auto options = std::make_unique<BGroupView>(BOrientation::Vertical);
		BLayoutBuilder::Group<> optionsBuilder(options.get());
		optionsBuilder.SetInsets(kUseHalfItemInsets);
		for (int i = 1; i <= 12; i++) {
			optionsBuilder.Add(std::make_unique<BCheckBox>(
				"Option " + std::to_string(i), nullptr));
		}
		auto scrollView = std::make_unique<BScrollView>("options", std::move(options),
			BViewFlags{}, false, true);
		scrollView->SetExplicitMinSize({kSizeUnset, 60});
		scrollView->SetExplicitPreferredSize({kSizeUnset, 110});

		// The box and the list side by side, on the first of two pages.
		auto split = std::make_unique<BSplitView>(BOrientation::Horizontal);
		BLayoutBuilder::Split<>(split.get())
			.Add(std::move(box), 2.0f)
			.Add(std::move(scrollView), 1.0f);
		auto tabs = std::make_unique<BTabView>("tabs");
		tabs->AddTab(std::move(split));
		tabs->TabAt(0)->SetLabel("Panels");
		tabs->AddTab(std::make_unique<BStringView>("About",
			"NewGUI: Haiku's Interface Kit on SDL3 and Skia"));

		// The menus.
		auto menuBar = std::make_unique<BMenuBar>("menu bar");
		auto file = std::make_unique<BMenu>("File");
		file->AddItem(std::make_unique<BMenuItem>("Reset", std::make_unique<BMessage>(kReset),
			'R', BModifier::Command));
		file->AddSeparatorItem();
		file->AddItem(std::make_unique<BMenuItem>("Quit",
			std::make_unique<BMessage>(kQuitRequested), 'Q', BModifier::Command));
		auto help = std::make_unique<BMenu>("Help");
		help->AddItem(std::make_unique<BMenuItem>("About", std::make_unique<BMessage>(kAbout)));
		menuBar->AddItem(std::move(file));
		menuBar->AddItem(std::move(help));
		fTabs = tabs.get();

		// The menu bar on top, without insets; below it the label and the
		// button stay centered and the tab view takes the width; the window
		// cannot become smaller than they and the window insets.
		BLayoutBuilder::Group<>(this, BOrientation::Vertical, 0)
			.Add(std::move(menuBar))
			.AddGroup(BOrientation::Vertical)
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
			.Add(std::move(tabs))
			.End();

		clickMe->MakeDefault(true);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kProgress) {
			const std::int32_t value = fSlider->Value();
			fStatus->SetTo(static_cast<float>(value), {}, std::to_string(value));
			return;
		}
		if (message->what == kReset) {
			fClicks = 0;
			fLabel->SetText("Not clicked yet");
			return;
		}
		if (message->what == kAbout) {
			fTabs->Select(1);
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
	BTabView* fTabs = nullptr;
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
