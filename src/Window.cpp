/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/Window.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <future>
#include <mutex>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Application.h>
#include <ngui/Palette.h>
#include <ngui/PropertyInfo.h>
#include <ngui/Button.h>
#include <ngui/Cursor.h>
#include <ngui/LookRegistry.h>
#include <ngui/MenuBar.h>
#include <ngui/SeparatorItem.h>
#include <ngui/MenuItem.h>
#include <ngui/MessageRunner.h>
#include <ngui/TextSystem.h>
#include <ngui/TextView.h>
#include <ngui/ToolTip.h>
#include <ngui/gfx/BackendRegistry.h>
#include <ngui/gfx/Canvas.h>

#include "LooperPort.h"
#include "CursorState.h"
#include "MenuPrivate.h"
#include "ToolTipPopup.h"
#include "ScriptingSupport.h"
#include "DragData.h"
#include "decor/Decorator.h"
#include "WindowList.h"
#include "record/Recorder.h"


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
// their titles, in the same order (WindowEntries())
std::vector<std::string> sWindowTitles;
// their serials, in the same order, and the next one
std::vector<std::uint64_t> sWindowSerials;
std::uint64_t sNextWindowSerial = 0;

// Where they are on the screen, in the same order: for a drag from one of
// them, which must find the window under the mouse (module P4).
struct WindowPlace {
	gfx::Rect		frame = {0, 0, 0, 0};
	bool			shown = false;
	std::uint64_t	front = 0;	// larger: activated or shown later
};
std::vector<WindowPlace> sWindowPlaces;
std::uint64_t sNextFront = 1;

// A drag of the application over a window (DragMessage()): to the window
// under the mouse, with "where" (screen), "message", "offset", "reply"
// (BMessenger) and "phase": 0 over it, 1 dropped on it, 2 gone from it.
constexpr std::uint32_t kDragTrack = FourCharCode("_DTK");
constexpr std::int32_t kDragOver = 0;
constexpr std::int32_t kDragDropped = 1;
constexpr std::int32_t kDragGone = 2;


// Where the replies to a running drag go, by the drag's number ("drag" of
// kDragTrack): a BMessage carries no messenger.
std::mutex sDragRepliesLock;
std::map<std::int64_t, BMessenger> sDragReplies;
std::int64_t sNextDrag = 1;


// The window of the application in front at \a screen, if any.
BMessenger
WindowAt(gfx::Point screen)
{
	std::scoped_lock lock(sWindowsLock);
	BMessenger found;
	std::uint64_t front = 0;
	for (std::size_t i = 0; i < sWindows.size(); i++) {
		const WindowPlace& place = sWindowPlaces[i];
		if (place.shown && place.frame.Contains(screen) && place.front >= front) {
			found = sWindows[i];
			front = place.front;
		}
	}
	return found;
}


// What of \a message goes to other applications (module P4): its MIME
// fields, and its "paths" as a text/uri-list.
std::vector<platform::DragItem>
DragItemsOf(const BMessage& message)
{
	std::vector<platform::DragItem> items;
	for (const BFieldInfo& field : message.Fields(BTypeCode::Mime)) {
		if (auto data = message.FindData(field.name, BTypeCode::Mime); data.has_value())
			items.push_back({field.name, std::vector<std::byte>(data->begin(), data->end())});
	}
	std::string uris;
	for (std::int32_t i = 0; auto path = message.FindString("paths", i); i++)
		uris += detail::FileUri(*path) + "\r\n";
	if (!uris.empty() && !message.HasData("text/uri-list", BTypeCode::Mime)) {
		const auto* bytes = reinterpret_cast<const std::byte*>(uris.data());
		items.push_back({"text/uri-list", std::vector<std::byte>(bytes, bytes + uris.size())});
	}
	return items;
}

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


// What a drag started in this window keeps (DragMessage(), module P4).
struct BWindow::Drag {
			BMessage			message;
			BMessenger			replyTo;
			gfx::Point			offset;		// the mouse in the image
			std::int64_t		number = 0;	// its replyTo in sDragReplies
			BView*				image = nullptr;	// its popup, if it has one
			BMessenger			over;		// the window the mouse is over
};


// The mouse pressed on the frame NewGUI draws (Haiku's DefaultWindowBehaviour
// and its states).
struct BWindow::FrameTracking {
	enum class Action : std::uint8_t {
		Drag,			// DragState: the window moves
		Resize,			// ResizeState: the right bottom corner
		ResizeBorder,	// ResizeBorderState: the borders by the secondary button
		SlideTab,		// SlideTabState: the tab along the top
		Button			// DecoratorButtonState: close or zoom
	};

			Action				action = Action::Drag;
			detail::Decorator::Region region = detail::Decorator::Region::None;
			gfx::Point			start;		// the mouse on the screen
			gfx::Rect			frame;		// the content area then
			float				tabOffset = 0;
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
		sWindowTitles.push_back(fTitle);
		fSerial = sNextWindowSerial++;
		sWindowSerials.push_back(fSerial);
		sWindowPlaces.push_back({frame, false, 0});
	}
	fLook = LookRegistry::DefaultLook();
	fPalette = Palette::Current();
	fRoot = std::make_unique<BView>(Bounds(), "root", kFollowAll, BViewFlags());
	fRoot->SetViewUIColor(BColorWhich::PanelBackground);
	fRoot->_Attach(this);

	if (platform::Platform* platform = platform::Current()) {
		// NewGUI draws the frame where it is asked to and can (the look has
		// a decorator, the window may be transparent)
		if (BApplication::WindowFrames() == BWindowFrames::Look
			&& look != BWindowLook::NoBorder && platform->HasTransparentWindows()) {
			fDecorator = detail::MakeDecorator(fLook->Info().id, TextSystem::Default());
		}
		if (fDecorator != nullptr) {
			fDecorator->SetLook(look == BWindowLook::Bordered
				? detail::Decorator::Look::Bordered : detail::Decorator::Look::Titled);
			fDecorator->SetFlags(!flags.Has(BWindowFlag::NotClosable),
				!flags.Has(BWindowFlag::NotZoomable), !flags.Has(BWindowFlag::NotResizable));
			fDecorator->SetTitle(fTitle);
			fDecorator->SetContentSize(frame.Width(), frame.Height());
		}
		platform::WindowSpec spec;
		spec.title = fTitle;
		spec.frame = _PlatformFrame(frame);
		spec.decorated = look != BWindowLook::NoBorder && fDecorator == nullptr;
		spec.transparent = fDecorator != nullptr;
		spec.resizable = !flags.Has(BWindowFlag::NotResizable);
		spec.target = BMessenger(this);
		auto window = platform->CreateWindow(spec);
		if (window.has_value()) {
			fPlatformWindow = std::move(*window);
			fScale = fPlatformWindow->Scale();
		}
		if (fDecorator != nullptr)
			_FrameChanged();
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
		const auto found = std::ranges::find(sWindows, self);
		if (found != sWindows.end()) {
			sWindowTitles.erase(sWindowTitles.begin() + (found - sWindows.begin()));
			sWindowSerials.erase(sWindowSerials.begin() + (found - sWindows.begin()));
			sWindowPlaces.erase(sWindowPlaces.begin() + (found - sWindows.begin()));
			sWindows.erase(found);
		}
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
	// a drag from here ends; its image is one of the popups
	_EndDrag();
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
	fLook = fOwnLook != nullptr ? fOwnLook : LookRegistry::DefaultLook();
	// the colors too: one palette for the whole frame
	fPalette = Palette::Current();
}


const Palette&
BWindow::_FramePalette() const
{
	return fPalette;
}


std::expected<void, Error>
BWindow::SetControlLook(std::string_view id)
{
	std::shared_ptr<const ControlLook> look;
	if (!id.empty()) {
		const gfx::Caps caps = fBackend != nullptr ? fBackend->Capabilities()
			: LookRegistry::Default().BackendCaps();
		auto found = LookRegistry::Default().Find(id, caps);
		if (!found.has_value())
			return std::unexpected(found.error());
		look = std::move(*found);
	}
	if (look == fOwnLook)
		return {};
	fOwnLook = std::move(look);
	_LookChanged();
	return {};
}


// Rule L11: the metrics may have changed, so the layout is redone, and every
// view records its DisplayList anew with the new look, popups included.
void
BWindow::_LookChanged()
{
	_TakeLook();
	// NewGUI's frame follows the look, if the new one has a decorator
	if (fDecorator != nullptr) {
		std::unique_ptr<detail::Decorator> next = detail::MakeDecorator(fLook->Info().id,
			TextSystem::Default());
		if (next != nullptr) {
			next->SetLook(fDecorator->GetLook());
			next->SetFlags(fDecorator->IsClosable(), fDecorator->IsZoomable(),
				fDecorator->IsResizable());
			next->SetTitle(fDecorator->Title());
			next->SetFocus(fDecorator->IsFocus());
			next->SetContentSize(fFrame.Width(), fFrame.Height());
			fDecorator = std::move(next);
			// the content stays where it is; the window around it changes
			if (fPlatformWindow != nullptr) {
				const gfx::Rect outer = _PlatformFrame(fFrame);
				fPlatformWindow->MoveTo({outer.x0, outer.y0});
				fPlatformWindow->ResizeTo({outer.Width(), outer.Height()});
				SetSizeLimits(fMinSize.w, fMaxSize.w, fMinSize.h, fMaxSize.h);
			}
			fSurface.reset();
			_FrameChanged();
		}
	}
	fRoot->InvalidateLayout(true);
	ForEachView(fRoot.get(), [](BView* view) { view->Invalidate(); });
	for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
		popup->root->InvalidateLayout(true);
		ForEachView(popup->root.get(), [](BView* view) { view->Invalidate(); });
	}
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
	if (fPlatformWindow != nullptr) {
		// with NewGUI's frame, the limits of the whole window
		const gfx::Rect outer = _PlatformFrame(fFrame);
		const float extraWidth = outer.Width() - fFrame.Width();
		const float extraHeight = outer.Height() - fFrame.Height();
		fPlatformWindow->SetSizeLimits({fMinSize.w + extraWidth, fMinSize.h + extraHeight},
			{fMaxSize.w + extraWidth, fMaxSize.h + extraHeight});
	}

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
		_PublishPlace(true);
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
		_PublishPlace(false);
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
	if (fPlatformWindow != nullptr) {
		const gfx::Point offset = _ContentOffset();
		fPlatformWindow->MoveTo({where.x - offset.x, where.y - offset.y});
	}
	const gfx::Point old = {fFrame.x0, fFrame.y0};
	fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
	_PublishPlace(false);
	if (old.x != where.x || old.y != where.y)
		FrameMoved(where);
}


void
BWindow::ResizeTo(float width, float height)
{
	// what the app_server does: the size stays within the limits
	width = std::clamp(width, fMinSize.w, fMaxSize.w);
	height = std::clamp(height, fMinSize.h, fMaxSize.h);
	if (fDecorator != nullptr) {
		const gfx::Point before = _ContentOffset();
		fDecorator->SetContentSize(width, height);
		// a narrow window's tab may change where the content lies
		const gfx::Point after = _ContentOffset();
		if (fPlatformWindow != nullptr && (before.x != after.x || before.y != after.y))
			fPlatformWindow->MoveTo({fFrame.x0 - after.x, fFrame.y0 - after.y});
	}
	if (fPlatformWindow != nullptr) {
		const gfx::Rect outer = _PlatformFrame({0, 0, width, height});
		fPlatformWindow->ResizeTo({outer.Width(), outer.Height()});
	}
	_Resized(width, height, fScale);
	// the frame of the new size
	_FrameChanged();
}


void
BWindow::Zoom()
{
	// Haiku's BWindow::Zoom(): the smallest of the zoom limits, the size
	// limits and the screen (NewGUI: the screen the window is on, without
	// the system's panels in place of Deskbar)
	float maxZoomWidth = std::min(fMaxZoom.w, fMaxSize.w);
	float maxZoomHeight = std::min(fMaxZoom.h, fMaxSize.h);

	platform::Platform* platform = platform::Current();
	if (platform == nullptr)
		return;
	const std::vector<platform::DisplayInfo> displays = platform->Displays();
	if (displays.empty())
		return;
	const gfx::Point center = {(fFrame.x0 + fFrame.x1) / 2, (fFrame.y0 + fFrame.y1) / 2};
	const platform::DisplayInfo* screen = &displays.front();
	for (const platform::DisplayInfo& display : displays) {
		if (display.frame.Contains(center))
			screen = &display;
	}
	// NewGUI: the part free of the system's panels, as Haiku removes the
	// area taken up by Deskbar
	const gfx::Rect screenFrame = screen->usable.Width() > 0 && screen->usable.Height() > 0
		? screen->usable : screen->frame;
	maxZoomWidth = std::min(maxZoomWidth, screenFrame.Width());
	maxZoomHeight = std::min(maxZoomHeight, screenFrame.Height());

	gfx::Rect zoomArea = screenFrame;

	// remove the area taken up by the tab and border
	const float borderWidth = fDecorator != nullptr ? fDecorator->BorderWidth() : 0;
	const float tabHeight = fDecorator != nullptr ? fDecorator->TabHeight() : 0;
	zoomArea.x0 += borderWidth;
	zoomArea.y0 += borderWidth + tabHeight;
	zoomArea.x1 -= borderWidth;
	zoomArea.y1 -= borderWidth;

	// inset towards center (NewGUI: to the limit exactly, so that a second
	// Zoom() finds the size it gave and puts the window back)
	if (zoomArea.Height() > maxZoomHeight) {
		zoomArea.y0 += std::floor((zoomArea.Height() - maxZoomHeight) / 2);
		zoomArea.y1 = zoomArea.y0 + maxZoomHeight;
	}
	if (zoomArea.Width() > maxZoomWidth) {
		zoomArea.x0 += std::floor((zoomArea.Width() - maxZoomWidth) / 2);
		zoomArea.x1 = zoomArea.x0 + maxZoomWidth;
	}

	// Un-Zoom
	if (fPreviousFrame.has_value() && fFrame.Width() == zoomArea.Width()
		&& fFrame.Height() == zoomArea.Height()) {
		// already zoomed!
		const gfx::Rect previous = *fPreviousFrame;
		Zoom({previous.x0, previous.y0}, previous.Width(), previous.Height());
		return;
	}

	// Zoom: remember fFrame for later "unzooming"
	fPreviousFrame = fFrame;
	Zoom({zoomArea.x0, zoomArea.y0}, zoomArea.Width(), zoomArea.Height());
}


void
BWindow::Zoom(gfx::Point origin, float width, float height)
{
	// the default implementation of this hook function
	// just does the obvious:
	MoveTo(origin);
	ResizeTo(width, height);
}


void
BWindow::SetZoomLimits(float maxWidth, float maxHeight)
{
	fMaxZoom = {maxWidth, maxHeight};
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
	{
		const BMessenger self(this);
		std::scoped_lock lock(sWindowsLock);
		const auto found = std::ranges::find(sWindows, self);
		if (found != sWindows.end())
			sWindowTitles[static_cast<std::size_t>(found - sWindows.begin())] = fTitle;
	}
	if (fPlatformWindow != nullptr)
		fPlatformWindow->SetTitle(title);
	if (fDecorator != nullptr) {
		fDecorator->SetTitle(fTitle);
		_FrameChanged();
	}
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
			// with NewGUI's frame: the content's coordinates, and the frame
			// takes what is meant for it
			_ToContent(message);
			if (fDecorator != nullptr && _FrameMouse(message))
				return;
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
		case platform::kDropData:
			_ToContent(message);
			_DropEvent(message);
			return;
		case kDragTrack:
			_DragTrack(message);
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
		{
			// the whole window with NewGUI's frame
			const gfx::Rect outer = _PlatformFrame(fFrame);
			const float width = message->GetFloat("width", outer.Width())
				- (outer.Width() - fFrame.Width());
			const float height = message->GetFloat("height", outer.Height())
				- (outer.Height() - fFrame.Height());
			if (fDecorator != nullptr)
				fDecorator->SetContentSize(width, height);
			_Resized(width, height, message->GetFloat("be:scale", fScale));
			_FrameChanged();
			return;
		}
		case kWindowMoved:
		{
			const gfx::Point offset = _ContentOffset();
			const gfx::Point outer = message->GetPoint("where",
				{fFrame.x0 - offset.x, fFrame.y0 - offset.y});
			const gfx::Point where = {outer.x + offset.x, outer.y + offset.y};
			fFrame = {where.x, where.y, where.x + fFrame.Width(), where.y + fFrame.Height()};
			_PublishPlace(false);
			FrameMoved(where);
			return;
		}
		case kColorsUpdated:
		{
			// the application's colors changed (B_COLORS_UPDATED): the views
			// hear it and draw anew, the frame NewGUI draws too
			_TakeLook();
			ForEachView(fRoot.get(), [message](BView* view) { view->MessageReceived(message); });
			for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
				ForEachView(popup->root.get(),
					[message](BView* view) { view->MessageReceived(message); });
			}
			if (fDecorator != nullptr)
				_FrameChanged();
			return;
		}
		case kBlendingChanged:
			// the application blends otherwise (specification 24.1): every
			// surface anew, all of it drawn
			fSurface.reset();
			for (const std::unique_ptr<PopupSurface>& popup : fPopups)
				popup->surface.reset();
			fMainUpdateWanted = true;
			_ScheduleUpdate();
			return;
		case kLookChanged:
			// a window with a look of its own keeps it (18.5)
			if (fOwnLook == nullptr)
				_LookChanged();
			return;
		case kAccessibleAction:
			_AccessibleAction(message);
			return;
		case kAnnounce:
			fAnnouncement = message->GetString("text", "");
			fAnnouncementLive = static_cast<AccessibleLive>(message->GetInt32("live",
				static_cast<std::int32_t>(AccessibleLive::Polite)));
			fAnnouncements++;
			_ScheduleUpdate();
			return;
		case kAccessibilityRequested:
			// assistive technology listens: the whole tree at the next update
			fAccessibleTree.reset();
			_ScheduleUpdate();
			return;
		case platform::kSystemMenuInvoked:
			// an item chosen in the system's menu bar, if it still is there
			if (BMenuItem* item = _SystemMenuItem(static_cast<std::uint64_t>(
					message->GetInt64("id", 0)));
					item != nullptr && item->IsEnabled()) {
				(void)item->Invoke();
			}
			return;
		case platform::kSystemMenusBeginning:
			// the items as the program makes them up for the opening menu
			MenusBeginning();
			_UpdateSystemMenuBar();
			return;
		case platform::kSystemMenusEnded:
			MenusEnded();
			return;
		case detail::kRecordWindowState:
			// a recording begins: its frame and scale as it has them
			detail::RecordWindowState(*this, fFrame, fScale);
			return;
		case kWindowActivated:
		{
			fActive = message->GetBool("active", false);
			if (fActive)
				_PublishPlace(true);
			if (!fActive && fMenuTracking != nullptr)
				fMenuTracking->CancelTracking();
			if (fActive) {
				// activated by the window system, but a modal window has the
				// input: it comes to the front
				BMessenger modal;
				if (_BlockedByModal(&modal))
					(void)modal.SendMessage(kActivateRequest);
			}
			if (fDecorator != nullptr) {
				fDecorator->SetFocus(fActive);
				_FrameChanged();
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
			fDropData.MakeEmpty();
			return;
		case platform::kDropData:
			// everything at once, by MIME type (module P4)
			for (const BFieldInfo& field : message->Fields(BTypeCode::Mime)) {
				const auto data = message->FindData(field.name, BTypeCode::Mime);
				if (!data.has_value())
					continue;
				if (field.name == "text/uri-list") {
					const std::string_view list(reinterpret_cast<const char*>(data->data()),
						data->size());
					for (std::string& path : detail::PathsOf(list))
						fDropPaths.push_back(std::move(path));
				}
				(void)fDropData.AddData(field.name, BTypeCode::Mime, *data);
			}
			return;
		case kDropPosition:
			_DragMoved(where, BMessage(kSimpleData));
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
	if (fDropPaths.empty() && !fDropTextSeen && fDropData.IsEmpty())
		return;

	// files as kSimpleData with "paths", other data as kMimeData; the MIME
	// fields of the drop are kept in both
	BMessage dropped(fDropPaths.empty() ? kMimeData : kSimpleData);
	for (const std::string& path : fDropPaths)
		(void)dropped.AddString("paths", path);
	if (fDropTextSeen && !fDropData.HasData("text/plain", BTypeCode::Mime)) {
		(void)dropped.AddData("text/plain", BTypeCode::Mime,
			std::as_bytes(std::span(fDropText.data(), fDropText.size())));
	}
	for (const BFieldInfo& field : fDropData.Fields(BTypeCode::Mime)) {
		if (auto data = fDropData.FindData(field.name, BTypeCode::Mime); data.has_value())
			(void)dropped.AddData(field.name, BTypeCode::Mime, *data);
	}
	fDropData.MakeEmpty();
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
BWindow::_DragMoved(gfx::Point where, const BMessage& drag)
{
	// Haiku: the views get MouseMoved() with the drag message. NewGUI: from
	// another application the platform tells the content only at the drop,
	// so that message has no fields while dragging.
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


namespace {


// The image of a drag, in a popup under the mouse.
class DragImageView final : public BView {
public:
	DragImageView(gfx::Rect frame, std::unique_ptr<BBitmap> image)
		:
		BView(frame, "drag image", kFollowLeftTop, BViewFlag::WillDraw),
		fImage(std::move(image))
	{
	}

	void Draw(DrawContext& context) override
	{
		const gfx::Rect bounds = fImage->Bounds();
		context.canvas.DrawImage(fImage->Image(), bounds, bounds);
	}

private:
	std::unique_ptr<BBitmap> fImage;
};


} // namespace


void
BWindow::_StartDrag(const BMessage& message, std::unique_ptr<BBitmap> image, gfx::Point offset,
	BMessenger replyTo)
{
	// a drag that is running ends first, without a drop
	_EndDrag();
	fDrag = std::make_unique<Drag>();
	fDrag->message = message;
	fDrag->replyTo = std::move(replyTo);
	fDrag->offset = offset;
	{
		std::scoped_lock lock(sDragRepliesLock);
		fDrag->number = sNextDrag++;
		sDragReplies[fDrag->number] = fDrag->replyTo;
	}
	const gfx::Point mouse = fLastMouse.value_or(gfx::Point{fFrame.x0, fFrame.y0});
	if (image != nullptr && image->IsValid()) {
		const gfx::Rect bounds = image->Bounds();
		const gfx::Rect frame{mouse.x - offset.x, mouse.y - offset.y,
			mouse.x - offset.x + bounds.Width(), mouse.y - offset.y + bounds.Height()};
		// a tooltip's popup: it takes no input, the mouse stays the window's
		fDrag->image = _OpenPopup(std::make_unique<DragImageView>(frame, std::move(image)),
			true);
	}
	_DragTo(mouse, false);
}


// The mouse of a drag at \a screen: the image follows, the window under it
// hears of the drag; \a drop ends it there.
void
BWindow::_DragTo(gfx::Point screen, bool drop)
{
	if (fDrag == nullptr)
		return;
	if (fDrag->image != nullptr)
		fDrag->image->MoveTo({screen.x - fDrag->offset.x, screen.y - fDrag->offset.y});
	const BMessenger self(this);
	const BMessenger over = WindowAt(screen);
	const auto send = [this, &self](const BMessenger& window, BMessage& track) {
		// this window at once, in order with its own events
		if (window == self)
			_DragTrack(&track);
		else
			(void)window.SendMessage(track);
	};
	BMessage track(kDragTrack);
	(void)track.AddPoint("where", screen);
	(void)track.AddMessage("message", fDrag->message);
	(void)track.AddPoint("offset", fDrag->offset);
	(void)track.AddInt64("drag", fDrag->number);
	if (fDrag->over.IsValid() && !(fDrag->over == over)) {
		BMessage gone(track);
		(void)gone.AddInt32("phase", kDragGone);
		send(fDrag->over, gone);
	}
	fDrag->over = over;
	// out of the application's windows: to the system, if it takes the data
	if (!over.IsValid() && !drop && fPlatformWindow != nullptr
		&& fPlatformWindow->CanDragToSystem()) {
		std::vector<platform::DragItem> items = DragItemsOf(fDrag->message);
		if (!items.empty() && fPlatformWindow->StartSystemDrag(std::move(items)).has_value()) {
			_EndDrag();
			return;
		}
	}
	(void)track.AddInt32("phase", drop ? kDragDropped : kDragOver);
	if (over.IsValid())
		send(over, track);
	if (drop) {
		// the drop's window takes its replyTo when it gets the drop, which
		// may be after this
		if (over.IsValid())
			fDrag->number = 0;
		fDrag->over = BMessenger();
		_EndDrag();
	}
}


// Ends the drag without a drop: the window it is over hears it is gone.
void
BWindow::_EndDrag()
{
	if (fDrag == nullptr)
		return;
	if (fDrag->over.IsValid()) {
		BMessage gone(kDragTrack);
		(void)gone.AddInt32("phase", kDragGone);
		(void)gone.AddPoint("where", fLastMouse.value_or(gfx::Point{0, 0}));
		(void)gone.AddMessage("message", fDrag->message);
		if (fDrag->over == BMessenger(this))
			_DragTrack(&gone);
		else
			(void)fDrag->over.SendMessage(gone);
	}
	if (fDrag->number != 0) {
		std::scoped_lock lock(sDragRepliesLock);
		sDragReplies.erase(fDrag->number);
	}
	if (fDrag->image != nullptr)
		(void)ClosePopup(fDrag->image);
	fDrag.reset();
}


// A drag of the application over this window (kDragTrack).
void
BWindow::_DragTrack(BMessage* track)
{
	const gfx::Point screen = track->GetPoint("where", {0, 0});
	const gfx::Point where{screen.x - fFrame.x0, screen.y - fFrame.y0};
	const std::int32_t phase = track->GetInt32("phase", kDragOver);
	BMessage drag;
	if (auto found = track->FindMessage("message"); found.has_value())
		drag = std::move(*found);
	if (phase == kDragOver) {
		_DragMoved(where, drag);
		fDragOver = std::move(drag);
		return;
	}
	if (fDragView != nullptr) {
		fDragView->MouseMoved(fDragView->ConvertFromWindow(where), BMouseTransit::Exited,
			fDragOver.has_value() ? &*fDragOver : &drag);
		fDragView = nullptr;
	}
	fDragOver.reset();
	if (phase != kDragDropped)
		return;

	// Haiku's drop: to the view under the mouse, its parents and the window
	// if it does not handle it; replies to the drag's replyTo.
	drag.fDropPoint = screen;
	drag.fDropOffset = track->GetPoint("offset", {0, 0});
	BMessenger replyTo;
	{
		std::scoped_lock lock(sDragRepliesLock);
		const auto found = sDragReplies.find(track->GetInt64("drag", 0));
		if (found != sDragReplies.end()) {
			replyTo = found->second;
			sDragReplies.erase(found);
		}
	}
	if (replyTo.IsValid()) {
		drag.fDelivery = std::make_unique<MessageDelivery>();
		drag.fDelivery->reply = std::make_unique<ReplyToken>(std::weak_ptr<ReplyWaiter>(),
			replyTo);
	}
	if (BView* target = ViewAt(fRoot.get(), where))
		target->MessageReceived(&drag);
	else
		MessageReceived(&drag);
}


void
BWindow::_PublishPlace(bool toFront)
{
	const BMessenger self(this);
	std::scoped_lock lock(sWindowsLock);
	const auto found = std::ranges::find(sWindows, self);
	if (found == sWindows.end())
		return;
	WindowPlace& place = sWindowPlaces[static_cast<std::size_t>(found - sWindows.begin())];
	place.frame = fFrame;
	place.shown = !IsHidden();
	if (toFront)
		place.front = sNextFront++;
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

	// While something is dragged (DragMessage()), the drag gets the moves;
	// the release drops it, then goes on to the views as usual.
	if (fDrag != nullptr && message->HasPoint("where")) {
		if (message->what == kMouseMoved) {
			_DragTo(screen, false);
			return;
		}
		if (message->what == kMouseUp)
			_DragTo(screen, true);
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


void
BWindow::GetAccessibleInfo(AccessibleInfo* info) const
{
	if (info == nullptr)
		return;
	const bool modal = fFeel == BWindowFeel::ModalApp;
	info->role = modal ? AccessibleRole::Dialog : AccessibleRole::Window;
	info->name = fTitle;
	if (modal)
		info->states |= AccessibleState::Modal;
}


void
BWindow::Announce(std::string_view text, AccessibleLive live)
{
	BMessage message(kAnnounce);
	(void)message.AddString("text", text);
	(void)message.AddInt32("live", static_cast<std::int32_t>(live));
	(void)PostMessage(message);
}


AccessibleTree
BWindow::GetAccessibleTree() const
{
	AccessibleTree tree;
	AccessibleNode window;
	window.id = static_cast<std::uint64_t>(static_cast<const BHandler*>(this)->fToken);
	GetAccessibleInfo(&window.info);
	window.bounds = {0, 0, fFrame.Width(), fFrame.Height()};
	tree.nodes.push_back(std::move(window));
	tree.focus = tree.nodes.front().id;

	// the shown views, a node for each whose role is not None, its items
	// first; \a offset moves a popup's views into window coordinates
	std::function<void(const BView*, std::size_t, gfx::Point)> visit;
	const auto addChildren = [&](const BView* view, std::size_t parent, gfx::Point offset) {
		for (std::int32_t i = 0; i < view->CountChildren(); i++)
			visit(view->ChildAt(i), parent, offset);
	};
	visit = [&](const BView* view, std::size_t parent, gfx::Point offset) {
		if (view == nullptr || view->IsHidden())
			return;
		AccessibleNode node;
		view->GetAccessibleInfo(&node.info);
		if (node.info.role == AccessibleRole::None) {
			addChildren(view, parent, offset);
			return;
		}
		const auto token = static_cast<std::uint64_t>(static_cast<const BHandler*>(view)->fToken);
		node.id = token;
		node.bounds = view->ConvertToWindow(view->Bounds()).OffsetByCopy(offset.x, offset.y);
		tree.nodes[parent].children.push_back(node.id);
		const std::size_t index = tree.nodes.size();
		tree.nodes.push_back(std::move(node));
		std::vector<AccessibleItem> items;
		view->GetAccessibleItems(&items);
		// where each item's node is, for the items that lie in it
		std::vector<std::size_t> itemNodes;
		itemNodes.reserve(items.size());
		for (std::size_t i = 0; i < items.size(); i++) {
			AccessibleNode item;
			item.id = (token << 32) | (static_cast<std::uint64_t>(i) + 1);
			item.info = std::move(items[i].info);
			item.bounds = view->ConvertToWindow(items[i].frame).OffsetByCopy(offset.x,
				offset.y);
			const std::int32_t in = items[i].parent;
			const std::size_t parentNode = in >= 0 && static_cast<std::size_t>(in) < i
				? itemNodes[static_cast<std::size_t>(in)] : index;
			tree.nodes[parentNode].children.push_back(item.id);
			itemNodes.push_back(tree.nodes.size());
			tree.nodes.push_back(std::move(item));
		}
		addChildren(view, index, offset);
	};
	if (fRoot != nullptr)
		addChildren(fRoot.get(), 0, {0, 0});
	// open menus (not tooltips), where they are on the screen
	const gfx::Point origin = _SurfaceOrigin(nullptr);
	for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
		// a menu in its frame (MenuFrame), not a tooltip
		bool menu = false;
		ForEachView(popup->root.get(), [&](BView* view) {
			menu = menu || dynamic_cast<const BMenu*>(view) != nullptr;
		});
		if (!menu)
			continue;
		const gfx::Point at = _SurfaceOrigin(popup->root.get());
		visit(popup->root.get(), 0, {at.x - origin.x, at.y - origin.y});
	}

	for (const BView* view = fFocus; view != nullptr; view = view->Parent()) {
		const auto id = static_cast<std::uint64_t>(static_cast<const BHandler*>(view)->fToken);
		if (tree.Find(id) != nullptr) {
			tree.focus = id;
			break;
		}
	}
	// an item of the focused view that has the focus itself (a table's
	// focus row)
	if (tree.focus != tree.nodes.front().id) {
		for (const AccessibleNode& node : tree.nodes) {
			if (node.id >> 32 == tree.focus && node.info.states.Has(AccessibleState::Focused)) {
				tree.focus = node.id;
				break;
			}
		}
	}
	// the last announcement: a node of its own, new for each one
	if (!fAnnouncement.empty()) {
		AccessibleNode announcement;
		announcement.id = (tree.nodes.front().id << 32) | fAnnouncements;
		announcement.info.role = AccessibleRole::Label;
		announcement.info.name = fAnnouncement;
		announcement.info.live = fAnnouncementLive;
		announcement.bounds = {0, 0, 0, 0};
		tree.nodes.front().children.push_back(announcement.id);
		tree.nodes.push_back(std::move(announcement));
	}

	// while menus are tracked, the selected item of the innermost menu
	if (fMenuTracking != nullptr) {
		for (const AccessibleNode& node : tree.nodes) {
			if (node.info.role == AccessibleRole::MenuItem
				&& node.info.states.Has(AccessibleState::Selected)) {
				tree.focus = node.id;
			}
		}
	}
	// only the focus is focused, while the window is active
	for (AccessibleNode& node : tree.nodes) {
		if (IsActive() && node.id == tree.focus && &node != &tree.nodes.front())
			node.info.states |= AccessibleState::Focused;
		else
			node.info.states.Clear(AccessibleState::Focused);
	}
	return tree;
}


BView*
BWindow::_ViewWithToken(std::int32_t token) const
{
	BView* found = nullptr;
	const auto match = [&](BView* view) {
		if (static_cast<const BHandler*>(view)->fToken == token)
			found = view;
	};
	ForEachView(fRoot.get(), match);
	// and the views of open popups (menus)
	for (const std::unique_ptr<PopupSurface>& popup : fPopups)
		ForEachView(popup->root.get(), match);
	return found;
}


// kAccessibleAction: an action of assistive technology for a view.
void
BWindow::_AccessibleAction(BMessage* message)
{
	const std::int64_t node = message->GetInt64("node", 0);
	const auto action = static_cast<AccessibleAction>(message->GetInt32("action", 0));
	std::expected<void, Error> result;
	// a view's token, or its token times 2^32 plus one of its items' index + 1
	const auto id = static_cast<std::uint64_t>(node);
	const std::uint64_t token = id >> 32 != 0 ? id >> 32 : id;
	const auto item = static_cast<std::int64_t>(id >> 32 != 0 ? (id & 0xffffffffu) : 0) - 1;
	BView* view = node > 0 && token <= static_cast<std::uint64_t>(
			std::numeric_limits<std::int32_t>::max())
		? _ViewWithToken(static_cast<std::int32_t>(token)) : nullptr;
	if (view == nullptr)
		result = Failure(ErrorCode::NotFound, "no view is that node");
	else if (item >= 0) {
		result = view->PerformAccessibleItemAction(static_cast<int32>(item), action,
			*message);
	} else
		result = view->PerformAccessibleAction(action, *message);
	// what changed reaches assistive technology at the next update, also
	// when nothing is drawn anew (a selection without the focus)
	_ScheduleUpdate();
	if (message->IsSourceWaiting())
		detail::SendScriptingReply(*message, BMessage(kReply), result);
}


std::vector<BMessenger>
BWindow::_Windows()
{
	std::scoped_lock lock(sWindowsLock);
	return sWindows;
}


std::vector<detail::WindowEntry>
detail::WindowEntries()
{
	std::scoped_lock lock(sWindowsLock);
	std::vector<WindowEntry> entries;
	for (std::size_t i = 0; i < sWindows.size(); i++)
		entries.push_back({sWindows[i], sWindowTitles[i], sWindowSerials[i]});
	return entries;
}


std::uint64_t
detail::NextWindowSerial()
{
	std::scoped_lock lock(sWindowsLock);
	return sNextWindowSerial;
}


std::uint64_t
WindowAccess::Serial(const BWindow& window)
{
	return window.fSerial;
}


std::int32_t
WindowAccess::PopupIndex(const BWindow& window, const BHandler* handler)
{
	for (std::size_t i = 0; i < window.fPopups.size(); i++) {
		if (window.fPopups[i].get() == handler)
			return static_cast<std::int32_t>(i);
	}
	return -1;
}


bool
WindowAccess::MasksInput(const BWindow& window)
{
	for (const BView* view = window.CurrentFocus(); view != nullptr; view = view->Parent()) {
		if (view->IsInputPrivate())
			return true;
		if (const auto* text = dynamic_cast<const BTextView*>(view);
				text != nullptr && text->IsTypingHidden()) {
			return true;
		}
	}
	return false;
}


void
detail::PostToWindows(std::uint32_t what)
{
	std::vector<BMessenger> windows;
	{
		std::scoped_lock lock(sWindowsLock);
		windows = sWindows;
	}
	for (const BMessenger& window : windows)
		(void)window.SendMessage(what);
}


void
BWindow::_Resized(float width, float height, float scale)
{
	const bool sized = width != fFrame.Width() || height != fFrame.Height();
	const bool scaled = scale != fScale;
	if (!sized && !scaled)
		return;
	fFrame = {fFrame.x0, fFrame.y0, fFrame.x0 + width, fFrame.y0 + height};
	_PublishPlace(false);
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


// The window with NewGUI's frame: \a content inside it.
gfx::Rect
BWindow::_PlatformFrame(gfx::Rect content) const
{
	if (fDecorator == nullptr)
		return content;
	const gfx::Rect bounds = fDecorator->Bounds();
	return {content.x0 + bounds.x0, content.y0 + bounds.y0, content.x0 + bounds.x1,
		content.y0 + bounds.y1};
}


// The content's left top corner in the window, (0, 0) without NewGUI's frame.
gfx::Point
BWindow::_ContentOffset() const
{
	if (fDecorator == nullptr)
		return {0, 0};
	const gfx::Rect bounds = fDecorator->Bounds();
	return {-bounds.x0, -bounds.y0};
}


// A position of the window system (in the window) in the content's
// coordinates.
void
BWindow::_ToContent(BMessage* message) const
{
	if (fDecorator == nullptr || !message->HasPoint("where"))
		return;
	const gfx::Point offset = _ContentOffset();
	const gfx::Point where = message->GetPoint("where", {0, 0});
	(void)message->ReplacePoint("where", {where.x - offset.x, where.y - offset.y});
}


// The frame changed (its size, title, focus or a button): it is drawn
// anew, and the window system hears where the content lies.
void
BWindow::_FrameChanged()
{
	if (fDecorator == nullptr)
		return;
	fFrameDirty = true;
	fMainUpdateWanted = true;
	if (fPlatformWindow != nullptr) {
		platform::WindowFrameLayout layout;
		layout.contentOffset = _ContentOffset();
		layout.contentSize = {fFrame.Width(), fFrame.Height()};
		const gfx::Point offset = layout.contentOffset;
		const auto moved = [offset](const gfx::Rect& area) {
			return gfx::Rect{area.x0 + offset.x, area.y0 + offset.y, area.x1 + offset.x,
				area.y1 + offset.y};
		};
		for (const gfx::Rect& area : fDecorator->Footprint())
			layout.shape.push_back(moved(area));
		// for a window system that moves and resizes by itself (Wayland):
		// the buttons for the window, the corner resizes, the rest moves
		for (const gfx::Rect& button : {fDecorator->CloseRect(), fDecorator->ZoomRect()}) {
			if (button.x1 > button.x0 && button.y1 > button.y0)
				layout.areas.push_back({moved(button), platform::WindowFrameHit::Normal});
		}
		if (const gfx::Rect corner = fDecorator->ResizeRect();
				corner.x1 > corner.x0 && corner.y1 > corner.y0) {
			layout.areas.push_back({moved(corner), platform::WindowFrameHit::ResizeBottomRight});
		}
		for (const gfx::Rect& area : layout.shape)
			layout.areas.push_back({area, platform::WindowFrameHit::Move});
		fPlatformWindow->SetFrameLayout(layout);
	}
	_ScheduleUpdate();
}


// \a content inside NewGUI's frame, as the window shows them.
gfx::DisplayListRef
BWindow::_WithFrame(const gfx::DisplayListRef& content, const DrawEnvironment& environment)
{
	const gfx::Rect outer = _PlatformFrame({0, 0, fFrame.Width(), fFrame.Height()});
	const gfx::Point offset = _ContentOffset();
	gfx::Canvas canvas({0, 0, outer.Width(), outer.Height()});
	DrawContext context{canvas, environment.caps, environment.palette, environment.text,
		environment.scale};
	canvas.Save();
	canvas.Translate(offset.x, offset.y);
	fDecorator->Draw(context);
	canvas.Restore();
	if (content != nullptr)
		canvas.DrawList(content, offset);
	return canvas.Finish();
}


// The mouse on NewGUI's frame (Haiku's DefaultWindowBehaviour): true if the
// frame takes \a message, false if it is for the views.
bool
BWindow::_FrameMouse(BMessage* message)
{
	using Region = detail::Decorator::Region;
	using Action = FrameTracking::Action;
	if (!message->HasPoint("where"))
		return false;
	const gfx::Point where = message->GetPoint("where", {0, 0});
	const gfx::Point screen = {where.x + fFrame.x0, where.y + fFrame.y0};

	if (fFrameTracking != nullptr) {
		FrameTracking& tracking = *fFrameTracking;
		const float dx = screen.x - tracking.start.x;
		const float dy = screen.y - tracking.start.y;
		if (message->what == kMouseMoved) {
			switch (tracking.action) {
				case Action::Drag:
					MoveTo({tracking.frame.x0 + dx, tracking.frame.y0 + dy});
					break;
				case Action::Resize:
					ResizeTo(tracking.frame.Width() + dx, tracking.frame.Height() + dy);
					break;
				case Action::ResizeBorder:
				{
					// the borders nearest to where it was pressed
					const Region region = tracking.region;
					const bool left = region == Region::LeftBorder
						|| region == Region::LeftTopCorner || region == Region::LeftBottomCorner;
					const bool right = region == Region::RightBorder
						|| region == Region::RightTopCorner
						|| region == Region::RightBottomCorner;
					const bool top = region == Region::TopBorder || region == Region::Tab
						|| region == Region::CloseButton || region == Region::ZoomButton
						|| region == Region::LeftTopCorner || region == Region::RightTopCorner;
					const bool bottom = region == Region::BottomBorder
						|| region == Region::LeftBottomCorner
						|| region == Region::RightBottomCorner;
					float width = tracking.frame.Width();
					float height = tracking.frame.Height();
					if (left)
						width -= dx;
					else if (right)
						width += dx;
					if (top)
						height -= dy;
					else if (bottom)
						height += dy;
					width = std::clamp(width, fMinSize.w, fMaxSize.w);
					height = std::clamp(height, fMinSize.h, fMaxSize.h);
					const float x = left ? tracking.frame.x1 - width : tracking.frame.x0;
					const float y = top ? tracking.frame.y1 - height : tracking.frame.y0;
					if (x != fFrame.x0 || y != fFrame.y0)
						MoveTo({x, y});
					ResizeTo(width, height);
					break;
				}
				case Action::SlideTab:
					if (fDecorator->SetTabLocation(tracking.tabOffset + dx))
						_FrameChanged();
					break;
				case Action::Button:
				{
					const bool inside = fDecorator->RegionAt(where) == tracking.region;
					if (inside != fDecorator->IsPressed(tracking.region)) {
						fDecorator->SetPressed(tracking.region, inside);
						_FrameChanged();
					}
					break;
				}
			}
			return true;
		}
		if (message->what == kMouseUp) {
			if (message->GetInt32("buttons", 0) != 0)
				return true;
			std::unique_ptr<FrameTracking> ended = std::move(fFrameTracking);
			if (ended->action == Action::Button && fDecorator->IsPressed(ended->region)) {
				fDecorator->SetPressed(ended->region, false);
				_FrameChanged();
				if (ended->region == Region::CloseButton) {
					BMessage quit(kQuitRequested);
					(void)quit.AddInt64("when", Clock::Default().Now().count());
					(void)PostMessage(quit, this);
				} else if (ended->region == Region::ZoomButton) {
					Zoom();
				}
			}
			return true;
		}
		return true;
	}

	if (message->what != kMouseDown)
		return false;
	const Region region = fDecorator->RegionAt(where);
	if (region == Region::None) {
		// beside the tab, where the window is transparent
		return !Bounds().Contains(where);
	}

	// DefaultWindowBehaviour::MouseDown(): the region's action
	const auto buttons = message->GetInt32("buttons", 1);
	const auto modifiers = static_cast<std::uint32_t>(message->GetInt32("modifiers", 0));
	Action action = Action::Drag;
	if ((buttons & 1) != 0) {
		switch (region) {
			case Region::Tab:
				// tab sliding in any case if either shift key is held down
				if ((modifiers & static_cast<std::uint32_t>(BModifier::Shift)) != 0)
					action = Action::SlideTab;
				break;
			case Region::CloseButton:
				if (fDecorator->IsClosable())
					action = Action::Button;
				break;
			case Region::ZoomButton:
				if (fDecorator->IsZoomable())
					action = Action::Button;
				break;
			case Region::RightBottomCorner:
				if (fDecorator->IsResizable())
					action = Action::Resize;
				break;
			default:
				break;
		}
	} else if ((buttons & 2) != 0) {
		if (!fDecorator->IsResizable())
			return true;
		action = Action::ResizeBorder;
	} else {
		return true;
	}

	// activate the window, except for resizing by the borders
	if (action != Action::ResizeBorder && !fActive)
		Activate();

	auto tracking = std::make_unique<FrameTracking>();
	tracking->action = action;
	tracking->region = region;
	tracking->start = screen;
	tracking->frame = fFrame;
	tracking->tabOffset = fDecorator->TabOffset();
	if (action == Action::Button) {
		fDecorator->SetPressed(region, true);
		_FrameChanged();
	}
	fFrameTracking = std::move(tracking);
	return true;
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
	// The platform renders the frames itself (phase G, on the GPU): the
	// window hands over its list.
	const bool platformDraws = fPlatformWindow != nullptr && fPlatformWindow->DrawsLists();
	// NewGUI: a frame of the content area only if it needs one (not when
	// only a popup changed).
	const bool drawMain = fMainUpdateWanted || fFrameCount == 0
		|| (!platformDraws && fSurface == nullptr);
	fMainUpdateWanted = false;
	if (fPlatformWindow != nullptr && IsHidden())
		return;
	if (fBackend == nullptr) {
		auto backend = gfx::BackendRegistry::Default().CreatePreferred({});
		if (!backend.has_value())
			return;
		fBackend = std::move(*backend);
	}
	// the application's way of blending (specification 24.1)
	fBackend->SetBlending(BApplication::Blending());
	const TextSystem& text = TextSystem::Default();
	const gfx::Caps caps = fBackend->Capabilities();

	if (drawMain && platformDraws) {
		const DrawEnvironment environment{caps, fPalette, text, fScale};
		fLastFrame = fRoot->RecordTree(environment);
		fFrameCount++;
		if (fDecorator != nullptr) {
			// NewGUI's frame around the content, on a transparent window
			(void)fPlatformWindow->PresentList(_WithFrame(fLastFrame, environment),
				gfx::Color{0, 0, 0, 0});
		} else {
			(void)fPlatformWindow->PresentList(fLastFrame,
				fPalette.Get(BColorWhich::PanelBackground));
		}
		fDirty.clear();
		fFrameDirty = false;
	} else if (drawMain) {
		if (fSurface == nullptr) {
			gfx::NativeWindow native;
			if (fPlatformWindow != nullptr)
				native = fPlatformWindow->Native();
			// the whole window, with NewGUI's frame
			const gfx::Rect outer = _PlatformFrame(fFrame);
			native.size = {outer.Width(), outer.Height()};
			native.scale = fScale;
			fSurface = fBackend->CreateWindowSurface(native, gfx::SurfaceKind::CpuRaster);
			if (fSurface == nullptr)
				return;
			fFrameDirty = fDecorator != nullptr;
			// Not `fDirty = {Bounds()}`: GCC warns wrongly about the bounds
			// of the initializer list (-Warray-bounds) in optimized builds.
			fDirty.clear();
			fDirty.push_back(Bounds());
		}

		const DrawEnvironment environment{caps, fPalette, text, fScale};
		fLastFrame = fRoot->RecordTree(environment);
		_AddFonts(text);
		fFrameCount++;
		if (fDecorator != nullptr) {
			// NewGUI's frame around the content, on a transparent window
			fBackend->BeginFrame(*fSurface, gfx::Color{0, 0, 0, 0});
			fBackend->Render(*_WithFrame(fLastFrame, environment), {0, 0});
		} else {
			fBackend->BeginFrame(*fSurface, fPalette.Get(BColorWhich::PanelBackground));
			fBackend->Render(*fLastFrame, {0, 0});
		}
		fBackend->EndFrame(*fSurface);

		if (fPlatformWindow != nullptr) {
			if (fDecorator != nullptr) {
				// the window's areas: the content's moved by its offset, all of
				// it when the frame changed
				const gfx::Rect outer = _PlatformFrame(fFrame);
				const gfx::Point offset = _ContentOffset();
				std::vector<gfx::Rect> areas;
				if (fFrameDirty) {
					areas.push_back({0, 0, outer.Width(), outer.Height()});
				} else {
					for (const gfx::Rect& rect : fDirty) {
						areas.push_back({rect.x0 + offset.x, rect.y0 + offset.y,
							rect.x1 + offset.x, rect.y1 + offset.y});
					}
				}
				Present(*fPlatformWindow, *fSurface, areas, fScale);
			} else {
				Present(*fPlatformWindow, *fSurface, fDirty, fScale);
			}
		}
		fDirty.clear();
		fFrameDirty = false;
	}

	for (const std::unique_ptr<PopupSurface>& popup : fPopups)
		_UpdatePopup(*popup, caps, text);
	_UpdateAccessibility();
	_UpdateSystemMenuBar();
}


// Passes the accessibility tree on to the platform when it changed, while
// assistive technology listens.
void
BWindow::_UpdateAccessibility()
{
	if (fPlatformWindow == nullptr || !fPlatformWindow->IsAccessibilityActive()) {
		fAccessibleTree.reset();
		return;
	}
	AccessibleTree tree = GetAccessibleTree();
	if (fAccessibleTree.has_value() && *fAccessibleTree == tree)
		return;
	if (fDecorator != nullptr) {
		// the platform's window has NewGUI's frame around the content
		AccessibleTree inWindow = tree;
		const gfx::Point offset = _ContentOffset();
		for (AccessibleNode& node : inWindow.nodes) {
			node.bounds = {node.bounds.x0 + offset.x, node.bounds.y0 + offset.y,
				node.bounds.x1 + offset.x, node.bounds.y1 + offset.y};
		}
		fPlatformWindow->UpdateAccessibility(inWindow);
	} else {
		fPlatformWindow->UpdateAccessibility(tree);
	}
	fAccessibleTree = std::move(tree);
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

	const DrawEnvironment environment{caps, fPalette, text, popup.scale};
	popup.lastFrame = popup.root->RecordTree(environment);
	_AddFonts(text);
	fBackend->BeginFrame(*popup.surface, fPalette.Get(BColorWhich::MenuBackground));
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
template<typename T>
std::future<std::expected<T, Error>>
Ready(std::expected<T, Error> value)
{
	std::promise<std::expected<T, Error>> promise;
	promise.set_value(std::move(value));
	return promise.get_future();
}


// \a work's result, run on the thread of the window \a window: at once on
// its own thread, else from a thread of its own that waits for the window.
template<typename T>
std::future<std::expected<T, Error>>
OnWindowThread(const BMessenger& window, std::function<std::expected<T, Error>(BWindow&)> work)
{
	return std::async(std::launch::async, [window, work = std::move(work)] {
		std::expected<T, Error> result = Failure(ErrorCode::NotFound, "the window is gone");
		auto ran = RunOnLooper(window, [&](BLooper& looper) {
			if (auto* target = dynamic_cast<BWindow*>(&looper))
				result = work(*target);
		}, BMessenger::kInfiniteTimeout);
		if (!ran.has_value())
			return std::expected<T, Error>(std::unexpected(ran.error()));
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
	return OnWindowThread<BBitmap>(window, [options](BWindow& target) {
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
	return OnWindowThread<BBitmap>(window, [](BWindow& target) {
		return target._CaptureFrame();
	});
}


std::future<std::expected<gfx::DisplayListRef, Error>>
BWindow::Record(const BSnapshotOptions& options)
{
	if (std::this_thread::get_id() == Thread())
		return Ready(_RecordForExport(options, nullptr));
	return Record(BMessenger(this), options);
}


std::future<std::expected<gfx::DisplayListRef, Error>>
BWindow::Record(const BMessenger& window, const BSnapshotOptions& options)
{
	return OnWindowThread<gfx::DisplayListRef>(window, [options](BWindow& target) {
		return target._RecordForExport(options, nullptr);
	});
}


// The window, or \a view and its children, recorded anew over the area of
// \a options with \a caps at \a scale, after pending layout and drawing
// (S1); on the panel background if \a background.
std::expected<gfx::DisplayListRef, Error>
BWindow::_Record(const BSnapshotOptions& options, BView* view, const gfx::Caps& caps,
	float scale, bool background)
{
	UpdateIfNeeded();
	if (view != nullptr && !view->IsLayoutValid())
		view->Layout(false);

	const DrawEnvironment environment{caps, fPalette, TextSystem::Default(), scale};
	BView* root = view != nullptr ? view : fRoot.get();
	const gfx::Rect bounds = view != nullptr ? view->Bounds() : Bounds();
	const gfx::Rect area = options.area.value_or(bounds);
	if (!area.IsFinite() || !(area.Width() > 0) || !(area.Height() > 0))
		return Failure(ErrorCode::InvalidArgument, "an empty area");

	// The window as its frames have it, then its popups at their places on
	// the screen, each on its menu background.
	gfx::Canvas canvas({area.x0, area.y0, area.x1, area.y1});
	if (background) {
		canvas.FillRect(area,
			gfx::Paint{.color = fPalette.Get(BColorWhich::PanelBackground)});
	}
	canvas.DrawList(root->RecordTree(environment), {0, 0});
	if (view == nullptr && options.includePopups) {
		for (const std::unique_ptr<PopupSurface>& popup : fPopups) {
			const gfx::Rect frame = popup->root->Frame();
			const gfx::Point at{frame.x0 - fFrame.x0, frame.y0 - fFrame.y0};
			canvas.FillRect({at.x, at.y, at.x + frame.Width(), at.y + frame.Height()},
				gfx::Paint{.color = fPalette.Get(BColorWhich::MenuBackground)});
			canvas.DrawList(popup->root->RecordTree(environment), at);
		}
	}
	return canvas.Finish();
}


// The window, or \a view and its children, recorded for an export
// (specification 19): with the capabilities of the options' backend.
std::expected<gfx::DisplayListRef, Error>
BWindow::_RecordForExport(const BSnapshotOptions& options, BView* view)
{
	gfx::Caps caps;
	if (!options.backend.empty() || fBackend == nullptr) {
		auto made = options.backend.empty()
			? gfx::BackendRegistry::Default().CreatePreferred({})
			: gfx::BackendRegistry::Default().Create(options.backend);
		if (!made.has_value())
			return std::unexpected(made.error());
		caps = (*made)->Capabilities();
	} else
		caps = fBackend->Capabilities();
	return _Record(options, view, caps, options.scale > 0 ? options.scale : fScale, true);
}


// The window, or \a view and its children, rendered anew into an
// offscreen CPU surface (specification 19.1, level a).
std::expected<BBitmap, Error>
BWindow::_Snapshot(const BSnapshotOptions& options, BView* view)
{
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
	backend->SetBlending(BApplication::Blending());

	const float scale = options.scale > 0 ? options.scale : fScale;
	const auto recorded = _Record(options, view, backend->Capabilities(), scale, false);
	if (!recorded.has_value())
		return std::unexpected(recorded.error());
	const gfx::DisplayListRef& list = *recorded;
	const gfx::Rect area = list->Bounds();
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
	backend->BeginFrame(*surface, fPalette.Get(BColorWhich::PanelBackground));
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
	if (fFrameCount == 0)
		return Failure(ErrorCode::NotFound, "no frame shown yet");
	// a frame the platform rendered (phase G): what it shows
	if (fPlatformWindow != nullptr && fPlatformWindow->DrawsLists()) {
		const auto shown = fPlatformWindow->ReadShownFrame();
		if (!shown.has_value())
			return std::unexpected(shown.error());
		return BitmapOf(gfx::CpuPixels{shown->width, shown->height,
			static_cast<std::size_t>(shown->width) * 4, gfx::PixelFormat::Rgba8Premul,
			shown->rgba});
	}
	if (fSurface == nullptr)
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
	if (fPlatformWindow != nullptr) {
		// in the window, with NewGUI's frame around the content
		const gfx::Point offset = _ContentOffset();
		fPlatformWindow->SetTextInputArea({rect.x0 + offset.x, rect.y0 + offset.y,
			rect.x1 + offset.x, rect.y1 + offset.y}, cursor);
	}
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


namespace {


// The items of \a menu for the system's menu bar (module P3); the id of an
// entry is its item's address, checked against the menus when chosen.
std::vector<platform::SystemMenuItem>
SystemItemsOf(const BMenu& menu)
{
	std::vector<platform::SystemMenuItem> items;
	for (int32 index = 0; index < menu.CountItems(); index++) {
		const BMenuItem* item = menu.ItemAt(index);
		platform::SystemMenuItem entry;
		entry.id = reinterpret_cast<std::uintptr_t>(item);
		if (dynamic_cast<const BSeparatorItem*>(item) != nullptr) {
			entry.separator = true;
			items.push_back(std::move(entry));
			continue;
		}
		entry.label = item->Label();
		entry.enabled = item->IsEnabled();
		entry.marked = item->IsMarked();
		entry.shortcut = item->Shortcut(&entry.modifiers);
		if (const BMenu* submenu = item->Submenu())
			entry.items = SystemItemsOf(*submenu);
		items.push_back(std::move(entry));
	}
	return items;
}


// The item of \a menu or its submenus with the address \a id.
BMenuItem*
FindSystemItem(const BMenu& menu, std::uint64_t id)
{
	for (int32 index = 0; index < menu.CountItems(); index++) {
		BMenuItem* item = menu.ItemAt(index);
		if (reinterpret_cast<std::uintptr_t>(item) == id)
			return item;
		if (const BMenu* submenu = item->Submenu()) {
			if (BMenuItem* found = FindSystemItem(*submenu, id))
				return found;
		}
	}
	return nullptr;
}


} // namespace


// Passes the key menu bar on to the system's menu bar when it changed, and
// hides it in the window (module P3).
void
BWindow::_UpdateSystemMenuBar()
{
	if (fPlatformWindow == nullptr || !fPlatformWindow->HasSystemMenuBar())
		return;
	std::vector<platform::SystemMenuItem> menus;
	if (fKeyMenuBar != nullptr && fKeyMenuBar->UsesSystemMenuBar()) {
		if (!fKeyMenuBar->fHiddenForSystemMenuBar) {
			fKeyMenuBar->fHiddenForSystemMenuBar = true;
			fKeyMenuBar->Hide();
		}
		menus = SystemItemsOf(*fKeyMenuBar);
	}
	if (menus == fSystemMenus)
		return;
	fSystemMenus = menus;
	fPlatformWindow->SetSystemMenuBar(std::move(menus));
}


BMenuItem*
BWindow::_SystemMenuItem(std::uint64_t id) const
{
	if (id == 0 || fKeyMenuBar == nullptr || !fKeyMenuBar->UsesSystemMenuBar())
		return nullptr;
	return FindSystemItem(*fKeyMenuBar, id);
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


// #pragma mark - Scripting


namespace {


// Ported from Haiku: src/kits/interface/Window.cpp (commit b1d497bb69),
// sWindowPropInfo with the properties NewGUI's windows have; the indices
// are those of MessageReceived().
const std::vector<BPropertyInfoEntry> kWindowProperties = {
	{"Active", {kGetProperty, kSetProperty}, {kDirectSpecifier},
		"Whether it is the active window.", 0, {BTypeCode::Bool}},
	{"Frame", {kGetProperty, kSetProperty}, {kDirectSpecifier},
		"Its content area on the screen.", 0, {BTypeCode::Rect}},
	{"Hidden", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Whether it is hidden.", 0,
		{BTypeCode::Bool}},
	{"Title", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Its title.", 0,
		{BTypeCode::String}},
	{"View", {kCountProperties}, {kDirectSpecifier}, "How many top views it has.", 0,
		{BTypeCode::Int32}},
	{"View", {}, {}, "A view by index or name; leads on to its properties.", 0, {}},
};


} // namespace


void
BWindow::MessageReceived(BMessage* message)
{
	const auto specifier = message->HasSpecifiers() ? message->GetCurrentSpecifier()
		: std::expected<BSpecifier, Error>(std::unexpected(Error{ErrorCode::BadScriptSyntax, ""}));
	if (!specifier.has_value()) {
		BLooper::MessageReceived(message);
		return;
	}
	const bool get = message->what == kGetProperty;
	const bool set = message->what == kSetProperty;
	BMessage reply(kReply);
	std::expected<void, Error> result;
	switch (BPropertyInfo(kWindowProperties).FindMatch(*message, specifier->index,
		specifier->form, specifier->property)) {
		case 0:
			if (get)
				result = reply.AddBool("result", IsActive());
			else if (set) {
				const auto active = message->FindBool("data");
				if (active.has_value())
					Activate(*active);
				result = active.has_value() ? std::expected<void, Error>()
					: std::unexpected(active.error());
			}
			break;
		case 1:
			if (get)
				result = reply.AddRect("result", Frame());
			else if (set) {
				const auto frame = message->FindRect("data");
				if (frame.has_value()) {
					MoveTo({frame->x0, frame->y0});
					ResizeTo(frame->Width(), frame->Height());
				}
				result = frame.has_value() ? std::expected<void, Error>()
					: std::unexpected(frame.error());
			}
			break;
		case 2:
			if (get)
				result = reply.AddBool("result", IsHidden());
			else if (set) {
				const auto hide = message->FindBool("data");
				if (hide.has_value()) {
					if (*hide && !IsHidden())
						Hide();
					else if (!*hide && IsHidden())
						Show();
				}
				result = hide.has_value() ? std::expected<void, Error>()
					: std::unexpected(hide.error());
			}
			break;
		case 3:
			if (get)
				result = reply.AddString("result", Title());
			else if (set) {
				const auto title = message->FindString("data");
				if (title.has_value())
					SetTitle(*title);
				result = title.has_value() ? std::expected<void, Error>()
					: std::unexpected(title.error());
			}
			break;
		default:
			BLooper::MessageReceived(message);
			return;
	}
	detail::SendScriptingReply(*message, std::move(reply), result);
}


BHandler*
BWindow::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
	std::uint32_t form, std::string_view property)
{
	if (BPropertyInfo(kWindowProperties).FindMatch(*message, index, form, property) >= 0) {
		// the root view resolves the same specifier among its children
		if (property == "View")
			return fRoot.get();
		return this;
	}
	return BLooper::ResolveSpecifier(message, index, specifier, form, property);
}


std::expected<void, Error>
BWindow::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-window"); !added)
		return added;
	if (auto added = data->AddMessage("messages", BPropertyInfo(kWindowProperties).ToMessage());
			!added) {
		return added;
	}
	return BLooper::GetSupportedSuites(data);
}


} // namespace ngui
