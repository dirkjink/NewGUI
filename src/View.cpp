/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/View.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

#include <ngui/AppDefs.h>
#include <ngui/Cursor.h>
#include <ngui/LayoutContext.h>
#include <ngui/LookRegistry.h>
#include <ngui/Message.h>
#include <ngui/PropertyInfo.h>
#include <ngui/ScrollBar.h>
#include <ngui/ToolTip.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Paint.h>

#include "ScriptingSupport.h"
#include "ToolTipPopup.h"


namespace ngui {


namespace {


// Haiku moves a centered side by half the change in whole pixels
// (int32 x / 2, rounding towards zero).
float
Half(float delta)
{
	return std::trunc(delta / 2);
}


} // namespace


BView::BView(gfx::Rect frame, std::string_view name, BResizingMode resizingMode,
	BViewFlags flags)
	:
	BHandler(name),
	fFrame(frame),
	fResizingMode(resizingMode),
	fFlags(flags),
	fViewColor(gfx::Color{1, 1, 1, 1})
{
}


BView::BView(std::string_view name, BViewFlags flags)
	:
	BView({0, 0, 0, 0}, name, kFollowLeftTop, flags | BViewFlag::SupportsLayout)
{
}


BView::~BView()
{
	assert(fParent == nullptr && "BView destroyed while in a parent; RemoveChild() it first");
	// The scroll bars no longer scroll it.
	if (fVerScroller != nullptr)
		fVerScroller->SetTarget(static_cast<BView*>(nullptr));
	if (fHorScroller != nullptr)
		fHorScroller->SetTarget(static_cast<BView*>(nullptr));
	// The layout goes first: its items refer to the children.
	_DropLayout();
	for (std::unique_ptr<BView>& child : fChildren)
		child->fParent = nullptr;
}


BView*
BView::AddChild(std::unique_ptr<BView> child, BView* before)
{
	BView* added = _AddChild(std::move(child), before);
	if (added != nullptr && fLayout)
		(void)fLayout->AddView(added);
	return added;
}


BLayoutItem*
BView::AddChild(std::unique_ptr<BLayoutItem> child)
{
	if (!fLayout)
		return nullptr;
	return fLayout->AddItem(std::move(child));
}


BView*
BView::_AddChild(std::unique_ptr<BView> child, BView* before)
{
	if (child == nullptr)
		return nullptr;
	auto position = fChildren.end();
	if (before != nullptr) {
		position = std::find_if(fChildren.begin(), fChildren.end(),
			[before](const std::unique_ptr<BView>& candidate) {
				return candidate.get() == before;
			});
		if (position == fChildren.end())
			return nullptr;
	}
	BView* added = child.get();
	added->fParent = this;
	fChildren.insert(position, std::move(child));
	if (fWindow != nullptr) {
		added->_Attach(fWindow);
		_Damage(ConvertToParent(added->Frame()));
	}
	InvalidateLayout();
	return added;
}


std::unique_ptr<BView>
BView::RemoveChild(BView* child)
{
	const auto found = std::find_if(fChildren.begin(), fChildren.end(),
		[child](const std::unique_ptr<BView>& candidate) { return candidate.get() == child; });
	if (found == fChildren.end())
		return nullptr;
	std::unique_ptr<BView> removed = std::move(*found);
	if (fWindow != nullptr) {
		_Damage(ConvertToParent(removed->Frame()));
		removed->_Detach();
	}
	fChildren.erase(found);
	removed->fParent = nullptr;
	// Without a parent, the removal of its last item does not take the view
	// out once more (BLayout::RemoveItem()).
	removed->_RemoveLayoutItemsFromLayout();
	InvalidateLayout();
	return removed;
}


std::unique_ptr<BView>
BView::RemoveSelf()
{
	return fParent != nullptr ? fParent->RemoveChild(this) : nullptr;
}


BView*
BView::Parent() const
{
	return fParent;
}


std::int32_t
BView::CountChildren() const
{
	return static_cast<std::int32_t>(fChildren.size());
}


BView*
BView::ChildAt(std::int32_t index) const
{
	if (index < 0 || static_cast<std::size_t>(index) >= fChildren.size())
		return nullptr;
	return fChildren[static_cast<std::size_t>(index)].get();
}


BView*
BView::NextSibling() const
{
	if (fParent == nullptr)
		return nullptr;
	const std::vector<std::unique_ptr<BView>>& siblings = fParent->fChildren;
	for (std::size_t i = 0; i + 1 < siblings.size(); i++) {
		if (siblings[i].get() == this)
			return siblings[i + 1].get();
	}
	return nullptr;
}


BView*
BView::PreviousSibling() const
{
	if (fParent == nullptr)
		return nullptr;
	const std::vector<std::unique_ptr<BView>>& siblings = fParent->fChildren;
	for (std::size_t i = 1; i < siblings.size(); i++) {
		if (siblings[i].get() == this)
			return siblings[i - 1].get();
	}
	return nullptr;
}


BView*
BView::FindView(std::string_view name) const
{
	if (Name() == name)
		return const_cast<BView*>(this);
	for (const std::unique_ptr<BView>& child : fChildren) {
		if (BView* found = child->FindView(name))
			return found;
	}
	return nullptr;
}


BWindow*
BView::Window() const
{
	return fWindow;
}


gfx::Rect
BView::Frame() const
{
	return fFrame;
}


gfx::Rect
BView::Bounds() const
{
	return {fScroll.x, fScroll.y, fScroll.x + fFrame.Width(), fScroll.y + fFrame.Height()};
}


void
BView::MoveBy(float dx, float dy)
{
	if (dx == 0 && dy == 0)
		return;
	_Damage(fFrame);
	fFrame = {fFrame.x0 + dx, fFrame.y0 + dy, fFrame.x1 + dx, fFrame.y1 + dy};
	_Damage(fFrame);
	// A popup's root moves its popup.
	if (_IsSurfaceRoot())
		fWindow->_RootFrameChanged(this);
	if (fFlags.Has(BViewFlag::FrameEvents))
		FrameMoved({fFrame.x0, fFrame.y0});
}


void
BView::MoveTo(gfx::Point where)
{
	MoveBy(where.x - fFrame.x0, where.y - fFrame.y0);
}


void
BView::ResizeBy(float dw, float dh)
{
	if (dw == 0 && dh == 0)
		return;
	_Damage(fFrame);
	fFrame = {fFrame.x0, fFrame.y0, fFrame.x1 + dw, fFrame.y1 + dh};
	// A popup's root resizes its popup.
	if (_IsSurfaceRoot())
		fWindow->_RootFrameChanged(this);
	// The list covers the whole view; a new size needs a new one.
	Invalidate();
	// layout the children (Haiku's _ResizeBy(): a view that supports layout
	// is laid out, in a window only; the others follow their modes)
	if (fFlags.Has(BViewFlag::SupportsLayout)) {
		if (fWindow != nullptr)
			Relayout();
	} else {
		for (std::unique_ptr<BView>& child : fChildren)
			child->_ParentResized(dw, dh);
	}
	if (fFlags.Has(BViewFlag::FrameEvents))
		FrameResized(fFrame.Width(), fFrame.Height());
}


void
BView::ResizeTo(float width, float height)
{
	ResizeBy(width - fFrame.Width(), height - fFrame.Height());
}


void
BView::ScrollBy(float dx, float dy)
{
	ScrollTo({fScroll.x + dx, fScroll.y + dy});
}


void
BView::ScrollTo(gfx::Point where)
{
	// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69).

	// scrolling by fractional values is not supported
	where.x = std::round(where.x);
	where.y = std::round(where.y);

	// no reason to process this further if no scroll is intended.
	if (where.x == fScroll.x && where.y == fScroll.y)
		return;

	// make sure scrolling is within valid bounds
	if (fHorScroller) {
		float min, max;
		fHorScroller->GetRange(&min, &max);

		if (where.x < min)
			where.x = min;
		else if (where.x > max)
			where.x = max;
	}
	if (fVerScroller) {
		float min, max;
		fVerScroller->GetRange(&min, &max);

		if (where.y < min)
			where.y = min;
		else if (where.y > max)
			where.y = max;
	}

	float xDiff = where.x - fScroll.x;
	float yDiff = where.y - fScroll.y;
	if (xDiff == 0 && yDiff == 0)
		return;

	// NewGUI: the view is recorded anew instead of the app_server
	// scrolling its pixels (CopyBits).
	fScroll = where;
	Invalidate();

	// then set the new values of the scrollbars
	if (fHorScroller && xDiff != 0.0f)
		fHorScroller->SetValue(fScroll.x);
	if (fVerScroller && yDiff != 0.0f)
		fVerScroller->SetValue(fScroll.y);
}


BScrollBar*
BView::ScrollBar(BOrientation direction) const
{
	switch (direction) {
		case BOrientation::Vertical:
			return fVerScroller;

		case BOrientation::Horizontal:
			return fHorScroller;
	}

	return nullptr;
}


void
BView::TargetedByScrollView(BScrollView* /*scrollView*/)
{
	// hook method
}


namespace {


// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69),
// sViewPropInfo without "Shelf" and with "Name" (specification 20.7); the
// indices are those of BView::MessageReceived().
const std::vector<BPropertyInfoEntry> kViewProperties = {
	{"Frame", {kGetProperty, kSetProperty}, {kDirectSpecifier},
		"Its frame in its parent.", 0, {BTypeCode::Rect}},
	{"Hidden", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Whether it is hidden.", 0,
		{BTypeCode::Bool}},
	{"Name", {kGetProperty}, {kDirectSpecifier}, "Its name.", 0, {BTypeCode::String}},
	{"View", {kCountProperties}, {kDirectSpecifier}, "How many children it has.", 0,
		{BTypeCode::Int32}},
	{"View", {}, {kIndexSpecifier, kReverseIndexSpecifier, kNameSpecifier},
		"A child by index or a descendant by name; leads on to its properties.", 0, {}},
};


} // namespace


void
BView::MessageReceived(BMessage* message)
{
	// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69).
	if (message->HasSpecifiers()) {
		const auto specifier = message->GetCurrentSpecifier();
		const std::int32_t match = specifier.has_value()
			? BPropertyInfo(kViewProperties).FindMatch(*message, specifier->index,
				specifier->form, specifier->property)
			: -1;
		if (match >= 0 && match <= 3) {
			const bool get = message->what == kGetProperty;
			BMessage reply(kReply);
			std::expected<void, Error> result;
			if (match == 0 && get)
				result = reply.AddRect("result", Frame());
			else if (match == 0) {
				const auto frame = message->FindRect("data");
				if (frame.has_value()) {
					MoveTo({frame->x0, frame->y0});
					ResizeTo(frame->Width(), frame->Height());
				}
				result = frame.has_value() ? std::expected<void, Error>()
					: std::unexpected(frame.error());
			} else if (match == 1 && get)
				result = reply.AddBool("result", IsHidden());
			else if (match == 1) {
				const auto hide = message->FindBool("data");
				if (hide.has_value()) {
					if (*hide)
						Hide();
					else
						Show();
				}
				result = hide.has_value() ? std::expected<void, Error>()
					: std::unexpected(hide.error());
			} else if (match == 2)
				result = reply.AddString("result", Name());
			else
				result = reply.AddInt32("result", CountChildren());
			detail::SendScriptingReply(*message, std::move(reply), result);
			return;
		}
	}

	// the application's colors changed: drawn anew in them (Haiku's view
	// takes its UI colors anew here)
	if (message->what == kColorsUpdated) {
		Invalidate();
		return;
	}

	if (message->what == kMouseIdle) {
		std::shared_ptr<BToolTip> tip;
		if (GetToolTipAt(message->GetPoint("where", {0, 0}), &tip))
			ShowToolTip(tip);
		else
			BHandler::MessageReceived(message);
		return;
	}

	if (message->what != kMouseWheelChanged) {
		BHandler::MessageReceived(message);
		return;
	}

	BScrollBar* horizontal = ScrollBar(BOrientation::Horizontal);
	BScrollBar* vertical = ScrollBar(BOrientation::Vertical);
	if (horizontal == nullptr && vertical == nullptr) {
		// Pass the message to the next handler
		BHandler::MessageReceived(message);
		return;
	}

	float deltaX = 0.0f;
	float deltaY = 0.0f;

	if (horizontal != nullptr)
		deltaX = message->GetFloat("be:wheel_delta_x", 0.0f);

	if (vertical != nullptr)
		deltaY = message->GetFloat("be:wheel_delta_y", 0.0f);

	if (deltaX == 0.0f && deltaY == 0.0f)
		return;

	const auto modifiers = BModifierFlags::FromRaw(
		static_cast<std::uint32_t>(message->GetInt32("modifiers", 0)));
	if (modifiers.Has(BModifier::Control))
		std::swap(horizontal, vertical);

	if (horizontal != nullptr && deltaX != 0.0f)
		(void)ScrollWithMouseWheelDelta(horizontal, deltaX);

	if (vertical != nullptr && deltaY != 0.0f)
		(void)ScrollWithMouseWheelDelta(vertical, deltaY);
}


bool
BView::ScrollWithMouseWheelDelta(BScrollBar* scrollBar, float delta)
{
	// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69).
	if (scrollBar == nullptr || delta == 0.0f)
		return false;

	float smallStep;
	float largeStep;
	scrollBar->GetSteps(&smallStep, &largeStep);

	// pressing the shift key scrolls faster (following the pseudo-standard set
	// by other desktop environments).
	// NewGUI: the modifiers of the current message instead of modifiers()
	const BMessage* current = Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	const auto modifiers = BModifierFlags::FromRaw(static_cast<std::uint32_t>(
		current != nullptr ? current->GetInt32("modifiers", 0) : 0));
	if (modifiers.Has(BModifier::Shift))
		delta *= largeStep;
	else
		delta *= smallStep * 3;

	scrollBar->SetValue(scrollBar->Value() + delta);

	return true;
}


gfx::Point
BView::ConvertToParent(gfx::Point point) const
{
	return {point.x - fScroll.x + fFrame.x0, point.y - fScroll.y + fFrame.y0};
}


gfx::Point
BView::ConvertFromParent(gfx::Point point) const
{
	return {point.x - fFrame.x0 + fScroll.x, point.y - fFrame.y0 + fScroll.y};
}


gfx::Rect
BView::ConvertToParent(const gfx::Rect& rect) const
{
	const gfx::Point a = ConvertToParent(gfx::Point{rect.x0, rect.y0});
	const gfx::Point b = ConvertToParent(gfx::Point{rect.x1, rect.y1});
	return {a.x, a.y, b.x, b.y};
}


gfx::Rect
BView::ConvertFromParent(const gfx::Rect& rect) const
{
	const gfx::Point a = ConvertFromParent(gfx::Point{rect.x0, rect.y0});
	const gfx::Point b = ConvertFromParent(gfx::Point{rect.x1, rect.y1});
	return {a.x, a.y, b.x, b.y};
}


gfx::Point
BView::ConvertToWindow(gfx::Point point) const
{
	// Up to the root view of its surface: the window's content area, whose
	// root is at (0, 0), or a popup, whose root's frame is on the screen.
	for (const BView* view = this; view != nullptr; view = view->fParent) {
		point = view->ConvertToParent(point);
		if (view->_IsSurfaceRoot())
			point = {point.x - view->fFrame.x0, point.y - view->fFrame.y0};
	}
	return point;
}


gfx::Point
BView::ConvertFromWindow(gfx::Point point) const
{
	if (fParent != nullptr)
		point = fParent->ConvertFromWindow(point);
	else if (_IsSurfaceRoot())
		point = {point.x + fFrame.x0, point.y + fFrame.y0};
	return ConvertFromParent(point);
}


gfx::Rect
BView::ConvertToWindow(const gfx::Rect& rect) const
{
	const gfx::Point a = ConvertToWindow(gfx::Point{rect.x0, rect.y0});
	const gfx::Point b = ConvertToWindow(gfx::Point{rect.x1, rect.y1});
	return {a.x, a.y, b.x, b.y};
}


gfx::Point
BView::ConvertToScreen(gfx::Point point) const
{
	const gfx::Point inWindow = ConvertToWindow(point);
	if (fWindow == nullptr)
		return inWindow;
	const gfx::Point origin = fWindow->_SurfaceOrigin(_Root());
	return {inWindow.x + origin.x, inWindow.y + origin.y};
}


gfx::Point
BView::ConvertFromScreen(gfx::Point point) const
{
	if (fWindow != nullptr) {
		const gfx::Point origin = fWindow->_SurfaceOrigin(_Root());
		point = {point.x - origin.x, point.y - origin.y};
	}
	return ConvertFromWindow(point);
}


BResizingMode
BView::ResizingMode() const
{
	return fResizingMode;
}


void
BView::SetResizingMode(BResizingMode mode)
{
	fResizingMode = mode;
}


BViewFlags
BView::Flags() const
{
	return fFlags;
}


void
BView::SetFlags(BViewFlags flags)
{
	if (flags.Has(BViewFlag::WillDraw) != fFlags.Has(BViewFlag::WillDraw)
		|| flags.Has(BViewFlag::DrawOnChildren) != fFlags.Has(BViewFlag::DrawOnChildren)) {
		Invalidate();
	}
	fFlags = flags;
}


void
BView::_ParentResized(float dw, float dh)
{
	// As Haiku's BView::_ParentResizedBy(): each side follows its rule.
	gfx::Rect frame = fFrame;
	switch (fResizingMode.horizontal) {
		case BHorizontalFollow::Left:
			break;
		case BHorizontalFollow::Right:
			frame.x0 += dw;
			frame.x1 += dw;
			break;
		case BHorizontalFollow::LeftRight:
			frame.x1 += dw;
			break;
		case BHorizontalFollow::Center:
			frame.x0 += Half(dw);
			frame.x1 += Half(dw);
			break;
	}
	switch (fResizingMode.vertical) {
		case BVerticalFollow::Top:
			break;
		case BVerticalFollow::Bottom:
			frame.y0 += dh;
			frame.y1 += dh;
			break;
		case BVerticalFollow::TopBottom:
			frame.y1 += dh;
			break;
		case BVerticalFollow::Center:
			frame.y0 += Half(dh);
			frame.y1 += Half(dh);
			break;
	}
	MoveTo({frame.x0, frame.y0});
	ResizeTo(frame.Width(), frame.Height());
}


void
BView::Show()
{
	if (fHideLevel > 0) {
		fHideLevel--;
		_Damage(fFrame);
		if (fHideLevel == 0)
			_InvalidateParentLayout();
	}
}


void
BView::Hide()
{
	fHideLevel++;
	_Damage(fFrame);
	if (fHideLevel == 1)
		_InvalidateParentLayout();
}


bool
BView::IsHidden() const
{
	for (const BView* view = this; view != nullptr; view = view->fParent) {
		if (view->fHideLevel > 0)
			return true;
	}
	return false;
}


void
BView::Draw(DrawContext&)
{
}


void
BView::DrawAfterChildren(DrawContext&)
{
}


void
BView::Invalidate()
{
	fList.reset();
	fListAfterChildren.reset();
	_Damage(fFrame);
}


void
BView::_Damage(const gfx::Rect& rect)
{
	// \a rect is in the parent's coordinates; the window collects the
	// areas of the next frame in those of the view's surface.
	if (fWindow == nullptr)
		return;
	if (fParent != nullptr) {
		fWindow->_AddDirty(_Root(), fParent->ConvertToWindow(rect));
	} else {
		// the root: its frame is the surface, from its origin
		fWindow->_AddDirty(this, {rect.x0 - fFrame.x0, rect.y0 - fFrame.y0,
			rect.x1 - fFrame.x0, rect.y1 - fFrame.y0});
	}
}


const BView*
BView::_Root() const
{
	const BView* view = this;
	while (view->fParent != nullptr)
		view = view->fParent;
	return view;
}


bool
BView::_IsSurfaceRoot() const
{
	// The root of the window's content area or of one of its popups.
	return fParent == nullptr && fWindow != nullptr;
}


bool
BView::IsValid() const
{
	return fList != nullptr;
}


void
BView::SetViewColor(std::optional<gfx::Color> color)
{
	if (color != fViewColor || fViewUIColor.has_value())
		Invalidate();
	fViewColor = color;
	fViewUIColor.reset();
}


std::optional<gfx::Color>
BView::ViewColor() const
{
	if (fViewUIColor.has_value()) {
		// in the colors of the frame being drawn
		const Palette palette = Window() != nullptr ? Window()->_FramePalette()
			: Palette::Current();
		return TintColor(palette.Get(*fViewUIColor), fViewUITint);
	}
	return fViewColor;
}


void
BView::SetViewUIColor(BColorWhich which, float tint)
{
	if (fViewUIColor != which || fViewUITint != tint)
		Invalidate();
	fViewUIColor = which;
	fViewUITint = tint;
	fViewColor.reset();
}


std::optional<BColorWhich>
BView::ViewUIColor(float* tint) const
{
	if (tint != nullptr)
		*tint = fViewUITint;
	return fViewUIColor;
}


void
BView::AdoptParentColors()
{
	if (Parent() == nullptr)
		return;
	float tint = kNoTint;
	if (const std::optional<BColorWhich> which = Parent()->ViewUIColor(&tint))
		SetViewUIColor(*which, tint);
	else
		SetViewColor(Parent()->ViewColor());
}


gfx::DisplayListRef
BView::_RecordOwn(const DrawEnvironment& environment)
{
	gfx::Canvas canvas(Bounds());
	if (fViewUIColor.has_value()) {
		canvas.FillRect(Bounds(), gfx::Paint{.color = TintColor(
			environment.palette.Get(*fViewUIColor), fViewUITint)});
	} else if (fViewColor.has_value()) {
		canvas.FillRect(Bounds(), gfx::Paint{.color = *fViewColor});
	}
	if (fFlags.Has(BViewFlag::WillDraw)) {
		DrawContext context{canvas, environment.caps, environment.palette, environment.text,
			environment.scale};
		Draw(context);
	}
	return canvas.Finish();
}


gfx::DisplayListRef
BView::_RecordChildrenOver(const DrawEnvironment& environment)
{
	gfx::Canvas canvas(Bounds());
	DrawContext context{canvas, environment.caps, environment.palette, environment.text,
		environment.scale};
	DrawAfterChildren(context);
	return canvas.Finish();
}


std::expected<BBitmap, Error>
BView::Snapshot(const BSnapshotOptions& options)
{
	if (Window() == nullptr)
		return Failure(ErrorCode::InvalidArgument, "the view has no window");
	return Window()->_Snapshot(options, this);
}


std::expected<gfx::DisplayListRef, Error>
BView::Record(const BSnapshotOptions& options)
{
	if (Window() == nullptr)
		return Failure(ErrorCode::InvalidArgument, "the view has no window");
	return Window()->_RecordForExport(options, this);
}


void
BView::DragMessage(const BMessage& message, gfx::Rect dragRect, BHandler* replyTo)
{
	BWindow* window = Window();
	if (window == nullptr)
		return;
	// NewGUI: the part of the view itself under the mouse, at scale 1
	std::unique_ptr<BBitmap> image;
	BSnapshotOptions options;
	options.area = dragRect;
	options.scale = 1;
	if (auto shot = Snapshot(options); shot.has_value())
		image = std::make_unique<BBitmap>(std::move(*shot));
	const gfx::Point corner = ConvertToScreen({dragRect.x0, dragRect.y0});
	const gfx::Point mouse = window->fLastMouse.value_or(corner);
	DragMessage(message, std::move(image), {mouse.x - corner.x, mouse.y - corner.y}, replyTo);
}


void
BView::DragMessage(const BMessage& message, std::unique_ptr<BBitmap> image, gfx::Point offset,
	BHandler* replyTo)
{
	BWindow* window = Window();
	if (window == nullptr)
		return;
	BHandler* replies = replyTo != nullptr ? replyTo : this;
	window->_StartDrag(message, std::move(image), offset, BMessenger(replies, window));
}


gfx::DisplayListRef
BView::RecordTree(const DrawEnvironment& environment)
{
	if (fList == nullptr)
		fList = _RecordOwn(environment);
	if (fChildren.empty() && !fFlags.Has(BViewFlag::DrawOnChildren))
		return fList;

	// Put together for every frame; only references are copied (11.4).
	const gfx::Rect bounds = Bounds();
	gfx::Canvas canvas(bounds);
	canvas.DrawList(fList, {0, 0});
	for (const std::unique_ptr<BView>& child : fChildren) {
		if (child->fHideLevel > 0)
			continue;
		const gfx::Rect frame = child->Frame();
		const gfx::Rect childBounds = child->Bounds();
		canvas.Save();
		canvas.ClipRect(frame);
		canvas.DrawList(child->RecordTree(environment),
			{frame.x0 - childBounds.x0, frame.y0 - childBounds.y0});
		canvas.Restore();
	}
	if (fFlags.Has(BViewFlag::DrawOnChildren)) {
		if (fListAfterChildren == nullptr)
			fListAfterChildren = _RecordChildrenOver(environment);
		canvas.DrawList(fListAfterChildren, {0, 0});
	}
	return canvas.Finish();
}


void
BView::MakeFocus(bool focus)
{
	// As Haiku's BView::MakeFocus().
	if (fWindow == nullptr)
		return;

	BView* focusView = fWindow->CurrentFocus();
	if (focus) {
		// Unfocus a previous focus view
		if (focusView != nullptr && focusView != this)
			focusView->MakeFocus(false);

		// if we want to make this view the current focus view
		fWindow->_SetFocus(this);
	} else {
		// we want to unfocus this view, but only if it actually has focus
		if (focusView == this)
			fWindow->_SetFocus(nullptr);
	}
}


const ControlLook&
BView::Look() const
{
	if (fWindow != nullptr)
		return fWindow->Look();

	// Outside a window the reference stays valid until the next call on this
	// thread, even if the current look is replaced meanwhile.
	thread_local std::shared_ptr<const ControlLook> current;
	current = LookRegistry::DefaultLook();
	return *current;
}


bool
BView::IsFocus() const
{
	return fWindow != nullptr && fWindow->CurrentFocus() == this;
}


void
BView::SetMouseEventMask(BEventMaskFlags mask)
{
	if (fWindow != nullptr && mask.Has(BEventMask::Pointer))
		fWindow->_SetMouseCapture(this);
}


void
BView::AttachedToWindow()
{
}


void
BView::AllAttached()
{
}


void
BView::DetachedFromWindow()
{
}


void
BView::AllDetached()
{
}


void
BView::MouseDown(gfx::Point)
{
}


void
BView::MouseUp(gfx::Point)
{
}


void
BView::MouseMoved(gfx::Point, BMouseTransit, const BMessage*)
{
}


void
BView::KeyDown(std::string_view)
{
	// As Haiku's: a view that does not use the key lets Tab move the focus.
	if (fWindow != nullptr)
		fWindow->_KeyboardNavigation();
}


void
BView::KeyUp(std::string_view)
{
}


void
BView::WindowActivated(bool)
{
}


void
BView::FrameMoved(gfx::Point)
{
}


void
BView::FrameResized(float, float)
{
}


void
BView::_Attach(BWindow* window)
{
	// As Haiku: AttachedToWindow() before the children, AllAttached()
	// after them.
	fWindow = window;
	window->AddHandler(this);
	// As in Haiku, the next handler of a view is its parent.
	if (fParent != nullptr)
		SetNextHandler(fParent);
	AttachedToWindow();
	for (std::unique_ptr<BView>& child : fChildren)
		child->_Attach(window);
	AllAttached();
}


void
BView::_Detach()
{
	DetachedFromWindow();
	for (std::unique_ptr<BView>& child : fChildren)
		child->_Detach();
	AllDetached();
	fWindow->_ViewDetached(this);
	fWindow->RemoveHandler(this);
	fWindow = nullptr;
}


// #pragma mark - Cursor


void
BView::SetViewCursor(const BCursor& cursor)
{
	if (!cursor.InitCheck().has_value())
		return;
	if (fCursor != nullptr && *fCursor == cursor)
		return;
	fCursor = std::make_unique<BCursor>(cursor);
	// shown at once if the mouse is over this view (Haiku's
	// AS_SET_VIEW_CURSOR)
	if (fWindow != nullptr)
		fWindow->_ViewCursorChanged(this);
}


void
BView::ClearViewCursor()
{
	if (fCursor == nullptr)
		return;
	fCursor.reset();
	if (fWindow != nullptr)
		fWindow->_ViewCursorChanged(this);
}


const BCursor*
BView::ViewCursor() const
{
	return fCursor.get();
}


// #pragma mark - Tooltips
//
// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69).


void
BView::SetToolTip(std::string_view text)
{
	if (text.empty()) {
		SetToolTip(std::shared_ptr<BToolTip>());
		return;
	}

	if (auto* textTip = dynamic_cast<BTextToolTip*>(fToolTip.get()))
		textTip->SetText(text);
	else
		SetToolTip(std::make_shared<BTextToolTip>(text));
}


void
BView::SetToolTip(std::shared_ptr<BToolTip> tip)
{
	if (fToolTip == tip)
		return;
	else if (tip == nullptr)
		HideToolTip();

	fToolTip = std::move(tip);
}


std::shared_ptr<BToolTip>
BView::ToolTip() const
{
	return fToolTip;
}


void
BView::GetAccessibleInfo(AccessibleInfo* info) const
{
	if (info == nullptr)
		return;
	info->name = fAccessibleName;
	info->description = fAccessibleDescription;
	info->live = fLiveRegion;
	if (info->description.empty()) {
		if (const auto* tip = dynamic_cast<const BTextToolTip*>(fToolTip.get()))
			info->description = tip->Text();
	}
	if (Flags().Has(BViewFlag::Navigable)) {
		info->states |= AccessibleState::Focusable;
		info->actions |= AccessibleAction::Focus;
	}
}


std::expected<void, Error>
BView::PerformAccessibleAction(AccessibleAction action, const BMessage& /*data*/)
{
	if (action == AccessibleAction::Focus && Flags().Has(BViewFlag::Navigable)) {
		MakeFocus(true);
		return {};
	}
	return Failure(ErrorCode::NotSupported, "the view does not take this action");
}


void
BView::GetAccessibleItems(std::vector<AccessibleItem>* /*items*/) const
{
}


std::expected<void, Error>
BView::PerformAccessibleItemAction(int32 /*index*/, AccessibleAction /*action*/,
	const BMessage& /*data*/)
{
	return Failure(ErrorCode::NotSupported, "the view has no items");
}


void
BView::SetLiveRegion(AccessibleLive live)
{
	fLiveRegion = live;
}


AccessibleLive
BView::LiveRegion() const
{
	return fLiveRegion;
}


void
BView::SetAccessibleName(std::string_view name)
{
	fAccessibleName = name;
}


const std::string&
BView::AccessibleName() const
{
	return fAccessibleName;
}


void
BView::SetAccessibleDescription(std::string_view description)
{
	fAccessibleDescription = description;
}


const std::string&
BView::AccessibleDescription() const
{
	return fAccessibleDescription;
}


void
BView::SetInputPrivate(bool isPrivate)
{
	fInputPrivate = isPrivate;
}


bool
BView::IsInputPrivate() const
{
	return fInputPrivate;
}


void
BView::ShowToolTip(std::shared_ptr<BToolTip> tip)
{
	if (tip == nullptr || fWindow == nullptr)
		return;

	// NewGUI: where the window saw the mouse last (Haiku's GetMouse())
	const gfx::Point where = fWindow->fLastMouse.value_or(ConvertToScreen(gfx::Point{0, 0}));
	BToolTipManager::Manager()->ShowTip(std::move(tip), where, this);
}


void
BView::HideToolTip()
{
	// NewGUI: only the tip shown for this view, also one of GetToolTipAt()
	// (Haiku: "TODO: Only hide if ours is the tooltip that's showing!")
	if (fWindow != nullptr && fWindow->fToolTip != nullptr
		&& fWindow->fToolTip->Owner() == this) {
		fWindow->fToolTip->Hide();
	}
}


bool
BView::GetToolTipAt(gfx::Point /*point*/, std::shared_ptr<BToolTip>* tip)
{
	*tip = fToolTip;
	return fToolTip != nullptr;
}


// #pragma mark - Layout Functions
//
// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69), the
// layout part. NewGUI: the view owns its layout (unique_ptr); the layout
// data are members instead of a BView::LayoutData; a top-level view tells
// its window to lay out before the next frame instead of posting
// B_LAYOUT_WINDOW.


BLayout*
BView::SetLayout(std::unique_ptr<BLayout> layout)
{
	fFlags |= BViewFlag::SupportsLayout;

	// unset and delete the old layout
	_DropLayout();

	fLayout = std::move(layout);

	if (fLayout) {
		fLayout->SetOwner(this);

		// add all children
		for (const std::unique_ptr<BView>& child : fChildren)
			(void)fLayout->AddView(child.get());
	}

	InvalidateLayout();
	return fLayout.get();
}


BLayout*
BView::GetLayout() const
{
	return fLayout.get();
}


gfx::Size
BView::MinSize()
{
	// TODO: make sure this works correctly when some methods are overridden
	float width;
	float height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(fMinSize,
		(fLayout ? fLayout->MinSize() : gfx::Size{width, height}));
}


gfx::Size
BView::MaxSize()
{
	return BLayoutUtils::ComposeSize(fMaxSize,
		(fLayout ? fLayout->MaxSize() : gfx::Size{kSizeUnlimited, kSizeUnlimited}));
}


gfx::Size
BView::PreferredSize()
{
	// TODO: make sure this works correctly when some methods are overridden
	float width;
	float height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(fPreferredSize,
		(fLayout ? fLayout->PreferredSize() : gfx::Size{width, height}));
}


BAlignment
BView::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(fAlignment,
		(fLayout ? fLayout->Alignment()
			: BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle}));
}


void
BView::GetPreferredSize(float* width, float* height)
{
	if (width != nullptr)
		*width = Bounds().Width();
	if (height != nullptr)
		*height = Bounds().Height();
}


void
BView::ResizeToPreferred()
{
	float width;
	float height;
	GetPreferredSize(&width, &height);

	ResizeTo(width, height);
}


void
BView::SetExplicitMinSize(gfx::Size size)
{
	fMinSize = size;
	InvalidateLayout();
}


void
BView::SetExplicitMaxSize(gfx::Size size)
{
	fMaxSize = size;
	InvalidateLayout();
}


void
BView::SetExplicitPreferredSize(gfx::Size size)
{
	fPreferredSize = size;
	InvalidateLayout();
}


void
BView::SetExplicitSize(gfx::Size size)
{
	fMinSize = size;
	fMaxSize = size;
	fPreferredSize = size;
	InvalidateLayout();
}


void
BView::SetExplicitAlignment(BAlignment alignment)
{
	fAlignment = alignment;
	InvalidateLayout();
}


gfx::Size
BView::ExplicitMinSize() const
{
	return fMinSize;
}


gfx::Size
BView::ExplicitMaxSize() const
{
	return fMaxSize;
}


gfx::Size
BView::ExplicitPreferredSize() const
{
	return fPreferredSize;
}


BAlignment
BView::ExplicitAlignment() const
{
	return fAlignment;
}


bool
BView::HasHeightForWidth()
{
	return fLayout ? fLayout->HasHeightForWidth() : false;
}


void
BView::GetHeightForWidth(float width, float* min, float* max, float* preferred)
{
	if (fLayout)
		fLayout->GetHeightForWidth(width, min, max, preferred);
}


void
BView::InvalidateLayout(bool descendants)
{
	if (!fMinMaxValid || fLayoutInProgress || fLayoutInvalidationDisabled > 0)
		return;

	fLayoutValid = false;
	fMinMaxValid = false;
	LayoutInvalidated(descendants);

	if (descendants) {
		for (const std::unique_ptr<BView>& child : fChildren)
			child->InvalidateLayout(descendants);
	}

	if (fLayout)
		fLayout->InvalidateLayout(descendants);
	else
		_InvalidateParentLayout();

	if (fParent == nullptr && fWindow != nullptr)
		fWindow->_LayoutInvalidated();
}


void
BView::EnableLayoutInvalidation()
{
	if (fLayoutInvalidationDisabled > 0)
		fLayoutInvalidationDisabled--;
}


void
BView::DisableLayoutInvalidation()
{
	fLayoutInvalidationDisabled++;
}


bool
BView::IsLayoutInvalidationDisabled() const
{
	return fLayoutInvalidationDisabled > 0;
}


bool
BView::IsLayoutValid() const
{
	return fLayoutValid;
}


void
BView::ResetLayoutInvalidation()
{
	fMinMaxValid = true;
}


BLayoutContext*
BView::LayoutContext() const
{
	return fLayoutContext;
}


void
BView::Layout(bool force)
{
	BLayoutContext context;
	_Layout(force, &context);
}


void
BView::Relayout()
{
	if (fLayoutValid && !fLayoutInProgress) {
		fNeedsRelayout = true;
		if (fLayout)
			fLayout->RequireLayout();

		// Layout() is recursive, that is if the parent view is currently laid
		// out, we don't call layout() on this view, but wait for the parent's
		// Layout() to do that for us.
		if (!fParent || !fParent->fLayoutInProgress)
			Layout(false);
	}
}


void
BView::LayoutInvalidated(bool /*descendants*/)
{
	// hook method
}


void
BView::DoLayout()
{
	if (fLayout)
		fLayout->_LayoutWithinContext(false, LayoutContext());
}


void
BView::LayoutChanged()
{
	// hook method
}


void
BView::_Layout(bool force, BLayoutContext* context)
{
	if (fNeedsRelayout || !fLayoutValid || force) {
		fLayoutValid = false;

		if (fLayoutInProgress)
			return;

		BLayoutContext* oldContext = fLayoutContext;
		fLayoutContext = context;

		fLayoutInProgress = true;
		DoLayout();
		fLayoutInProgress = false;

		fLayoutValid = true;
		fMinMaxValid = true;
		fNeedsRelayout = false;

		// layout children
		for (const std::unique_ptr<BView>& child : fChildren) {
			if (child->fHideLevel <= 0)
				child->_Layout(force, context);
		}

		LayoutChanged();

		fLayoutContext = oldContext;

		// invalidate the drawn content, if requested
		if (fFlags.Has(BViewFlag::InvalidateAfterLayout))
			Invalidate();
	}
}


void
BView::_InvalidateParentLayout()
{
	if (!fParent)
		return;

	BLayout* layout = fLayout.get();
	BLayout* layoutParent = layout ? layout->Layout() : nullptr;
	if (layoutParent) {
		layoutParent->InvalidateLayout();
	} else if (!fLayoutItems.empty()) {
		// NewGUI: a copy, since invalidation may not change the list, but
		// should not depend on that.
		const std::vector<BLayoutItem*> items = fLayoutItems;
		for (BLayoutItem* item : items)
			item->Layout()->InvalidateLayout();
	} else {
		fParent->InvalidateLayout();
	}
}


bool
BView::_WillLayout() const
{
	if (fLayoutInProgress)
		return false;
	if (fNeedsRelayout || !fLayoutValid || !fMinMaxValid)
		return true;
	return false;
}


void
BView::_RemoveLayoutItemsFromLayout()
{
	// Removes the items from their layouts and destroys them.
	while (!fLayoutItems.empty()) {
		BLayoutItem* item = fLayoutItems.back();
		std::unique_ptr<BLayoutItem> removed = item->RemoveSelf();
		if (!removed) {
			// not in a layout: cannot be registered, but do not loop
			std::erase(fLayoutItems, item);
		}
	}
}


void
BView::_RegisterLayoutItem(BLayoutItem* item)
{
	fLayoutItems.push_back(item);
}


void
BView::_DeregisterLayoutItem(BLayoutItem* item)
{
	std::erase(fLayoutItems, item);
}


int
BView::_HideLevel() const
{
	return fHideLevel;
}


void
BView::_DropLayout()
{
	if (fLayout) {
		// The layout's last invalidation must not reach the view, which may
		// be in its destructor; SetLayout() invalidates afterwards anyway.
		DisableLayoutInvalidation();
		fLayout->SetOwner(nullptr);
		fLayout.reset();
		EnableLayoutInvalidation();
	}
}


BHandler*
BView::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
	std::uint32_t form, std::string_view property)
{
	// Ported from Haiku: src/kits/interface/View.cpp (commit b1d497bb69).
	const std::int32_t match = BPropertyInfo(kViewProperties).FindMatch(*message, index, form,
		property);
	if (match >= 0 && match <= 3)
		return this;
	if (match != 4)
		return BHandler::ResolveSpecifier(message, index, specifier, form, property);
	BView* child = nullptr;
	if (form == kNameSpecifier)
		child = FindView(specifier.GetString("name", ""));
	else
		child = ChildAt(detail::SpecifiedIndex(specifier, form, CountChildren()));
	if (child == nullptr) {
		ReplyScriptingError(*message, form == kNameSpecifier
			? Error{ErrorCode::NotFound, "no view \"" + std::string(specifier.GetString("name",
				"")) + "\""}
			: Error{ErrorCode::OutOfRange, "view index out of range"});
		return nullptr;
	}
	(void)message->PopSpecifier();
	return child;
}


std::expected<void, Error>
BView::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-view"); !added)
		return added;
	if (auto added = data->AddMessage("messages", BPropertyInfo(kViewProperties).ToMessage());
			!added) {
		return added;
	}
	return BHandler::GetSupportedSuites(data);
}


} // namespace ngui
