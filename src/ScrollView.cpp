/*
 * Copyright 2004-2015, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2009 Stephan Aßmus, superstippi@gmx.de.
 * Copyright 2014-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/ScrollView.cpp (commit
 * b1d497bb69), without archiving and scripting. NewGUI (see ScrollView.h):
 * - The geometry is Haiku's: computed with inclusive rectangles and
 *   Haiku's sizes (compat::BRect, pixels - 1) and converted where views
 *   are moved, resized or asked for their sizes (pixel counts).
 * - The whole view is recorded anew instead of invalidating the changed
 *   border areas (11.4), so the previous size is not kept.
 * - No adjustment for the resize knob of B_DOCUMENT_WINDOW_LOOK.
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Axel Dörfler, axeld@pinc-software.de
 *		John Scipione, jscpione@gmail.com
 */


#include <ngui/ScrollView.h>

#include <algorithm>

#include <ngui/LayoutUtils.h>
#include <ngui/Palette.h>
#include <ngui/ScrollBar.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

#include "compat/CompatView.h"


namespace ngui {


using compat::BRect;
using compat::FromBRect;


namespace {


constexpr float kFancyBorderSize = 2;
constexpr float kPlainBorderSize = 1;


BRect
ToBRect(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


// Haiku's sizes (pixels - 1) of a view.
float
HaikuPreferredWidth(BScrollBar* bar)
{
	return bar->PreferredSize().w - 1;
}


float
HaikuPreferredHeight(BScrollBar* bar)
{
	return bar->PreferredSize().h - 1;
}


// Moves \a view to the inclusive \a rect and makes it as large.
void
PlaceView(BView* view, const BRect& rect)
{
	view->MoveTo({rect.left, rect.top});
	view->ResizeTo(rect.Width() + 1, rect.Height() + 1);
}


/*! This method returns the size of the specified border.
*/
float
BorderSize(BBorderStyle border)
{
	if (border == BBorderStyle::Fancy)
		return kFancyBorderSize;
	if (border == BBorderStyle::Plain)
		return kPlainBorderSize;

	return 0;
}


void
InsetBorders(BRect& frame, BBorderStyle border, BorderFlags borders, bool expand = false)
{
	float borderSize = BorderSize(border);
	if (expand)
		borderSize = -borderSize;
	if (borders.Has(Border::Left))
		frame.left += borderSize;
	if (borders.Has(Border::Top))
		frame.top += borderSize;
	if (borders.Has(Border::Right))
		frame.right -= borderSize;
	if (borders.Has(Border::Bottom))
		frame.bottom -= borderSize;
}


/*!	This static method is used to calculate the frame that the
	ScrollView will cover depending on the frame of its target
	and which border style is used.
	It is used in the constructor and at other places.
*/
BRect
ComputeFrame(BRect frame, BScrollBar* horizontal, BScrollBar* vertical,
	BBorderStyle border, BorderFlags borders)
{
	if (vertical != nullptr)
		frame.right += HaikuPreferredWidth(vertical);
	if (horizontal != nullptr)
		frame.bottom += HaikuPreferredHeight(horizontal);

	// Take the other minimum dimensions into account, too, but only if
	// the frame already has a greater-than-zero value for them. Otherwise,
	// non-layouted applications could wind up with broken layouts.
	if (vertical != nullptr) {
		const float minHeight = vertical->MinSize().h - 1;
		if (frame.Height() > 0 && frame.Height() < minHeight)
			frame.bottom += minHeight - frame.Height();
	}
	if (horizontal != nullptr) {
		const float minWidth = horizontal->MinSize().w - 1;
		if (frame.Width() > 0 && frame.Width() < minWidth)
			frame.right += minWidth - frame.Width();
	}

	InsetBorders(frame, border, borders, true);

	if (BorderSize(border) == 0) {
		if (vertical != nullptr)
			frame.right++;
		if (horizontal != nullptr)
			frame.bottom++;
	}

	return frame;
}


/*!	This method changes the "flags" argument as passed on to
	the BView constructor.
*/
BViewFlags
ModifyFlags(BViewFlags flags, BView* target, BBorderStyle border)
{
	if (target != nullptr && target->Flags().Has(BViewFlag::SupportsLayout)
			&& !target->Flags().Has(BViewFlag::ScrollViewAware))
		flags |= BViewFlag::FrameEvents;

	// We either need B_FULL_UPDATE_ON_RESIZE or B_FRAME_EVENTS if we have
	// to draw a border.
	if (border != BBorderStyle::None) {
		flags |= BViewFlag::WillDraw;
		if (!flags.Has(BViewFlag::FullUpdateOnResize))
			flags |= BViewFlag::FrameEvents;
	}

	return flags;
}


} // namespace


BScrollView::BScrollView(std::string_view name, std::unique_ptr<BView> target,
	BResizingMode resizingMode, BViewFlags flags, bool horizontal, bool vertical,
	BBorderStyle border)
	:
	BView(gfx::Rect{}, name, resizingMode, ModifyFlags(flags, target.get(), border)),
	fBorder(border)
{
	_Init(std::move(target), horizontal, vertical);
}


BScrollView::BScrollView(std::string_view name, std::unique_ptr<BView> target,
	BViewFlags flags, bool horizontal, bool vertical, BBorderStyle border)
	:
	BView(name, ModifyFlags(flags, target.get(), border)),
	fBorder(border)
{
	_Init(std::move(target), horizontal, vertical);
}


BScrollView::~BScrollView()
{
}


// #pragma mark - Hook methods


void
BScrollView::Draw(DrawContext& context)
{
	ControlStateFlags flags;
	if (fHighlighted && Window() != nullptr && Window()->IsActive())
		flags |= ControlState::Focused;

	gfx::Rect rect(Bounds());
	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);

	gfx::Rect verticalScrollBarFrame{0, 0, 0, 0};
	if (fVerticalScrollBar)
		verticalScrollBarFrame = fVerticalScrollBar->Frame();

	gfx::Rect horizontalScrollBarFrame{0, 0, 0, 0};
	if (fHorizontalScrollBar)
		horizontalScrollBarFrame = fHorizontalScrollBar->Frame();

	Look().DrawScrollViewFrame(context, rect, verticalScrollBarFrame,
		horizontalScrollBarFrame, base, fBorder, flags, fBorders);
}


void
BScrollView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	const BRect bounds = ToBRect(Bounds());

	if (fTarget != nullptr && fTarget->Flags().Has(BViewFlag::SupportsLayout)
			&& !fTarget->Flags().Has(BViewFlag::ScrollViewAware)) {
		// NewGUI: Haiku's size (pixels - 1)
		const gfx::Size preferred = fTarget->PreferredSize();
		const gfx::Size size{preferred.w - 1, preferred.h - 1};
		const float fontSize = TextSystem::Default().FontSize(FontRole::Plain);
		if (fHorizontalScrollBar != nullptr) {
			float delta = size.w - bounds.Width(),
				proportion = bounds.Width() / size.w;
			if (delta < 0)
				delta = 0;

			fHorizontalScrollBar->SetRange(0, delta);
			fHorizontalScrollBar->SetSteps(fontSize * 1.33f, bounds.Width());
			fHorizontalScrollBar->SetProportion(proportion);
		}
		if (fVerticalScrollBar != nullptr) {
			float delta = size.h - bounds.Height(),
				proportion = bounds.Height() / size.h;
			if (delta < 0)
				delta = 0;

			fVerticalScrollBar->SetRange(0, delta);
			fVerticalScrollBar->SetSteps(fontSize * 1.33f, bounds.Height());
			fVerticalScrollBar->SetProportion(proportion);
		}
	}

	// NewGUI: the whole view is recorded anew (11.4).
	if (fBorder != BBorderStyle::None)
		Invalidate();
}


void
BScrollView::WindowActivated(bool active)
{
	if (fHighlighted)
		Invalidate();

	BView::WindowActivated(active);
}


// #pragma mark - Size methods


void
BScrollView::GetPreferredSize(float* _width, float* _height)
{
	gfx::Size size = PreferredSize();

	if (_width)
		*_width = size.w;

	if (_height)
		*_height = size.h;
}


void
BScrollView::ResizeToPreferred()
{
	if (Window() == nullptr)
		return;
	BView::ResizeToPreferred();
}


namespace {


// Haiku's _ComputeSize(): the size of the scroll view for a target of
// \a targetSize; both are Haiku's sizes.
gfx::Size
ComputeSize(gfx::Size targetSize, BScrollBar* horizontal, BScrollBar* vertical,
	BBorderStyle border, BorderFlags borders)
{
	BRect frame = ComputeFrame(BRect(0, 0, targetSize.w, targetSize.h), horizontal,
		vertical, border, borders);

	return {frame.Width(), frame.Height()};
}


// From Haiku's size to a pixel count, unlimited sizes kept.
gfx::Size
PixelCount(gfx::Size size)
{
	return {std::min(size.w + 1, kSizeUnlimited), std::min(size.h + 1, kSizeUnlimited)};
}


} // namespace


gfx::Size
BScrollView::MinSize()
{
	const gfx::Size target = fTarget != nullptr
		? gfx::Size{fTarget->MinSize().w - 1, fTarget->MinSize().h - 1} : gfx::Size{16, 16};
	gfx::Size size = PixelCount(ComputeSize(target, fHorizontalScrollBar,
		fVerticalScrollBar, fBorder, fBorders));

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


gfx::Size
BScrollView::MaxSize()
{
	const gfx::Size target = fTarget != nullptr
		? gfx::Size{fTarget->MaxSize().w - 1, fTarget->MaxSize().h - 1}
		: gfx::Size{kSizeUnlimited, kSizeUnlimited};
	gfx::Size size = PixelCount(ComputeSize(target, fHorizontalScrollBar,
		fVerticalScrollBar, fBorder, fBorders));

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


gfx::Size
BScrollView::PreferredSize()
{
	const gfx::Size target = fTarget != nullptr
		? gfx::Size{fTarget->PreferredSize().w - 1, fTarget->PreferredSize().h - 1}
		: gfx::Size{32, 32};
	gfx::Size size = PixelCount(ComputeSize(target, fHorizontalScrollBar,
		fVerticalScrollBar, fBorder, fBorders));

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


// #pragma mark - BScrollView methods


BScrollBar*
BScrollView::ScrollBar(BOrientation direction) const
{
	if (direction == BOrientation::Horizontal)
		return fHorizontalScrollBar;

	return fVerticalScrollBar;
}


void
BScrollView::SetBorder(BBorderStyle border)
{
	if (fBorder == border)
		return;

	if (Flags().Has(BViewFlag::SupportsLayout)) {
		fBorder = border;
		SetFlags(ModifyFlags(Flags(), fTarget, border));

		DoLayout();
		Invalidate();
		return;
	}

	float offset = BorderSize(fBorder) - BorderSize(border);
	float resize = 2 * offset;

	float horizontalGap = 0, verticalGap = 0;
	float change = 0;
	if (border == BBorderStyle::None || fBorder == BBorderStyle::None) {
		if (fHorizontalScrollBar != nullptr)
			verticalGap = border != BBorderStyle::None ? 1.0f : -1.0f;
		if (fVerticalScrollBar != nullptr)
			horizontalGap = border != BBorderStyle::None ? 1.0f : -1.0f;

		change = border != BBorderStyle::None ? -1.0f : 1.0f;
		if (fHorizontalScrollBar == nullptr || fVerticalScrollBar == nullptr)
			change *= 2;
	}

	fBorder = border;

	BResizingMode savedResizingMode;
	if (fTarget != nullptr) {
		savedResizingMode = fTarget->ResizingMode();
		fTarget->SetResizingMode(kFollowLeftTop);
	}

	MoveBy(offset, offset);
	ResizeBy(-resize - horizontalGap, -resize - verticalGap);

	if (fTarget != nullptr) {
		fTarget->MoveBy(-offset, -offset);
		fTarget->SetResizingMode(savedResizingMode);
	}

	if (fHorizontalScrollBar != nullptr) {
		fHorizontalScrollBar->MoveBy(-offset - verticalGap, offset + verticalGap);
		fHorizontalScrollBar->ResizeBy(resize + horizontalGap - change, 0);
	}
	if (fVerticalScrollBar != nullptr) {
		fVerticalScrollBar->MoveBy(offset + horizontalGap, -offset - horizontalGap);
		fVerticalScrollBar->ResizeBy(0, resize + verticalGap - change);
	}

	SetFlags(ModifyFlags(Flags(), fTarget, border));
}


BBorderStyle
BScrollView::Border() const
{
	return fBorder;
}


void
BScrollView::SetBorders(BorderFlags borders)
{
	if (fBorders == borders || !Flags().Has(BViewFlag::SupportsLayout))
		return;

	fBorders = borders;
	DoLayout();
	Invalidate();
}


BorderFlags
BScrollView::Borders() const
{
	return fBorders;
}


std::expected<void, Error>
BScrollView::SetBorderHighlighted(bool highlight)
{
	if (fHighlighted == highlight)
		return {};

	if (fBorder != BBorderStyle::Fancy) {
		// highlighting only works for B_FANCY_BORDER
		return std::unexpected(Error{ErrorCode::NotSupported,
			"only a fancy border can be highlighted"});
	}

	fHighlighted = highlight;

	if (fHorizontalScrollBar != nullptr)
		fHorizontalScrollBar->SetBorderHighlighted(highlight);
	if (fVerticalScrollBar != nullptr)
		fVerticalScrollBar->SetBorderHighlighted(highlight);

	Invalidate();

	return {};
}


bool
BScrollView::IsBorderHighlighted() const
{
	return fHighlighted;
}


std::unique_ptr<BView>
BScrollView::SetTarget(std::unique_ptr<BView> target)
{
	if (fTarget == target.get())
		return nullptr;

	std::unique_ptr<BView> old;
	if (fTarget != nullptr) {
		fTarget->TargetedByScrollView(nullptr);
		old = RemoveChild(fTarget);
		// we are not supposed to delete the view
	}

	fTarget = target.get();
	if (fHorizontalScrollBar != nullptr)
		fHorizontalScrollBar->SetTarget(fTarget);
	if (fVerticalScrollBar != nullptr)
		fVerticalScrollBar->SetTarget(fTarget);

	if (target != nullptr) {
		float borderSize = BorderSize(fBorder);
		fTarget->MoveTo({fBorders.Has(Border::Left) ? borderSize : 0,
			fBorders.Has(Border::Top) ? borderSize : 0});

		// Haiku's _InnerFrame()
		BRect innerFrame = ToBRect(Bounds());
		InsetBorders(innerFrame, fBorder, fBorders);
		if (fHorizontalScrollBar != nullptr) {
			innerFrame.bottom -= HaikuPreferredHeight(fHorizontalScrollBar);
			if (borderSize == 0)
				innerFrame.bottom--;
		}
		if (fVerticalScrollBar != nullptr) {
			innerFrame.right -= HaikuPreferredWidth(fVerticalScrollBar);
			if (borderSize == 0)
				innerFrame.right--;
		}
		// NewGUI: as large as the inner frame (Haiku: one pixel smaller)
		fTarget->ResizeTo(innerFrame.Width() + 1, innerFrame.Height() + 1);
		fTarget->TargetedByScrollView(this);
		AddChild(std::move(target), ChildAt(0));
			// This way, we are making sure that the target will
			// be added top most in the list (which is important
			// for unarchiving)
	}

	SetFlags(ModifyFlags(Flags(), fTarget, fBorder));

	return old;
}


BView*
BScrollView::Target() const
{
	return fTarget;
}


//	#pragma mark - Protected methods


void
BScrollView::LayoutInvalidated(bool /*descendants*/)
{
}


namespace {


// Haiku's _AlignScrollBars().
void
AlignScrollBars(BScrollBar* horizontalBar, BScrollBar* verticalBar, BBorderStyle border,
	BRect targetFrame)
{
	const bool horizontal = horizontalBar != nullptr;
	const bool vertical = verticalBar != nullptr;
	if (horizontal) {
		BRect rect = targetFrame;
		rect.top = rect.bottom + 1;
		rect.bottom = rect.top + HaikuPreferredHeight(horizontalBar);
		if (border != BBorderStyle::None || vertical) {
			// extend scrollbar so that it overlaps one pixel with vertical
			// scrollbar
			rect.right++;
		}

		if (border != BBorderStyle::None) {
			// the scrollbar draws part of the surrounding frame on the left
			rect.left--;
		}

		PlaceView(horizontalBar, rect);
	}

	if (vertical) {
		BRect rect = targetFrame;
		rect.left = rect.right + 1;
		rect.right = rect.left + HaikuPreferredWidth(verticalBar);
		if (border != BBorderStyle::None || horizontal) {
			// extend scrollbar so that it overlaps one pixel with vertical
			// scrollbar
			rect.bottom++;
		}

		if (border != BBorderStyle::None) {
			// the scrollbar draws part of the surrounding frame on the left
			rect.top--;
		}

		PlaceView(verticalBar, rect);
	}
}


} // namespace


void
BScrollView::DoLayout()
{
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return;

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != nullptr) {
		BView::DoLayout();
		return;
	}

	// Haiku's _InnerFrame()
	BRect innerFrame = ToBRect(Bounds());
	InsetBorders(innerFrame, fBorder, fBorders);
	float borderSize = BorderSize(fBorder);
	if (fHorizontalScrollBar != nullptr) {
		innerFrame.bottom -= HaikuPreferredHeight(fHorizontalScrollBar);
		if (borderSize == 0)
			innerFrame.bottom--;
	}
	if (fVerticalScrollBar != nullptr) {
		innerFrame.right -= HaikuPreferredWidth(fVerticalScrollBar);
		if (borderSize == 0)
			innerFrame.right--;
	}

	if (fTarget != nullptr)
		PlaceView(fTarget, innerFrame);

	AlignScrollBars(fHorizontalScrollBar, fVerticalScrollBar, fBorder, innerFrame);
}


// #pragma mark - Private methods


void
BScrollView::_Init(std::unique_ptr<BView> target, bool horizontal, bool vertical)
{
	fTarget = target.get();
	fHorizontalScrollBar = nullptr;
	fVerticalScrollBar = nullptr;
	fHighlighted = false;
	fBorders = kAllBorders;

	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));

	if (horizontal) {
		fHorizontalScrollBar = static_cast<BScrollBar*>(AddChild(
			std::make_unique<BScrollBar>(gfx::Rect{0, 0, 15, 15}, "_HSB_", fTarget,
				0.0f, 1000.0f, BOrientation::Horizontal)));
	}

	if (vertical) {
		fVerticalScrollBar = static_cast<BScrollBar*>(AddChild(
			std::make_unique<BScrollBar>(gfx::Rect{0, 0, 15, 15}, "_VSB_", fTarget,
				0.0f, 1000.0f, BOrientation::Vertical)));
	}

	if (!Flags().Has(BViewFlag::SupportsLayout)) {
		BRect frame = ComputeFrame(fTarget != nullptr ? ToBRect(fTarget->Frame())
				: BRect(0, 0, 16, 16), fHorizontalScrollBar, fVerticalScrollBar, fBorder,
			kAllBorders);
		PlaceView(this, frame);
	}

	BRect targetFrame;
	if (fTarget) {
		// layout target and add it
		fTarget->TargetedByScrollView(this);
		fTarget->MoveTo({0, 0});

		if (fBorder != BBorderStyle::None)
			fTarget->MoveBy(BorderSize(fBorder), BorderSize(fBorder));

		AddChild(std::move(target));
		targetFrame = ToBRect(fTarget->Frame());
	} else {
		// no target specified
		targetFrame = ToBRect(Bounds());
		if (horizontal)
			targetFrame.bottom -= HaikuPreferredHeight(fHorizontalScrollBar) + 1;
		if (vertical)
			targetFrame.right -= HaikuPreferredWidth(fVerticalScrollBar) + 1;
		if (fBorder == BBorderStyle::Fancy) {
			targetFrame.bottom--;
			targetFrame.right--;
		}
	}

	AlignScrollBars(fHorizontalScrollBar, fVerticalScrollBar, fBorder, targetFrame);
}


// #pragma mark - accessibility


void
BScrollView::GetAccessibleInfo(AccessibleInfo* info) const
{
	BView::GetAccessibleInfo(info);
	if (info != nullptr)
		info->role = AccessibleRole::ScrollView;
}


} // namespace ngui
