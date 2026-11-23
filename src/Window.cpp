/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/BackendRegistry.h>

#include "LooperPort.h"


namespace ngui {


namespace {


// Draw a frame: the window posts it to itself once per batch of changes
// (Haiku's _UPDATE_).
constexpr std::uint32_t kUpdate = FourCharCode("_UPD");


// The windows of the process, in the order they were made, for the
// application (Haiku's looper list).
std::mutex sWindowsLock;
std::vector<BMessenger> sWindows;


// The deepest visible view at \a point, given in \a view's coordinates.
BView*
ViewAt(BView* view, gfx::Point point)
{
	if (!view->Bounds().Contains(point))
		return nullptr;
	for (std::int32_t i = view->CountChildren() - 1; i >= 0; i--) {
		BView* child = view->ChildAt(i);
		if (child->IsHidden())
			continue;
		if (BView* found = ViewAt(child, child->ConvertFromParent(point)))
			return found;
	}
	return view;
}


// The views of a subtree, depth first.
template<typename Visit>
void
ForEachView(BView* view, Visit visit)
{
	visit(view);
	for (std::int32_t i = 0; i < view->CountChildren(); i++)
		ForEachView(view->ChildAt(i), visit);
}


} // namespace


BWindow::BWindow(gfx::Rect frame, std::string_view title, BWindowLook look, BWindowFlags flags)
	:
	BLooper(title),
	fTitle(title),
	fFlags(flags),
	fFrame(frame)
{
	{
		std::scoped_lock lock(sWindowsLock);
		sWindows.push_back(BMessenger(this));
	}
	fRoot = std::make_unique<BView>(Bounds(), "root", kFollowAll, BViewFlags());
	fRoot->SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));
	fRoot->_Attach(this);

	if (platform::Platform* platform = platform::Current()) {
		platform::WindowSpec spec;
		spec.title = fTitle;
		spec.frame = frame;
		spec.decorated = look != BWindowLook::NoBorder;
		spec.resizable = !flags.Has(BWindowFlag::NotResizable);
		spec.target = BMessenger(this);
		auto window = platform->CreateWindow(spec);
		if (window.has_value()) {
			fPlatformWindow = std::move(*window);
			fScale = fPlatformWindow->Scale();
		}
	}
	_AddDirty(Bounds());
}


BWindow::~BWindow()
{
	{
		const BMessenger self(this);
		std::scoped_lock lock(sWindowsLock);
		std::erase(sWindows, self);
	}
	fRoot->_Detach();
	fRoot.reset();
	fSurface.reset();
	fPlatformWindow.reset();
	if (fFlags.Has(BWindowFlag::QuitOnWindowClose))
		(void)AppMessenger().SendMessage(kQuitRequested);
}


std::expected<void, Error>
BWindow::UseBackend(std::unique_ptr<gfx::Backend> backend)
{
	if (backend == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no backend");
	if (fFrameCount > 0)
		return Failure(ErrorCode::InvalidArgument, "the window has drawn already");
	fSurface.reset();
	fBackend = std::move(backend);
	fFontsOf = nullptr;
	return {};
}


BView*
BWindow::AddChild(std::unique_ptr<BView> child, BView* before)
{
	return fRoot->AddChild(std::move(child), before);
}


std::unique_ptr<BView>
BWindow::RemoveChild(BView* child)
{
	return fRoot->RemoveChild(child);
}


std::int32_t
BWindow::CountChildren() const
{
	return fRoot->CountChildren();
}


BView*
BWindow::ChildAt(std::int32_t index) const
{
	return fRoot->ChildAt(index);
}


BView*
BWindow::FindView(std::string_view name) const
{
	return fRoot->FindView(name);
}


BView*
BWindow::FindView(gfx::Point where) const
{
	return ViewAt(fRoot.get(), where);
}


BView*
BWindow::CurrentFocus() const
{
	return fFocus;
}


BView*
BWindow::RootView() const
{
	return fRoot.get();
}


void
BWindow::Show()
{
	if (fHideLevel == 0)
		return;
	if (--fHideLevel == 0) {
		if (fPlatformWindow != nullptr)
			fPlatformWindow->Show();
		_AddDirty(Bounds());
	}
}


void
BWindow::Hide()
{
	if (fHideLevel++ == 0 && fPlatformWindow != nullptr)
		fPlatformWindow->Hide();
}


bool
BWindow::IsHidden() const
{
	return fHideLevel > 0;
}


gfx::Rect
BWindow::Frame() const
{
	return fFrame;
}


gfx::Rect
BWindow::Bounds() const
{
	return {0, 0, fFrame.Width(), fFrame.Height()};
}


void
BWindow::MoveTo(gfx::Point where)
{
	if (fPlatformWindow != nullptr)
		fPlatformWindow->MoveTo(where);
	const gfx::Point old = {fFrame.x0, fFrame.y0};
	fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
	if (old.x != where.x || old.y != where.y)
		FrameMoved(where);
}


void
BWindow::ResizeTo(float width, float height)
{
	if (fPlatformWindow != nullptr)
		fPlatformWindow->ResizeTo({width, height});
	_Resized(width, height, fScale);
}


const std::string&
BWindow::Title() const
{
	return fTitle;
}


void
BWindow::SetTitle(std::string_view title)
{
	fTitle = title;
	if (fPlatformWindow != nullptr)
		fPlatformWindow->SetTitle(title);
}


BWindowFlags
BWindow::Flags() const
{
	return fFlags;
}


bool
BWindow::IsActive() const
{
	return fActive;
}


gfx::DisplayListRef
BWindow::DisplayList() const
{
	return fLastFrame;
}


std::uint64_t
BWindow::FrameCount() const
{
	return fFrameCount;
}


void
BWindow::UpdateIfNeeded()
{
	if (fUpdatePending)
		_Update();
}


void
BWindow::FrameMoved(gfx::Point)
{
}


void
BWindow::FrameResized(float, float)
{
}


void
BWindow::WindowActivated(bool)
{
}


void
BWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (handler != this) {
		BLooper::DispatchMessage(message, handler);
		return;
	}
	switch (message->what) {
		case kUpdate:
			if (fUpdatePending)
				_Update();
			return;
		case kMouseDown:
		case kMouseUp:
		case kMouseMoved:
			_MouseEvent(message);
			return;
		case kKeyDown:
		case kKeyUp:
			_KeyEvent(message);
			return;
		case kWindowResized:
			_Resized(message->GetFloat("width", fFrame.Width()),
				message->GetFloat("height", fFrame.Height()),
				message->GetFloat("be:scale", fScale));
			return;
		case kWindowMoved:
		{
			const gfx::Point where = message->GetPoint("where", {fFrame.x0, fFrame.y0});
			fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
			FrameMoved(where);
			return;
		}
		case kWindowActivated:
		{
			fActive = message->GetBool("active", false);
			WindowActivated(fActive);
			ForEachView(fRoot.get(), [this](BView* view) { view->WindowActivated(fActive); });
			return;
		}
		case kQuitRequested:
			// The close button of a window that may not be closed; a
			// request of the program (without "when") still counts.
			if (fFlags.Has(BWindowFlag::NotClosable) && message->HasInt64("when"))
				return;
			break;
		default:
			break;
	}
	BLooper::DispatchMessage(message, handler);
}


void
BWindow::_AddDirty(const gfx::Rect& rect)
{
	const gfx::Rect bounds = Bounds();
	const gfx::Rect clipped = {std::max(rect.x0, bounds.x0), std::max(rect.y0, bounds.y0),
		std::min(rect.x1, bounds.x1), std::min(rect.y1, bounds.y1)};
	if (clipped.x1 <= clipped.x0 || clipped.y1 <= clipped.y0)
		return;
	// An area inside one already noted adds nothing; noted areas inside
	// the new one are dropped, so that the presenter copies each pixel
	// once.
	const auto contains = [](const gfx::Rect& outer, const gfx::Rect& inner) {
		return outer.x0 <= inner.x0 && outer.y0 <= inner.y0 && outer.x1 >= inner.x1
			&& outer.y1 >= inner.y1;
	};
	const bool covered = std::any_of(fDirty.begin(), fDirty.end(),
		[&](const gfx::Rect& noted) { return contains(noted, clipped); });
	if (!covered) {
		std::erase_if(fDirty, [&](const gfx::Rect& noted) { return contains(clipped, noted); });
		fDirty.push_back(clipped);
	}
	if (fUpdatePending)
		return;
	fUpdatePending = true;
	(void)PostMessage(BMessage(kUpdate), this);
}


void
BWindow::_ViewDetached(BView* view)
{
	if (fFocus == view)
		fFocus = nullptr;
	if (fMouseView == view)
		fMouseView = nullptr;
	if (fCapture == view)
		fCapture = nullptr;
}


void
BWindow::_SetFocus(BView* view)
{
	if (view == fFocus)
		return;
	BView* old = fFocus;
	fFocus = view;
	// Focused views usually draw a mark.
	if (old != nullptr)
		old->Invalidate();
	if (view != nullptr)
		view->Invalidate();
}


void
BWindow::_SetMouseCapture(BView* view)
{
	// Only while a button is down, as Haiku's SetMouseEventMask() in
	// MouseDown().
	if (fButtons != 0)
		fCapture = view;
}


void
BWindow::_MouseEvent(BMessage* message)
{
	const gfx::Point where = message->GetPoint("where", {-1, -1});
	const bool exited = message->GetBool("be:exited", false);
	BView* under = exited ? nullptr : FindView(where);

	switch (message->what) {
		case kMouseDown:
		{
			fButtons = static_cast<std::uint32_t>(message->GetInt32("buttons", 1));
			BView* target = fCapture != nullptr ? fCapture : under;
			if (target != nullptr)
				target->MouseDown(target->ConvertFromWindow(where));
			break;
		}
		case kMouseUp:
		{
			fButtons = static_cast<std::uint32_t>(message->GetInt32("buttons", 0));
			BView* target = fCapture != nullptr ? fCapture : under;
			if (fButtons == 0)
				fCapture = nullptr;
			if (target != nullptr)
				target->MouseUp(target->ConvertFromWindow(where));
			break;
		}
		case kMouseMoved:
		{
			if (fCapture != nullptr) {
				const gfx::Point local = fCapture->ConvertFromWindow(where);
				fCapture->MouseMoved(local, fCapture->Bounds().Contains(local) && !exited
					? BMouseTransit::Inside : BMouseTransit::Outside, nullptr);
				break;
			}
			if (under != fMouseView) {
				if (fMouseView != nullptr) {
					fMouseView->MouseMoved(fMouseView->ConvertFromWindow(where),
						BMouseTransit::Exited, nullptr);
				}
				fMouseView = under;
				if (under != nullptr) {
					under->MouseMoved(under->ConvertFromWindow(where), BMouseTransit::Entered,
						nullptr);
				}
			} else if (under != nullptr) {
				under->MouseMoved(under->ConvertFromWindow(where), BMouseTransit::Inside,
					nullptr);
			}
			break;
		}
		default:
			break;
	}
}


void
BWindow::_KeyEvent(BMessage* message)
{
	const std::string_view bytes = message->GetString("bytes", "");
	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(message->GetInt32("modifiers", 0)));
	if (message->what == kKeyDown && bytes == "\t" && modifiers.Has(BModifier::Option)) {
		// Option+Tab navigates even out of views that use Tab themselves.
		_KeyboardNavigation();
		return;
	}
	if (fFocus == nullptr) {
		// Without a focus the window navigates, as Haiku's MessageReceived().
		if (message->what == kKeyDown)
			_KeyboardNavigation();
		return;
	}
	if (message->what == kKeyDown)
		fFocus->KeyDown(bytes);
	else
		fFocus->KeyUp(bytes);
}


void
BWindow::_KeyboardNavigation()
{
	// Ported from Haiku: src/kits/interface/Window.cpp (commit b1d497bb69).
	const BMessage* message = CurrentMessage();
	if (message == nullptr || message->GetString("bytes", "") != "\t")
		return;
	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(message->GetInt32("modifiers", 0)));
	const BViewFlags jumpGroups = modifiers.Has(BModifier::Option)
		? BViewFlags(BViewFlag::NavigableJump) : BViewFlags(BViewFlag::Navigable);
	BView* nextFocus = modifiers.Has(BModifier::Shift)
		? _FindPreviousNavigable(fFocus, jumpGroups) : _FindNextNavigable(fFocus, jumpGroups);
	if (nextFocus != nullptr && nextFocus != fFocus)
		nextFocus->MakeFocus(true);
}


BView*
BWindow::_FindNextNavigable(BView* focus, BViewFlags flags) const
{
	// Ported from Haiku: the next view in depth-first order, wrapping
	// around at the root view.
	BView* const top = fRoot.get();
	if (focus == nullptr)
		focus = top;
	BView* nextFocus = focus;
	while (true) {
		if (nextFocus->ChildAt(0) != nullptr) {
			nextFocus = nextFocus->ChildAt(0);
		} else if (nextFocus->NextSibling() != nullptr) {
			nextFocus = nextFocus->NextSibling();
		} else {
			// go to the nearest parent with a next sibling
			while (nextFocus->NextSibling() == nullptr && nextFocus->Parent() != nullptr)
				nextFocus = nextFocus->Parent();
			if (nextFocus == top) {
				// if we started with the top view, we traversed the whole
				// tree already
				if (nextFocus == focus)
					return nullptr;
				nextFocus = nextFocus->ChildAt(0);
			} else {
				nextFocus = nextFocus->NextSibling();
			}
		}
		if (nextFocus == focus || nextFocus == nullptr)
			return nullptr;
		if (!nextFocus->IsHidden() && nextFocus->Flags().HasAny(flags))
			return nextFocus;
	}
}


BView*
BWindow::_FindPreviousNavigable(BView* focus, BViewFlags flags) const
{
	BView* const top = fRoot.get();
	if (focus == nullptr)
		focus = top;
	BView* previousFocus = focus;
	while (true) {
		if (previousFocus->PreviousSibling() != nullptr) {
			// find the last child in the previous sibling
			previousFocus = _LastViewChild(previousFocus->PreviousSibling());
		} else {
			previousFocus = previousFocus->Parent();
			if (previousFocus == top || previousFocus == nullptr)
				previousFocus = _LastViewChild(top);
		}
		if (previousFocus == focus || previousFocus == nullptr)
			return nullptr;
		if (!previousFocus->IsHidden() && previousFocus->Flags().HasAny(flags))
			return previousFocus;
	}
}


BView*
BWindow::_LastViewChild(BView* parent) const
{
	while (true) {
		const std::int32_t count = parent->CountChildren();
		if (count == 0)
			return parent;
		parent = parent->ChildAt(count - 1);
	}
}


std::vector<BMessenger>
BWindow::_Windows()
{
	std::scoped_lock lock(sWindowsLock);
	return sWindows;
}


void
BWindow::_Resized(float width, float height, float scale)
{
	const bool sized = width != fFrame.Width() || height != fFrame.Height();
	const bool scaled = scale != fScale;
	if (!sized && !scaled)
		return;
	fFrame = {fFrame.x0, fFrame.y0, fFrame.x0 + width, fFrame.y0 + height};
	fScale = scale > 0 ? scale : 1;
	fSurface.reset();
	if (sized) {
		fRoot->ResizeTo(width, height);
		FrameResized(width, height);
	}
	if (scaled)
		ForEachView(fRoot.get(), [](BView* view) { view->Invalidate(); });
	_AddDirty(Bounds());
}


void
BWindow::_Update()
{
	fUpdatePending = false;
	if (fPlatformWindow != nullptr && IsHidden())
		return;
	if (fBackend == nullptr) {
		auto backend = gfx::BackendRegistry::Default().CreatePreferred({});
		if (!backend.has_value())
			return;
		fBackend = std::move(*backend);
	}
	// The backend draws the glyph runs of the text system, so it needs its
	// fonts; again whenever the default text system has been replaced.
	const TextSystem& text = TextSystem::Default();
	if (fFontsOf != &text) {
		for (const FontFile& font : text.Fonts())
			(void)fBackend->AddFont(font.id, font.data);
		fFontsOf = &text;
	}
	if (fSurface == nullptr) {
		gfx::NativeWindow native;
		if (fPlatformWindow != nullptr)
			native = fPlatformWindow->Native();
		native.size = {fFrame.Width(), fFrame.Height()};
		native.scale = fScale;
		fSurface = fBackend->CreateWindowSurface(native, gfx::SurfaceKind::CpuRaster);
		if (fSurface == nullptr)
			return;
		// Not `fDirty = {Bounds()}`: GCC warns wrongly about the bounds of
		// the initializer list (-Warray-bounds) in optimized builds.
		fDirty.clear();
		fDirty.push_back(Bounds());
	}

	const gfx::Caps caps = fBackend->Capabilities();
	const DrawEnvironment environment{caps, Palette::Default(), text, fScale};
	fLastFrame = fRoot->RecordTree(environment);
	fFrameCount++;
	fBackend->BeginFrame(*fSurface, Palette::Default().Get(BColorWhich::PanelBackground));
	fBackend->Render(*fLastFrame, {0, 0});
	fBackend->EndFrame(*fSurface);

	if (fPlatformWindow != nullptr) {
		if (const std::optional<gfx::CpuPixels> pixels = fSurface->Pixels()) {
			// The changed areas in device pixels, rounded outwards.
			std::vector<gfx::IntRect> dirty;
			for (const gfx::Rect& rect : fDirty) {
				dirty.push_back({static_cast<std::int32_t>(std::floor(rect.x0 * fScale)),
					static_cast<std::int32_t>(std::floor(rect.y0 * fScale)),
					static_cast<std::int32_t>(std::ceil(rect.x1 * fScale)),
					static_cast<std::int32_t>(std::ceil(rect.y1 * fScale))});
			}
			(void)fPlatformWindow->Present(*pixels, dirty);
		}
	}
	fDirty.clear();
}


} // namespace ngui
