/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <mutex>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Palette.h>
#include <ngui/Button.h>
#include <ngui/Cursor.h>
#include <ngui/LookRegistry.h>
#include <ngui/MenuBar.h>
#include <ngui/MenuItem.h>
#include <ngui/MessageRunner.h>
#include <ngui/TextSystem.h>
#include <ngui/ToolTip.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/Canvas.h>

#include "LooperPort.h"
#include "CursorState.h"
#include "MenuPrivate.h"
#include "ToolTipPopup.h"


namespace ngui {


namespace {


// Draw a frame: the window posts it to itself once per batch of changes
// (Haiku's _UPDATE_).
constexpr std::uint32_t kUpdate = FourCharCode("_UPD");


// Asks a window to activate itself (from a window a modal one blocks).
constexpr std::uint32_t kActivateRequest = FourCharCode("_ACQ");


// The windows of the process, in the order they were made, for the
// application (Haiku's looper list).
std::mutex sWindowsLock;
std::vector<BMessenger> sWindows;

// The shown windows with BWindowFeel::ModalApp, under sWindowsLock.
struct ModalWindow {
	const BWindow*	window;
	BMessenger		messenger;
};
std::vector<ModalWindow> sModalWindows;


// Mouse and keyboard input, which a modal window takes from the others.
bool
IsInput(std::uint32_t what)
{
	return what == kMouseDown || what == kMouseUp || what == kMouseMoved
		|| what == kMouseWheelChanged || what == kKeyDown || what == kKeyUp
		|| what == kInputMethodEvent;
}


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


// The window whose message this thread dispatches (Haiku's
// BLooper::LooperForThread(find_thread(NULL))).
thread_local BWindow* sDispatchingWindow = nullptr;


// A keyboard shortcut (Haiku's BWindow::Shortcut): a key with prepared
// modifiers and either a menu item or a message for a target.
struct BWindow::Shortcut {
	std::uint32_t			fKey = 0;
	BModifierFlags			fModifiers;
	BMenuItem*				fMenuItem = nullptr;
	std::unique_ptr<BMessage> fMessage;
	BHandler*				fTarget = nullptr;

	static BModifierFlags AllowedModifiers()
	{
		// NewGUI: no Menu key
		return BModifier::Command | BModifier::Option | BModifier::Shift | BModifier::Control;
	}

	static std::uint32_t PrepareKey(std::uint32_t key)
	{
		// NewGUI: ASCII only (Haiku: BUnicodeChar::ToUpper())
		if (key >= 'a' && key <= 'z')
			return key - 'a' + 'A';
		return key;
	}

	static BModifierFlags PrepareModifiers(BModifierFlags modifiers)
	{
		const BModifierFlags allowed = BModifierFlags::FromRaw(modifiers.Raw()
			& AllowedModifiers().Raw());
		if (modifiers.Has(BModifier::NoCommand))
			return BModifierFlags::FromRaw(allowed.Raw()
				& ~BModifierFlags(BModifier::Command).Raw());
		return allowed | BModifier::Command;
	}
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

	// Haiku's shortcuts of every window (Command+Q is handled directly, its
	// message goes to the application). NewGUI: not the ones of window
	// management (minimize, zoom, hide, front, back).
	if (!fFlags.Has(BWindowFlag::NotClosable))
		AddShortcut('W', BModifier::Command, std::make_unique<BMessage>(kQuitRequested));
	AddShortcut('X', BModifier::Command, std::make_unique<BMessage>(kCut), nullptr);
	AddShortcut('C', BModifier::Command, std::make_unique<BMessage>(kCopy), nullptr);
	AddShortcut('V', BModifier::Command, std::make_unique<BMessage>(kPaste), nullptr);
	AddShortcut('A', BModifier::Command, std::make_unique<BMessage>(kSelectAll), nullptr);
}


BWindow::BWindow(gfx::Rect frame, std::string_view title, BWindowLook look, BWindowFeel feel,
	BWindowFlags flags)
	:
	BWindow(frame, title, look, flags)
{
	fFeel = feel;
}


BWindow::~BWindow()
{
	{
		const BMessenger self(this);
		std::scoped_lock lock(sWindowsLock);
		std::erase(sWindows, self);
		std::erase_if(sModalWindows, [this](const ModalWindow& modal) {
			return modal.window == this;
		});
	}
	// Open menus give their popups back before the popups close; so does
	// the tooltip.
	if (fMenuTracking != nullptr)
		fMenuTracking->CancelTracking();
	fToolTip.reset();
	fIdleRunner.reset();
	while (!fPopups.empty())
		(void)ClosePopup(fPopups.back()->root.get());
	fRoot->_Detach();
	fRoot.reset();
	fDisposals.clear();
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
		_UpdateModal();
	}
}


void
BWindow::Hide()
{
	if (fHideLevel++ == 0) {
		if (fToolTip != nullptr)
			fToolTip->Close();
		if (fPlatformWindow != nullptr)
			fPlatformWindow->Hide();
		_UpdateModal();
	}
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


void
BWindow::Activate(bool active)
{
	if (active && fPlatformWindow != nullptr)
		fPlatformWindow->Activate();
}


void
BWindow::SetFeel(BWindowFeel feel)
{
	fFeel = feel;
	_UpdateModal();
}


BWindowFeel
BWindow::Feel() const
{
	return fFeel;
}


void
BWindow::_UpdateModal()
{
	// registered while shown with the modal feel
	std::scoped_lock lock(sWindowsLock);
	std::erase_if(sModalWindows, [this](const ModalWindow& modal) {
		return modal.window == this;
	});
	if (fFeel == BWindowFeel::ModalApp && !IsHidden())
		sModalWindows.push_back({this, BMessenger(this)});
}


bool
BWindow::_BlockedByModal(BMessenger* modal) const
{
	// The modal window shown last takes the input; the others, earlier
	// modal ones too, are blocked.
	std::scoped_lock lock(sWindowsLock);
	if (sModalWindows.empty() || sModalWindows.back().window == this)
		return false;
	if (modal != nullptr)
		*modal = sModalWindows.back().messenger;
	return true;
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
	// The window dispatching on this thread; what was disposed of while
	// dispatching goes when the outermost dispatch is done.
	struct Scope {
		BWindow* window;
		BWindow* previous = sDispatchingWindow;
		explicit Scope(BWindow* self) : window(self) { sDispatchingWindow = self; }
		~Scope()
		{
			sDispatchingWindow = previous;
			if (previous != window)
				window->fDisposals.clear();
		}
	} scope(this);

	if (handler != this) {
		BLooper::DispatchMessage(message, handler);
		return;
	}
	// NewGUI: a shown modal window of the application takes the input of
	// the others (Haiku's app_server does that); a click brings it to the
	// front.
	if (IsInput(message->what)) {
		BMessenger modal;
		if (_BlockedByModal(&modal)) {
			if (message->what == kMouseDown)
				(void)modal.SendMessage(kActivateRequest);
			return;
		}
	}

	switch (message->what) {
		case kUpdate:
			if (fUpdatePending)
				_Update();
			return;
		case kActivateRequest:
			Activate();
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
		case kInputMethodEvent:
			// To the focus view, as Haiku's _DetermineTarget(); not while
			// menus are open.
			if (fFocus != nullptr && fMenuTracking == nullptr)
				fFocus->MessageReceived(message);
			return;
		case kMenuTrackerTick:
			if (fMenuTracking != nullptr)
				fMenuTracking->Tick();
			return;
		case kMouseIdleCheck:
			_MouseIdleCheck();
			return;
		case kDropBegin:
		case kDropPosition:
		case kDropFile:
		case kDropText:
		case kDropComplete:
			_DropEvent(message);
			return;
		case kToolTipClose:
			if (fToolTip != nullptr)
				fToolTip->CloseIfHidden();
			return;
		case kMouseWheelChanged:
			// While menus are open, to their tracking (Haiku: to the menu
			// window's view under the mouse).
			if (fMenuTracking != nullptr) {
				_WheelToMenus(message);
				return;
			}
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
			if (fActive) {
				// activated by the window system, but a modal window has the
				// input: it comes to the front
				BMessenger modal;
				if (_BlockedByModal(&modal))
					(void)modal.SendMessage(kActivateRequest);
			}
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
	return _OpenPopup(std::move(view), false);
}


BView*
BWindow::_OpenPopup(std::unique_ptr<BView> view, bool tooltip)
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
			spec.tooltip = tooltip;
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
	// The tooltip takes no input; should the mouse reach it anyway, it
	// follows or closes (Haiku's ToolTipView::MouseMoved()).
	if (fToolTip != nullptr && popup->root.get() == fToolTip->Popup()) {
		if (message->what == kMouseMoved && message->HasPoint("where")) {
			const gfx::Point where = message->GetPoint("where", {0, 0});
			const gfx::Point origin = _SurfaceOrigin(popup->root.get());
			fToolTip->MouseMovedOver({where.x + origin.x, where.y + origin.y});
		}
		return;
	}

	switch (message->what) {
		case kMouseDown:
		case kMouseUp:
		case kMouseMoved:
			_MouseEvent(message, popup->root.get());
			return;
		case kMouseWheelChanged:
			if (fMenuTracking != nullptr) {
				_WheelToMenus(message);
				return;
			}
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
	if (fKeyMenuBar == view)
		fKeyMenuBar = nullptr;
	if (fMenuTracking != nullptr)
		fMenuTracking->ViewDetached(view);
	if (fToolTip != nullptr)
		fToolTip->ViewDetached(view);
	if (fCursorView == view)
		fCursorView = nullptr;
	if (fDragView == view)
		fDragView = nullptr;
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
BWindow::_WheelToMenus(const BMessage* message)
{
	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(message->GetInt32("modifiers", 0)));
	fMenuTracking->TrackWheel(message->GetFloat("be:wheel_delta_y", 0),
		modifiers.Has(BModifier::Shift));
}


void
BWindow::_MouseMovedForToolTip(gfx::Point screen)
{
	// Haiku's ToolTipView gets every mouse move: a sticky tip follows, another
	// one hides.
	if (fToolTip != nullptr)
		fToolTip->MouseMoved(screen);

	// Haiku's app_server sends B_MOUSE_IDLE once the mouse has rested for the
	// show delay; one runner checks, and runs again for the time left.
	fLastMove = Clock::Default().Now();
	fIdlePending = true;
	if (fIdleRunner == nullptr) {
		fIdleRunner = std::make_unique<BMessageRunner>(BMessenger(this),
			BMessage(kMouseIdleCheck), BToolTipManager::Manager()->ShowDelay(), 1);
	}
}


void
BWindow::_MouseIdleCheck()
{
	fIdleRunner.reset();
	if (!fIdlePending)
		return;
	const Clock::Time delay = BToolTipManager::Manager()->ShowDelay();
	const Clock::Time rested = Clock::Default().Now() - fLastMove;
	if (rested < delay) {
		fIdleRunner = std::make_unique<BMessageRunner>(BMessenger(this),
			BMessage(kMouseIdleCheck), delay - rested, 1);
		return;
	}
	fIdlePending = false;

	// To the view the mouse events go to; not while menus are open.
	BView* target = fCapture != nullptr ? fCapture : fMouseView;
	if (fMenuTracking != nullptr || target == nullptr || !fLastMouse.has_value())
		return;
	BMessage idle(kMouseIdle);
	(void)idle.AddPoint("where", target->ConvertFromScreen(*fLastMouse));
	target->MessageReceived(&idle);
}


void
BWindow::_CheckToolTipOwner(const BView* under)
{
	// The mouse left the view whose tip is shown (Haiku's HideToolTip() on
	// B_EXITED_VIEW and B_OUTSIDE_VIEW); a view's tip also stays over its
	// children.
	if (fToolTip == nullptr || fToolTip->Popup() == nullptr)
		return;
	for (const BView* view = under; view != nullptr; view = view->Parent()) {
		if (view == fToolTip->Owner())
			return;
	}
	fToolTip->Hide();
}


void
BWindow::_DropEvent(BMessage* message)
{
	// NewGUI: files and text dragged from other applications (module P2).
	// The platform reports them one by one; the drop goes, as Haiku's
	// app_server delivers it, to the view under the drop point as
	// kSimpleData with the paths, or kMimeData with "text/plain".
	const gfx::Point where = message->GetPoint("where", {0, 0});
	switch (message->what) {
		case kDropBegin:
			fDropPaths.clear();
			fDropText.clear();
			fDropTextSeen = false;
			return;
		case kDropPosition:
			_DragMoved(where);
			return;
		case kDropFile:
			if (auto path = message->FindString("path"))
				fDropPaths.emplace_back(*path);
			return;
		case kDropText:
			if (auto text = message->FindString("text")) {
				fDropText += *text;
				fDropTextSeen = true;
			}
			return;
		default:
			break;
	}

	// kDropComplete: the drag ends over the window
	BMessage drag(kSimpleData);
	if (fDragView != nullptr) {
		fDragView->MouseMoved(fDragView->ConvertFromWindow(where), BMouseTransit::Exited,
			&drag);
		fDragView = nullptr;
	}
	if (fDropPaths.empty() && !fDropTextSeen)
		return;

	BMessage dropped(fDropPaths.empty() ? kMimeData : kSimpleData);
	for (const std::string& path : fDropPaths)
		(void)dropped.AddString("paths", path);
	if (fDropPaths.empty()) {
		(void)dropped.AddData("text/plain", BTypeCode::Mime,
			std::as_bytes(std::span(fDropText.data(), fDropText.size())));
	}
	dropped.fDropPoint = gfx::Point{where.x + fFrame.x0, where.y + fFrame.y0};
	fDropPaths.clear();
	fDropText.clear();
	fDropTextSeen = false;

	// to the view under the drop point; its parents and the window get it
	// if it does not handle it (BHandler's next handler)
	if (BView* target = ViewAt(fRoot.get(), where))
		target->MessageReceived(&dropped);
	else
		MessageReceived(&dropped);
}


void
BWindow::_DragMoved(gfx::Point where)
{
	// Haiku: the views get MouseMoved() with the drag message. NewGUI: the
	// platform tells the content only at the drop, so the message has no
	// fields while dragging.
	BMessage drag(kSimpleData);
	BView* under = ViewAt(fRoot.get(), where);
	if (under != fDragView) {
		if (fDragView != nullptr) {
			fDragView->MouseMoved(fDragView->ConvertFromWindow(where), BMouseTransit::Exited,
				&drag);
		}
		fDragView = under;
		if (under != nullptr)
			under->MouseMoved(under->ConvertFromWindow(where), BMouseTransit::Entered, &drag);
	} else if (under != nullptr) {
		under->MouseMoved(under->ConvertFromWindow(where), BMouseTransit::Inside, &drag);
	}
}


void
BWindow::_UpdateCursor(const BView* under)
{
	fCursorView = under;
	std::optional<platform::CursorShape> cursor;
	if (under != nullptr && under->fCursor != nullptr)
		cursor = under->fCursor->Shape();
	detail::CursorState::Default().SetViewCursor(cursor);
}


void
BWindow::_ViewCursorChanged(const BView* view)
{
	// Haiku's AS_SET_VIEW_CURSOR: shown at once under the mouse
	if (view == fCursorView)
		_UpdateCursor(view);
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
	if (message->HasPoint("where"))
		fLastMouse = screen;
	if (message->what == kMouseMoved)
		detail::CursorState::Default().MouseMoved();

	// While menus are open, their tracking gets every mouse event.
	if (fMenuTracking != nullptr) {
		// the application's cursor over menus (their views have none)
		if (message->what == kMouseMoved)
			_UpdateCursor(nullptr);
		const auto buttons = static_cast<std::uint32_t>(message->GetInt32("buttons",
			message->what == kMouseUp ? 0 : static_cast<std::int32_t>(fButtons)));
		fButtons = buttons;
		fMenuTracking->TrackMouse(message->what, screen, buttons);
		return;
	}

	BView* under = exited ? nullptr : ViewAt(root, root->ConvertFromWindow(where));
	if (message->what == kMouseMoved) {
		_MouseMovedForToolTip(screen);
		// the cursor of the view under the mouse, also while another one
		// gets the events (Haiku's Window::MouseMoved())
		_UpdateCursor(under);
	}

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
			_CheckToolTipOwner(under);
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
	// A key hides a tooltip that is not sticky (Haiku's ToolTipView::KeyDown()).
	if (message->what == kKeyDown && fToolTip != nullptr)
		fToolTip->KeyDown();
	// While menus are open, their tracking gets the keys first.
	if (fMenuTracking != nullptr && fMenuTracking->TrackKey(message->what, bytes))
		return;
	// Then the window's own keys (Haiku's _HandleKeyDown()).
	if (message->what == kKeyDown && _HandleKeyDown(message))
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
	const TextSystem& text = TextSystem::Default();
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
		_AddFonts(text);
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
	_AddFonts(text);
	fBackend->BeginFrame(*popup.surface, Palette::Default().Get(BColorWhich::MenuBackground));
	fBackend->Render(*popup.lastFrame, {0, 0});
	fBackend->EndFrame(*popup.surface);

	if (popup.platformWindow != nullptr)
		Present(*popup.platformWindow, *popup.surface, popup.dirty, popup.scale);
	popup.dirty.clear();
}


// The backend draws the glyph runs of the text system, so it needs its
// fonts: all of them for a new text system, then those it has loaded
// since (fallbacks found while recording the frame).
void
BWindow::_AddFonts(const TextSystem& text)
{
	if (fFontsOf != &text) {
		fFontsOf = &text;
		fFontsAdded = 0;
	}
	const std::vector<FontFile> fonts = text.Fonts();
	for (std::size_t i = fFontsAdded; i < fonts.size(); i++)
		(void)fBackend->AddFont(fonts[i].id, fonts[i].data, fonts[i].faceIndex);
	fFontsAdded = fonts.size();
}


// #pragma mark - snapshots


namespace {


// \a pixels of a CPU surface as a B_RGBA32 bitmap (B, G, R, A, not
// premultiplied), rounded as the test kit's Image::FromCpuPixels().
std::expected<BBitmap, Error>
BitmapOf(const gfx::CpuPixels& pixels)
{
	BBitmap bitmap({0, 0, static_cast<float>(pixels.width), static_cast<float>(pixels.height)},
		BColorSpace::RGBA32);
	if (!bitmap.IsValid())
		return Failure(ErrorCode::OutOfRange, "the snapshot is too large");
	const bool bgra = pixels.format == gfx::PixelFormat::Bgra8Premul;
	const std::span<std::uint8_t> bits = bitmap.Bits();
	const auto bytesPerRow = static_cast<std::size_t>(bitmap.BytesPerRow());
	for (std::int32_t y = 0; y < pixels.height; y++) {
		const std::byte* row = pixels.bytes.data() + static_cast<std::size_t>(y) * pixels.stride;
		std::uint8_t* out = bits.data() + static_cast<std::size_t>(y) * bytesPerRow;
		for (std::int32_t x = 0; x < pixels.width; x++) {
			const std::byte* p = row + static_cast<std::size_t>(x) * 4;
			const int alpha = static_cast<int>(p[3]);
			const auto straight = [alpha](std::byte value) {
				if (alpha == 0)
					return std::uint8_t{0};
				const int unpremultiplied = (static_cast<int>(value) * 255 + alpha / 2) / alpha;
				return static_cast<std::uint8_t>(std::min(unpremultiplied, 255));
			};
			const std::byte red = bgra ? p[2] : p[0];
			const std::byte blue = bgra ? p[0] : p[2];
			std::uint8_t* q = out + static_cast<std::size_t>(x) * 4;
			q[0] = straight(blue);
			q[1] = straight(p[1]);
			q[2] = straight(red);
			q[3] = static_cast<std::uint8_t>(alpha);
		}
	}
	return bitmap;
}


// A future that is ready with \a value.
std::future<std::expected<BBitmap, Error>>
Ready(std::expected<BBitmap, Error> value)
{
	std::promise<std::expected<BBitmap, Error>> promise;
	promise.set_value(std::move(value));
	return promise.get_future();
}


// \a work's result, run on the thread of the window \a window: at once on
// its own thread, else from a thread of its own that waits for the window.
std::future<std::expected<BBitmap, Error>>
OnWindowThread(const BMessenger& window,
	std::function<std::expected<BBitmap, Error>(BWindow&)> work)
{
	return std::async(std::launch::async, [window, work = std::move(work)] {
		std::expected<BBitmap, Error> result = Failure(ErrorCode::NotFound,
			"the window is gone");
		auto ran = RunOnLooper(window, [&](BLooper& looper) {
			if (auto* target = dynamic_cast<BWindow*>(&looper))
				result = work(*target);
		}, BMessenger::kInfiniteTimeout);
		if (!ran.has_value())
			return std::expected<BBitmap, Error>(std::unexpected(ran.error()));
		return result;
	});
}


} // namespace


std::future<std::expected<BBitmap, Error>>
BWindow::Snapshot(const BSnapshotOptions& options)
{
	if (std::this_thread::get_id() == Thread())
		return Ready(_Snapshot(options, nullptr));
	return Snapshot(BMessenger(this), options);
}


std::future<std::expected<BBitmap, Error>>
BWindow::Snapshot(const BMessenger& window, const BSnapshotOptions& options)
{
	return OnWindowThread(window, [options](BWindow& target) {
		return target._Snapshot(options, nullptr);
	});
}


std::future<std::expected<BBitmap, Error>>
BWindow::CaptureFrame()
{
	if (std::this_thread::get_id() == Thread())
		return Ready(_CaptureFrame());
	return CaptureFrame(BMessenger(this));
}


std::future<std::expected<BBitmap, Error>>
BWindow::CaptureFrame(const BMessenger& window)
{
	return OnWindowThread(window, [](BWindow& target) { return target._CaptureFrame(); });
}


// The window, or \a view and its children, rendered anew into an
// offscreen CPU surface (specification 19.1, level a).
std::expected<BBitmap, Error>
BWindow::_Snapshot(const BSnapshotOptions& options, BView* view)
{
	// S1: pending layout and drawing first
	UpdateIfNeeded();
	if (view != nullptr && !view->IsLayoutValid())
		view->Layout(false);

	std::unique_ptr<gfx::Backend> own;
	gfx::Backend* backend = fBackend.get();
	if (!options.backend.empty() || backend == nullptr) {
		auto made = options.backend.empty()
			? gfx::BackendRegistry::Default().CreatePreferred({})
			: gfx::BackendRegistry::Default().Create(options.backend);
		if (!made.has_value())
			return std::unexpected(made.error());
		own = std::move(*made);
		backend = own.get();
	}
	const TextSystem& text = TextSystem::Default();
	if (own != nullptr) {
		for (const FontFile& font : text.Fonts())
			(void)own->AddFont(font.id, font.data, font.faceIndex);
	}

	const float scale = options.scale > 0 ? options.scale : fScale;
	const gfx::Caps caps = backend->Capabilities();
	const DrawEnvironment environment{caps, Palette::Default(), text, scale};
	BView* root = view != nullptr ? view : fRoot.get();
	const gfx::Rect bounds = view != nullptr ? view->Bounds() : Bounds();
	const gfx::Rect area = options.area.value_or(bounds);
	if (!(area.Width() > 0) || !(area.Height() > 0))
		return Failure(ErrorCode::InvalidArgument, "an empty area");

	// The window as its frames have it, then its popups at their places on
	// the screen, each on its menu background.
	gfx::Canvas canvas({area.x0, area.y0, area.x1, area.y1});
	canvas.DrawList(root->RecordTree(environment), {0, 0});
	if (view == nullptr && options.includePopups) {
		for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
			const gfx::Rect frame = popup->root->Frame();
			const gfx::Point at{frame.x0 - fFrame.x0, frame.y0 - fFrame.y0};
			canvas.FillRect({at.x, at.y, at.x + frame.Width(), at.y + frame.Height()},
				gfx::Paint{.color = Palette::Default().Get(BColorWhich::MenuBackground)});
			canvas.DrawList(popup->root->RecordTree(environment), at);
		}
	}
	const gfx::DisplayListRef list = canvas.Finish();
	// fonts the recording loaded (fallbacks) for the window's backend too
	if (own == nullptr)
		_AddFonts(text);
	else {
		const std::vector<FontFile> fonts = text.Fonts();
		for (const FontFile& font : fonts)
			(void)own->AddFont(font.id, font.data, font.faceIndex);
	}

	const gfx::Size device{std::ceil(area.Width() * scale), std::ceil(area.Height() * scale)};
	const gfx::SurfaceRef surface = backend->CreateOffscreenSurface(device, scale);
	if (surface == nullptr)
		return Failure(ErrorCode::NotSupported, "the backend has no offscreen surfaces");
	backend->BeginFrame(*surface, Palette::Default().Get(BColorWhich::PanelBackground));
	backend->Render(*list, {-area.x0, -area.y0});
	backend->EndFrame(*surface);
	const std::optional<gfx::CpuPixels> pixels = surface->Pixels();
	if (!pixels.has_value())
		return Failure(ErrorCode::NotSupported, "the backend gives no CPU pixels");
	return BitmapOf(*pixels);
}


// The frame shown last (specification 19.1, level b).
std::expected<BBitmap, Error>
BWindow::_CaptureFrame()
{
	if (fSurface == nullptr || fFrameCount == 0)
		return Failure(ErrorCode::NotFound, "no frame shown yet");
	const std::optional<gfx::CpuPixels> pixels = fSurface->Pixels();
	if (!pixels.has_value())
		return Failure(ErrorCode::NotSupported, "the window's surface has no CPU pixels");
	return BitmapOf(*pixels);
}


// #pragma mark - menus


BWindow*
BWindow::_DispatchingWindow()
{
	return sDispatchingWindow;
}


void
BWindow::_DisposeLater(std::unique_ptr<BView> view)
{
	fDisposals.push_back(std::move(view));
}


void
BWindow::_SetTextInputArea(gfx::Rect rect, int32 cursor)
{
	if (fPlatformWindow != nullptr)
		fPlatformWindow->SetTextInputArea(rect, cursor);
}


void
BWindow::_ClearComposition()
{
	if (fPlatformWindow != nullptr)
		fPlatformWindow->ClearComposition();
}


// #pragma mark - shortcuts


void
BWindow::AddShortcut(std::uint32_t key, BModifierFlags modifiers,
	std::unique_ptr<BMessage> message)
{
	AddShortcut(key, modifiers, std::move(message), this);
}


void
BWindow::AddShortcut(std::uint32_t key, BModifierFlags modifiers,
	std::unique_ptr<BMessage> message, BHandler* target)
{
	if (message == nullptr)
		return;

	auto shortcut = std::make_unique<Shortcut>();
	shortcut->fKey = Shortcut::PrepareKey(key);
	shortcut->fModifiers = Shortcut::PrepareModifiers(modifiers);
	shortcut->fMessage = std::move(message);
	shortcut->fTarget = target;

	// removes the shortcut if it already exists!
	RemoveShortcut(shortcut->fKey, shortcut->fModifiers.Has(BModifier::Command)
		? shortcut->fModifiers : shortcut->fModifiers | BModifier::NoCommand);
	fShortcuts.push_back(std::move(shortcut));
}


void
BWindow::RemoveShortcut(std::uint32_t key, BModifierFlags modifiers)
{
	Shortcut* shortcut = _FindShortcut(key, modifiers);
	if (shortcut != nullptr) {
		std::erase_if(fShortcuts,
			[shortcut](const std::unique_ptr<Shortcut>& entry) { return entry.get() == shortcut; });
	} else if (Shortcut::PrepareKey(key) == 'Q'
		&& Shortcut::PrepareModifiers(modifiers) == BModifierFlags(BModifier::Command)) {
		fNoQuitShortcut = true; // the quit shortcut is a fake shortcut
	}
}


bool
BWindow::HasShortcut(std::uint32_t key, BModifierFlags modifiers) const
{
	return _FindShortcut(key, modifiers) != nullptr;
}


void
BWindow::SetKeyMenuBar(BMenuBar* bar)
{
	fKeyMenuBar = bar;
}


BMenuBar*
BWindow::KeyMenuBar() const
{
	return fKeyMenuBar;
}


void
BWindow::_AddShortcut(std::uint32_t* key, BModifierFlags* modifiers, BMenuItem* item)
{
	auto shortcut = std::make_unique<Shortcut>();
	shortcut->fKey = Shortcut::PrepareKey(*key);
	shortcut->fModifiers = Shortcut::PrepareModifiers(*modifiers);
	shortcut->fMenuItem = item;

	// removes the shortcut if it already exists!
	RemoveShortcut(shortcut->fKey, shortcut->fModifiers.Has(BModifier::Command)
		? shortcut->fModifiers : shortcut->fModifiers | BModifier::NoCommand);

	// pass the prepared key and modifiers back to caller (Haiku's
	// Shortcut::Modifiers(): with B_NO_COMMAND_KEY if without Command)
	*key = shortcut->fKey;
	*modifiers = shortcut->fModifiers.Has(BModifier::Command)
		? shortcut->fModifiers : shortcut->fModifiers | BModifier::NoCommand;
	fShortcuts.push_back(std::move(shortcut));
}


BWindow::Shortcut*
BWindow::_FindShortcut(std::uint32_t key, BModifierFlags modifiers) const
{
	key = Shortcut::PrepareKey(key);
	const BModifierFlags prepared = Shortcut::PrepareModifiers(modifiers);
	for (const std::unique_ptr<Shortcut>& shortcut : fShortcuts) {
		if (shortcut->fKey == key && shortcut->fModifiers == prepared)
			return shortcut.get();
	}
	return nullptr;
}


bool
BWindow::_HandleKeyDown(BMessage* event)
{
	// Ported from Haiku: src/kits/interface/Window.cpp (commit b1d497bb69),
	// the menu bar key, Command+Q and the shortcuts.
	const std::string_view bytes = event->GetString("bytes", "");
	if (bytes.empty())
		return false;

	const std::uint32_t key = Shortcut::PrepareKey(static_cast<unsigned char>(bytes[0]));
	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(event->GetInt32("modifiers", 0)));

	// handle BMenuBar key
	if (key == static_cast<std::uint32_t>(keys::kEscape) && modifiers.Has(BModifier::Command)
		&& fKeyMenuBar != nullptr) {
		fKeyMenuBar->StartMenuBar(0, true, false, nullptr);
		return true;
	}

	// Special handling for Command+q
	if (modifiers.Has(BModifier::Command)) {
		// Command+q has been pressed, so, we will quit
		// the shortcut mechanism doesn't allow handlers outside the window
		if (!fNoQuitShortcut && key == 'Q') {
			BMessage message(kQuitRequested);
			(void)message.AddBool("shortcut", true);
			(void)AppMessenger().SendMessage(message);
			return true;
		}
	}

	// Handle shortcuts
	{
		// Pretend that the user opened a menu, to give the subclass a
		// chance to update its menus. This may install new shortcuts,
		// which is why we have to call it here, before trying to find
		// a shortcut for the given key.
		// Only do this if Command key is down, it's too expensive to
		// do this on every key press.
		if (modifiers.Has(BModifier::Command))
			MenusBeginning();

		Shortcut* shortcut = _FindShortcut(key, modifiers.Has(BModifier::Command)
			? modifiers : modifiers | BModifier::NoCommand);
		if (shortcut != nullptr) {
			if (shortcut->fMenuItem != nullptr) {
				BMenu* menu = shortcut->fMenuItem->Menu();
				if (menu != nullptr && shortcut->fMenuItem->IsEnabled()) {
					(void)shortcut->fMenuItem->Invoke();
				} else {
					// Process disabled shortcuts as if they did not exist.
					// (This lets B_NO_COMMAND_KEY shortcuts fall back to
					// regular key events.)
					shortcut = nullptr;
				}
			} else {
				BHandler* target = shortcut->fTarget;
				if (target == nullptr)
					target = CurrentFocus();
				BMessage message(*shortcut->fMessage);
				const auto now = Clock::Default().Now().count();
				if (message.HasInt64("when"))
					(void)message.ReplaceInt64("when", now);
				else
					(void)message.AddInt64("when", now);
				if (message.HasBool("shortcut"))
					(void)message.ReplaceBool("shortcut", true);
				else
					(void)message.AddBool("shortcut", true);
				(void)PostMessage(message, target != nullptr ? target : this);
			}
		}

		if (modifiers.Has(BModifier::Command))
			MenusEnded();

		if (shortcut != nullptr)
			return true;
	}

	// we always eat the event if the command key was pressed
	return modifiers.Has(BModifier::Command);
}


} // namespace ngui
