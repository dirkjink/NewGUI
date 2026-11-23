/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// A window with one button in the BeOS look: pressed while the mouse
// button is down over it, as Haiku's BButton tracks a click. Closing the
// window quits the application. It shows the toolkit end to end: the
// platform's events as messages to the window, distributed to views; the
// look drawing into a view's Canvas; the window putting the DisplayLists
// together, Skia rendering them, the presenter copying the changed area
// into the window; the label shaped by the text subsystem with the bundled
// Noto Sans, drawn by Skia with the same font file; the layout keeping the
// button centered when the window is resized (BLayoutBuilder).


#include <cstdio>
#include <memory>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/BeLook.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/platform/SdlPlatform.h>
#include <ngui/text/FontTextSystem.h>


namespace {


using namespace ngui;


// A labeled button, drawn by the BeOS look as Haiku's BButton::Draw().
class ButtonView : public BView {
public:
	ButtonView()
		:
		BView("button"),
		fLook(CreateBeLook())
	{
		SetViewColor(std::nullopt);
		SetExplicitSize({120, 32});
	}

	void Draw(DrawContext& context) override
	{
		const gfx::Color base = context.palette.Get(BColorWhich::ControlBackground);
		const gfx::Color background = context.palette.Get(BColorWhich::PanelBackground);
		const gfx::Color text = context.palette.Get(BColorWhich::ControlText);
		gfx::Rect rect = Bounds();
		const ControlStateFlags flags = fPressed ? ControlState::Activated : ControlStateFlags();
		fLook->DrawButtonFrame(context, rect, base, background, flags);
		fLook->DrawButtonBackground(context, rect, base, flags);
		fLook->DrawLabel(context, "Click me", 0, rect, base, flags,
			BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle}, &text);
	}

	void MouseDown(gfx::Point) override
	{
		// As BButton: follow the mouse until the button is released.
		SetMouseEventMask(BEventMask::Pointer);
		_SetPressed(true);
	}

	void MouseMoved(gfx::Point where, BMouseTransit, const BMessage*) override
	{
		if (fTracking)
			_SetPressed(Bounds().Contains(where));
	}

	void MouseUp(gfx::Point) override
	{
		fTracking = false;
		_SetPressed(false);
	}

private:
	void _SetPressed(bool pressed)
	{
		fTracking = fTracking || pressed;
		if (pressed == fPressed)
			return;
		fPressed = pressed;
		Invalidate();
	}

	std::shared_ptr<const ControlLook> fLook;
	bool fPressed = false;
	bool fTracking = false;
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
		auto window = std::make_unique<BWindow>(gfx::Rect{100, 100, 420, 260}, "NewGUI",
			BWindowLook::Titled,
			BWindowFlag::QuitOnWindowClose | BWindowFlag::AutoUpdateSizeLimits);
		// The button stays centered; the window cannot become smaller than
		// the button and the window insets.
		BLayoutBuilder::Group<>(window.get(), BOrientation::Vertical)
			.SetInsets(kUseWindowInsets)
			.AddGlue()
			.AddGroup(BOrientation::Horizontal)
				.AddGlue()
				.Add(std::make_unique<ButtonView>())
				.AddGlue()
			.End()
			.AddGlue();
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
