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
#include <ngui/Button.h>
#include <ngui/LookRegistry.h>
#include <ngui/TextSystem.h>
#include <ngui/gfx/BackendRegistry.h>

#include "LooperPort.h"
#include "MenuPrivate.h"


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


// Shows the \a dirty areas of \a surface in \a window, in device pixels
// rounded outwards.
void
Present(platform::PlatformWindow& window, gfx::Surface& surface,
	const std::vector<gfx::Rect>& dirty, float scale)
{
	const std::optional<gfx::CpuPixels> pixels = surface.Pixels();
	if (!pixels.has_value())
		return;
	std::vector<gfx::IntRect> areas;
	for (const gfx::Rect& rect : dirty) {
		areas.push_back({static_cast<std::int32_t>(std::floor(rect.x0 * scale)),
			static_cast<std::int32_t>(std::floor(rect.y0 * scale)),
			static_cast<std::int32_t>(std::ceil(rect.x1 * scale)),
			static_cast<std::int32_t>(std::ceil(rect.y1 * scale))});
	}
	(void)window.Present(*pixels, areas);
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


// A popup of the window (NewGUI): a root view whose frame is on the
// screen, the platform popup it is shown in and what is drawn there. It is
// a handler of the window, so that the popup's events arrive in the
// window's thread.
class BWindow::PopupSurface : public BHandler {
public:
	explicit PopupSurface(BWindow* window)
		:
		BHandler("popup"),
		fWindow(window)
	{
	}

	void MessageReceived(BMessage* message) override
	{
		fWindow->_PopupEvent(this, message);
	}

	BWindow*			fWindow;
	std::unique_ptr<BView> root;
	std::unique_ptr<platform::PlatformWindow> platformWindow;
	gfx::SurfaceRef		surface;
	float				scale = 1;
	std::vector<gfx::Rect> dirty;
	gfx::DisplayListRef	lastFrame;
};


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
	fLook = LookRegistry::DefaultLook();
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
	// Open menus give their popups back before the popups close.
	if (fMenuTracking != nullptr)
		fMenuTracking->CancelTracking();
	while (!fPopups.empty())
		(void)ClosePopup(fPopups.back()->root.get());
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


// #pragma mark - Look and default button


const ControlLook&
BWindow::Look() const
{
	return *fLook;
}


void
BWindow::_TakeLook()
{
	fLook = LookRegistry::DefaultLook();
}


BButton*
BWindow::DefaultButton() const
{
	return fDefaultButton;
}


void
BWindow::SetDefaultButton(BButton* button)
{
	// Ported from Haiku: src/kits/interface/Window.cpp (commit b1d497bb69).
	if (fDefaultButton == button)
		return;

	if (fDefaultButton != nullptr) {
		// tell old button it's no longer the default one
		BButton* oldDefault = fDefaultButton;
		oldDefault->MakeDefault(false);
		oldDefault->Invalidate();
	}

	fDefaultButton = button;

	if (button != nullptr) {
		// notify new default button
		fDefaultButton->MakeDefault(true);
		fDefaultButton->Invalidate();
	}
}


// #pragma mark - Layout
//
// Ported from Haiku: src/kits/app/Window.cpp (commit b1d497bb69), the layout
// part. NewGUI: there is no app_server, so SetSizeLimits() keeps the limits
// itself, passes them to the platform window and resizes into them; layout
// runs before a frame instead of on B_LAYOUT_WINDOW.


BLayout*
BWindow::SetLayout(std::unique_ptr<BLayout> layout)
{
	return fRoot->SetLayout(std::move(layout));
}


BLayout*
BWindow::GetLayout() const
{
	return fRoot->GetLayout();
}


BLayoutItem*
BWindow::AddChild(std::unique_ptr<BLayoutItem> child)
{
	return fRoot->AddChild(std::move(child));
}


void
BWindow::InvalidateLayout(bool descendants)
{
	fRoot->InvalidateLayout(descendants);
}


void
BWindow::Layout(bool force)
{
	UpdateSizeLimits();

	// Do the actual layout
	fRoot->Layout(force);
}


void
BWindow::ResizeToPreferred()
{
	Layout(false);

	float width = fRoot->PreferredSize().w;
	width = std::min(width, fRoot->MaxSize().w);
	width = std::max(width, fRoot->MinSize().w);

	float height = fRoot->PreferredSize().h;
	height = std::min(height, fRoot->MaxSize().h);
	height = std::max(height, fRoot->MinSize().h);

	if (GetLayout() != nullptr && GetLayout()->HasHeightForWidth())
		GetLayout()->GetHeightForWidth(width, nullptr, nullptr, &height);

	ResizeTo(width, height);
}


void
BWindow::SetSizeLimits(float minWidth, float maxWidth, float minHeight,
	float maxHeight)
{
	if (minWidth > maxWidth || minHeight > maxHeight)
		return;

	fMinSize = {minWidth, minHeight};
	fMaxSize = {maxWidth, maxHeight};
	if (fPlatformWindow != nullptr)
		fPlatformWindow->SetSizeLimits(fMinSize, fMaxSize);

	// what the app_server does: the window is resized into the limits
	const float width = std::clamp(fFrame.Width(), minWidth, maxWidth);
	const float height = std::clamp(fFrame.Height(), minHeight, maxHeight);
	if (width != fFrame.Width() || height != fFrame.Height())
		ResizeTo(width, height);
}


void
BWindow::GetSizeLimits(float* minWidth, float* maxWidth, float* minHeight,
	float* maxHeight) const
{
	if (minHeight != nullptr)
		*minHeight = fMinSize.h;
	if (minWidth != nullptr)
		*minWidth = fMinSize.w;
	if (maxHeight != nullptr)
		*maxHeight = fMaxSize.h;
	if (maxWidth != nullptr)
		*maxWidth = fMaxSize.w;
}


void
BWindow::UpdateSizeLimits()
{
	if (fFlags.Has(BWindowFlag::AutoUpdateSizeLimits)) {
		// Get min/max constraints of the top view and enforce window
		// size limits respectively.
		gfx::Size minSize = fRoot->MinSize();
		gfx::Size maxSize = fRoot->MaxSize();
		SetSizeLimits(minSize.w, maxSize.w, minSize.h, maxSize.h);
	}
}


void
BWindow::_LayoutInvalidated()
{
	// Haiku posts B_LAYOUT_WINDOW; NewGUI lays out before the next frame.
	fMainUpdateWanted = true;
	_ScheduleUpdate();
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
	// what the app_server does: the size stays within the limits
	width = std::clamp(width, fMinSize.w, fMaxSize.w);
	height = std::clamp(height, fMinSize.h, fMaxSize.h);
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
BWindow::MenusBeginning()
{
}


void
BWindow::MenusEnded()
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
			_MouseEvent(message, fRoot.get());
			return;
		case kKeyDown:
		case kKeyUp:
			_KeyEvent(message);
			return;
		case kMouseWheelChanged:
			// To the view under the mouse, as Haiku's _DetermineTarget();
			// it passes the wheel on to its parents if it cannot scroll.
			if (fMouseView != nullptr)
				fMouseView->MessageReceived(message);
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
			if (!fActive && fMenuTracking != nullptr)
				fMenuTracking->CancelTracking();
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
	fMainUpdateWanted = true;
	_ScheduleUpdate();
}


void
BWindow::_AddDirty(const BView* root, const gfx::Rect& rect)
{
	if (root == fRoot.get()) {
		_AddDirty(rect);
		return;
	}
	PopupSurface* popup = _PopupOf(root);
	if (popup == nullptr)
		return;
	const gfx::Rect bounds = {0, 0, root->Frame().Width(), root->Frame().Height()};
	const gfx::Rect clipped = {std::max(rect.x0, bounds.x0), std::max(rect.y0, bounds.y0),
		std::min(rect.x1, bounds.x1), std::min(rect.y1, bounds.y1)};
	if (clipped.x1 <= clipped.x0 || clipped.y1 <= clipped.y0)
		return;
	popup->dirty.push_back(clipped);
	_ScheduleUpdate();
}


void
BWindow::_ScheduleUpdate()
{
	if (fUpdatePending)
		return;
	fUpdatePending = true;
	(void)PostMessage(BMessage(kUpdate), this);
}


gfx::Point
BWindow::_SurfaceOrigin(const BView* root) const
{
	// The content area's origin on the screen, or the popup's.
	if (root == fRoot.get() || root == nullptr)
		return {fFrame.x0, fFrame.y0};
	const gfx::Rect frame = root->Frame();
	return {frame.x0, frame.y0};
}


BWindow::PopupSurface*
BWindow::_PopupOf(const BView* root) const
{
	for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
		if (popup->root.get() == root)
			return popup.get();
	}
	return nullptr;
}


void
BWindow::_RootFrameChanged(BView* root)
{
	PopupSurface* popup = _PopupOf(root);
	if (popup == nullptr)
		return;
	const gfx::Rect frame = root->Frame();
	if (popup->platformWindow != nullptr) {
		const gfx::Rect current = popup->platformWindow->Frame();
		if (current.x0 != frame.x0 || current.y0 != frame.y0)
			popup->platformWindow->MoveTo({frame.x0, frame.y0});
		if (current.Width() != frame.Width() || current.Height() != frame.Height())
			popup->platformWindow->ResizeTo({frame.Width(), frame.Height()});
	}
	// A new size needs a new surface.
	popup->surface.reset();
	popup->dirty.clear();
	popup->dirty.push_back({0, 0, frame.Width(), frame.Height()});
	_ScheduleUpdate();
}


BView*
BWindow::OpenPopup(std::unique_ptr<BView> view)
{
	if (view == nullptr || view->Parent() != nullptr || view->Window() != nullptr)
		return nullptr;

	auto popup = std::make_unique<PopupSurface>(this);
	AddHandler(popup.get());
	const gfx::Rect frame = view->Frame();
	if (fPlatformWindow != nullptr) {
		if (platform::Platform* platform = platform::Current()) {
			platform::WindowSpec spec;
			spec.title = fTitle;
			spec.frame = frame;
			spec.decorated = false;
			spec.resizable = false;
			spec.target = BMessenger(popup.get(), this);
			spec.parent = fPlatformWindow.get();
			spec.popup = true;
			auto window = platform->CreateWindow(spec);
			if (window.has_value()) {
				popup->platformWindow = std::move(*window);
				popup->scale = popup->platformWindow->Scale();
				popup->platformWindow->Show();
			}
		}
	}
	popup->root = std::move(view);
	BView* root = popup->root.get();
	popup->dirty.push_back({0, 0, frame.Width(), frame.Height()});
	fPopups.push_back(std::move(popup));
	root->_Attach(this);
	_ScheduleUpdate();
	return root;
}


std::unique_ptr<BView>
BWindow::ClosePopup(BView* view)
{
	const auto found = std::find_if(fPopups.begin(), fPopups.end(),
		[view](const std::unique_ptr<PopupSurface>& popup) { return popup->root.get() == view; });
	if (found == fPopups.end())
		return nullptr;
	std::unique_ptr<PopupSurface> popup = std::move(*found);
	fPopups.erase(found);
	popup->root->_Detach();
	(void)RemoveHandler(popup.get());
	popup->surface.reset();
	popup->platformWindow.reset();
	return std::move(popup->root);
}


std::int32_t
BWindow::CountPopups() const
{
	return static_cast<std::int32_t>(fPopups.size());
}


BView*
BWindow::PopupAt(std::int32_t index) const
{
	if (index < 0 || index >= CountPopups())
		return nullptr;
	return fPopups[static_cast<std::size_t>(index)]->root.get();
}


void
BWindow::_PopupEvent(PopupSurface* popup, BMessage* message)
{
	switch (message->what) {
		case kMouseDown:
		case kMouseUp:
		case kMouseMoved:
			_MouseEvent(message, popup->root.get());
			return;
		case kMouseWheelChanged:
			if (fMouseView != nullptr)
				fMouseView->MessageReceived(message);
			return;
		case kKeyDown:
		case kKeyUp:
			// Popups get no keyboard focus; should a key arrive, it goes
			// where the window's keys go.
			_KeyEvent(message);
			return;
		case kWindowResized:
		{
			const float scale = message->GetFloat("be:scale", popup->scale);
			if (scale != popup->scale) {
				popup->scale = scale > 0 ? scale : 1;
				popup->surface.reset();
				ForEachView(popup->root.get(), [](BView* view) { view->Invalidate(); });
			}
			return;
		}
		case kQuitRequested:
			// The window system closed the popup (Wayland does on a click
			// elsewhere): the menus end.
			if (fMenuTracking != nullptr)
				fMenuTracking->CancelTracking();
			return;
		default:
			// The opener decides when a popup closes.
			return;
	}
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
	if (fDefaultButton == view)
		fDefaultButton = nullptr;
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
BWindow::_MouseEvent(BMessage* message, BView* root)
{
	// NewGUI: the position on the screen, so that a view of another surface
	// (the window, a popup) gets it in its own coordinates.
	const gfx::Point where = message->GetPoint("where", {-1, -1});
	const gfx::Point origin = _SurfaceOrigin(root);
	const gfx::Point screen = {where.x + origin.x, where.y + origin.y};
	const bool exited = message->GetBool("be:exited", false);

	// While menus are open, their tracking gets every mouse event.
	if (fMenuTracking != nullptr) {
		const auto buttons = static_cast<std::uint32_t>(message->GetInt32("buttons",
			message->what == kMouseUp ? 0 : static_cast<std::int32_t>(fButtons)));
		fButtons = buttons;
		fMenuTracking->TrackMouse(message->what, screen, buttons);
		return;
	}

	BView* under = exited ? nullptr : ViewAt(root, root->ConvertFromWindow(where));

	switch (message->what) {
		case kMouseDown:
		{
			fButtons = static_cast<std::uint32_t>(message->GetInt32("buttons", 1));
			BView* target = fCapture != nullptr ? fCapture : under;
			if (target != nullptr)
				target->MouseDown(target->ConvertFromScreen(screen));
			break;
		}
		case kMouseUp:
		{
			fButtons = static_cast<std::uint32_t>(message->GetInt32("buttons", 0));
			BView* target = fCapture != nullptr ? fCapture : under;
			if (fButtons == 0)
				fCapture = nullptr;
			if (target != nullptr)
				target->MouseUp(target->ConvertFromScreen(screen));
			break;
		}
		case kMouseMoved:
		{
			if (fCapture != nullptr) {
				const gfx::Point local = fCapture->ConvertFromScreen(screen);
				fCapture->MouseMoved(local, fCapture->Bounds().Contains(local) && !exited
					? BMouseTransit::Inside : BMouseTransit::Outside, nullptr);
				break;
			}
			if (under != fMouseView) {
				if (fMouseView != nullptr) {
					fMouseView->MouseMoved(fMouseView->ConvertFromScreen(screen),
						BMouseTransit::Exited, nullptr);
				}
				fMouseView = under;
				if (under != nullptr) {
					under->MouseMoved(under->ConvertFromScreen(screen), BMouseTransit::Entered,
						nullptr);
				}
			} else if (under != nullptr) {
				under->MouseMoved(under->ConvertFromScreen(screen), BMouseTransit::Inside,
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
	// While menus are open, their tracking gets the keys first.
	if (fMenuTracking != nullptr && fMenuTracking->TrackKey(message->what, bytes))
		return;
	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(message->GetInt32("modifiers", 0)));
	if (message->what == kKeyDown && bytes == "\t" && modifiers.Has(BModifier::Option)) {
		// Option+Tab navigates even out of views that use Tab themselves.
		_KeyboardNavigation();
		return;
	}
	// The default button hears Enter without modifiers, as in Haiku's
	// _DetermineTarget().
	if (fDefaultButton != nullptr && bytes.size() == 1 && bytes[0] == keys::kEnter
		&& !modifiers.HasAny(BModifier::Shift | BModifier::Command | BModifier::Control
			| BModifier::Option)) {
		if (message->what == kKeyDown)
			fDefaultButton->KeyDown(bytes);
		else
			fDefaultButton->KeyUp(bytes);
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
	// A new cycle takes the look anew (L10).
	_TakeLook();
	// Layout first, while the update is still pending: views it moves add
	// their areas to this frame instead of asking for another one.
	if (!fRoot->IsLayoutValid())
		Layout(false);
	for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
		if (!popup->root->IsLayoutValid())
			popup->root->Layout(false);
	}
	fUpdatePending = false;
	// NewGUI: a frame of the content area only if it needs one (not when
	// only a popup changed).
	const bool drawMain = fMainUpdateWanted || fSurface == nullptr;
	fMainUpdateWanted = false;
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
	const gfx::Caps caps = fBackend->Capabilities();

	if (drawMain) {
		if (fSurface == nullptr) {
			gfx::NativeWindow native;
			if (fPlatformWindow != nullptr)
				native = fPlatformWindow->Native();
			native.size = {fFrame.Width(), fFrame.Height()};
			native.scale = fScale;
			fSurface = fBackend->CreateWindowSurface(native, gfx::SurfaceKind::CpuRaster);
			if (fSurface == nullptr)
				return;
			// Not `fDirty = {Bounds()}`: GCC warns wrongly about the bounds
			// of the initializer list (-Warray-bounds) in optimized builds.
			fDirty.clear();
			fDirty.push_back(Bounds());
		}

		const DrawEnvironment environment{caps, Palette::Default(), text, fScale};
		fLastFrame = fRoot->RecordTree(environment);
		fFrameCount++;
		fBackend->BeginFrame(*fSurface, Palette::Default().Get(BColorWhich::PanelBackground));
		fBackend->Render(*fLastFrame, {0, 0});
		fBackend->EndFrame(*fSurface);

		if (fPlatformWindow != nullptr)
			Present(*fPlatformWindow, *fSurface, fDirty, fScale);
		fDirty.clear();
	}

	for (const std::unique_ptr<PopupSurface>& popup : fPopups)
		_UpdatePopup(*popup, caps, text);
}


void
BWindow::_UpdatePopup(PopupSurface& popup, const gfx::Caps& caps, const TextSystem& text)
{
	if (popup.dirty.empty() && popup.surface != nullptr)
		return;
	const gfx::Rect frame = popup.root->Frame();
	if (popup.surface == nullptr) {
		gfx::NativeWindow native;
		if (popup.platformWindow != nullptr)
			native = popup.platformWindow->Native();
		native.size = {frame.Width(), frame.Height()};
		native.scale = popup.scale;
		popup.surface = fBackend->CreateWindowSurface(native, gfx::SurfaceKind::CpuRaster);
		if (popup.surface == nullptr)
			return;
		popup.dirty.clear();
		popup.dirty.push_back({0, 0, frame.Width(), frame.Height()});
	}

	const DrawEnvironment environment{caps, Palette::Default(), text, popup.scale};
	popup.lastFrame = popup.root->RecordTree(environment);
	fBackend->BeginFrame(*popup.surface, Palette::Default().Get(BColorWhich::MenuBackground));
	fBackend->Render(*popup.lastFrame, {0, 0});
	fBackend->EndFrame(*popup.surface);

	if (popup.platformWindow != nullptr)
		Present(*popup.platformWindow, *popup.surface, popup.dirty, popup.scale);
	popup.dirty.clear();
}


} // namespace ngui
