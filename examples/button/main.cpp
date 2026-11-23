/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


// A window with one button in the BeOS look: pressed while the mouse
// button is down over it. Closing the window quits the application. It
// shows the path of the toolkit end to end: the platform's events as
// messages, a looper per window, the look drawing into a Canvas, Skia
// rendering into a CPU surface, the presenter copying into the window.
// There is no label yet: text needs the text subsystem.


#include <cstdio>
#include <memory>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/BeLook.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/SkiaBackend.h>
#include <ngui/platform/SdlPlatform.h>


namespace {


using namespace ngui;


// Metrics without fonts; the button has no label.
class NoText : public TextSystem {
public:
	float FontSize(FontRole) const override
	{
		return 12;
	}

	FontHeight Height(FontRole) const override
	{
		return {9, 3, 1};
	}

	float StringWidth(std::string_view, FontRole) const override
	{
		return 0;
	}

	std::string Truncate(std::string_view text, float, TruncationMode, FontRole) const override
	{
		return std::string(text);
	}

	ShapedText Shape(std::string_view, FontRole) const override
	{
		return {};
	}
};


class ButtonWindow : public BLooper {
public:
	ButtonWindow()
		:
		BLooper("button window"),
		fLook(CreateBeLook())
	{
		platform::WindowSpec spec;
		spec.title = "NewGUI";
		spec.frame = {100, 100, 420, 260};
		spec.target = BMessenger(this);
		auto window = platform::Current()->CreateWindow(spec);
		if (!window.has_value()) {
			std::fprintf(stderr, "no window: %s\n", window.error().message.c_str());
			return;
		}
		fWindow = std::move(*window);
		fWindow->Show();
	}

	bool QuitRequested() override
	{
		// The window goes first, then the application.
		fWindow.reset();
		(void)AppMessenger().SendMessage(kQuitRequested);
		return true;
	}

	void MessageReceived(BMessage* message) override
	{
		switch (message->what) {
			case kWindowResized:
				fSurface.reset();
				_Draw();
				break;
			case kMouseDown:
				fPressed = fButton.Contains(message->GetPoint("where", {}));
				_Draw();
				break;
			case kMouseUp:
				fPressed = false;
				_Draw();
				break;
			default:
				BLooper::MessageReceived(message);
		}
	}

	void Start()
	{
		_Draw();
	}

private:
	void _Draw()
	{
		if (fWindow == nullptr)
			return;
		const gfx::NativeWindow native = fWindow->Native();
		if (fSurface == nullptr)
			fSurface = fBackend.CreateWindowSurface(native, gfx::SurfaceKind::CpuRaster);
		if (fSurface == nullptr)
			return;

		const gfx::Rect bounds{0, 0, native.size.w, native.size.h};
		gfx::Canvas canvas(bounds);
		const gfx::Caps caps;
		DrawContext context{canvas, caps, Palette::Default(), fText, native.scale};
		const gfx::Color panel = Palette::Default().Get(BColorWhich::PanelBackground);
		canvas.FillRect(bounds, gfx::Paint{.color = panel});

		const float x = (bounds.Width() - 120) / 2;
		const float y = (bounds.Height() - 32) / 2;
		fButton = {x, y, x + 120, y + 32};
		gfx::Rect rect = fButton;
		const ControlStateFlags flags = fPressed ? ControlState::Activated : ControlStateFlags();
		fLook->DrawButtonFrame(context, rect, panel, panel, flags);
		fLook->DrawButtonBackground(context, rect, panel, flags);

		fBackend.BeginFrame(*fSurface, panel);
		fBackend.Render(*canvas.Finish(), {0, 0});
		fBackend.EndFrame(*fSurface);
		const std::optional<gfx::CpuPixels> pixels = fSurface->Pixels();
		if (!pixels.has_value())
			return;
		const gfx::IntRect all{0, 0, pixels->width, pixels->height};
		(void)fWindow->Present(*pixels, std::span(&all, 1));
	}

	std::shared_ptr<const ControlLook> fLook;
	NoText fText;
	gfx::SkiaBackend fBackend;
	gfx::SurfaceRef fSurface;
	std::unique_ptr<platform::PlatformWindow> fWindow;
	gfx::Rect fButton{0, 0, 0, 0};
	bool fPressed = false;
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
		// On the main thread: the window is made here, then runs on its own.
		auto window = std::make_unique<ButtonWindow>();
		window->Start();
		(void)BLooper::Run(std::move(window));
	}
};


} // namespace


int
main(int argc, char** argv)
{
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
