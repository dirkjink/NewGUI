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
// into the window. There is no label yet: text needs the text subsystem.


#include <cstdio>
#include <memory>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/BeLook.h>
#include <ngui/View.h>
#include <ngui/Window.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/platform/SdlPlatform.h>


namespace {


using namespace ngui;


// A button without a label, drawn by the BeOS look.
class ButtonView : public BView {
public:
	explicit ButtonView(gfx::Rect frame)
		:
		BView(frame, "button", BResizingMode{BHorizontalFollow::Center,
			BVerticalFollow::Center}),
		fLook(CreateBeLook())
	{
		SetViewColor(std::nullopt);
	}

	void Draw(DrawContext& context) override
	{
		const gfx::Color panel = context.palette.Get(BColorWhich::PanelBackground);
		gfx::Rect rect = Bounds();
		const ControlStateFlags flags = fPressed ? ControlState::Activated : ControlStateFlags();
		fLook->DrawButtonFrame(context, rect, panel, panel, flags);
		fLook->DrawButtonBackground(context, rect, panel, flags);
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
			BWindowLook::Titled, BWindowFlag::QuitOnWindowClose);
		window->AddChild(std::make_unique<ButtonView>(gfx::Rect{100, 64, 220, 96}));
		window->Show();
		(void)BLooper::Run(std::move(window));
	}
};


} // namespace


int
main(int argc, char** argv)
{
	gfx::SkiaBackend::Register(gfx::BackendRegistry::Default());
	auto platform = platform::SdlPlatform::Create();
	if (!platform.has_value()) {
		std::fprintf(stderr, "%s\n", platform.error().message.c_str());
		return 1;
	}
	ButtonApp app(argc, argv);
	if (!app.UsePlatform(std::move(*platform)).has_value())
		return 1;
	return app.Run();
}
