/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include <ngui/View.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Paint.h>


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
	BView({0, 0, 0, 0}, name, kFollowLeftTop, flags)
{
}


BView::~BView()
{
	assert(fParent == nullptr && "BView destroyed while in a parent; RemoveChild() it first");
	for (std::unique_ptr<BView>& child : fChildren)
		child->fParent = nullptr;
}


BView*
BView::AddChild(std::unique_ptr<BView> child, BView* before)
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
	// The list covers the whole view; a new size needs a new one.
	Invalidate();
	for (std::unique_ptr<BView>& child : fChildren)
		child->_ParentResized(dw, dh);
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
	if (dx == 0 && dy == 0)
		return;
	fScroll = {fScroll.x + dx, fScroll.y + dy};
	Invalidate();
}


void
BView::ScrollTo(gfx::Point where)
{
	ScrollBy(where.x - fScroll.x, where.y - fScroll.y);
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
	// Up to the root view, whose frame is the content area at (0, 0).
	for (const BView* view = this; view != nullptr; view = view->fParent)
		point = view->ConvertToParent(point);
	return point;
}


gfx::Point
BView::ConvertFromWindow(gfx::Point point) const
{
	if (fParent != nullptr)
		point = fParent->ConvertFromWindow(point);
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
	const gfx::Rect frame = fWindow->Frame();
	return {inWindow.x + frame.x0, inWindow.y + frame.y0};
}


gfx::Point
BView::ConvertFromScreen(gfx::Point point) const
{
	if (fWindow != nullptr) {
		const gfx::Rect frame = fWindow->Frame();
		point = {point.x - frame.x0, point.y - frame.y0};
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
	}
}


void
BView::Hide()
{
	fHideLevel++;
	_Damage(fFrame);
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
	// areas of the next frame in its own.
	if (fWindow == nullptr)
		return;
	fWindow->_AddDirty(fParent != nullptr ? fParent->ConvertToWindow(rect) : rect);
}


bool
BView::IsValid() const
{
	return fList != nullptr;
}


void
BView::SetViewColor(std::optional<gfx::Color> color)
{
	if (color != fViewColor)
		Invalidate();
	fViewColor = color;
}


std::optional<gfx::Color>
BView::ViewColor() const
{
	return fViewColor;
}


gfx::DisplayListRef
BView::_RecordOwn(const DrawEnvironment& environment)
{
	gfx::Canvas canvas(Bounds());
	if (fViewColor.has_value())
		canvas.FillRect(Bounds(), gfx::Paint{.color = *fViewColor});
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
	if (fWindow == nullptr)
		return;
	if (focus)
		fWindow->_SetFocus(this);
	else if (IsFocus())
		fWindow->_SetFocus(nullptr);
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


} // namespace ngui
