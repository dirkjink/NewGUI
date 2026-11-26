/*
 * Copyright 2001-2022 Haiku, Inc. All Rights Reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Box.cpp (commit b1d497bb69),
 * without archiving and scripting. NewGUI (see Box.h):
 * - Sizes and the layout data are pixel counts (Haiku's plus 1); the label
 *   box and the border are drawn on Haiku's pixels.
 * - The label view is added outside the layout (BView::_AddChild()), and
 *   SetLayout() keeps it out; DoLayout() therefore does not take it out and
 *   put it back.
 * - Draw() clips the label out with Canvas::ClipOutRect(); the label is
 *   bold (Haiku sets be_bold_font), measured with TextSystem::Default().
 * - The view color is the panel background, or the parent's when attached
 *   (AdoptParentColors()).
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		DarkWyrm, bpmagic@columbus.rr.com
 *		Axel Dörfler, axeld@pinc-software.de
 *		Marc Flerackers, mflerackers@androme.be
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/Box.h>

#include <cmath>

#include <ngui/ControlLook.h>
#include <ngui/Layout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>

#include "compat/CompatView.h"


namespace ngui {


using compat::BPoint;
using compat::BRect;
using compat::FromBRect;


namespace {


BRect
ToBRect(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1 - 1, rect.y1 - 1);
}


} // namespace


BBox::BBox(gfx::Rect frame, std::string_view name, BResizingMode resizingMode,
	BViewFlags flags, BBorderStyle border)
	:
	BView(frame, name, resizingMode, flags | BViewFlag::WillDraw | BViewFlag::FrameEvents),
	fStyle(border)
{
	_InitObject();
}


BBox::BBox(std::string_view name, BViewFlags flags, BBorderStyle border,
	std::unique_ptr<BView> child)
	:
	BView(name, flags | BViewFlag::WillDraw | BViewFlag::FrameEvents),
	fStyle(border)
{
	_InitObject();

	if (child)
		AddChild(std::move(child));
}


BBox::BBox(BBorderStyle border, std::unique_ptr<BView> child)
	:
	BView(std::string_view{},
		BViewFlag::WillDraw | BViewFlag::FrameEvents | BViewFlag::NavigableJump),
	fStyle(border)
{
	_InitObject();

	if (child)
		AddChild(std::move(child));
}


BBox::~BBox()
{
	_ClearLabel();
}


void
BBox::SetBorder(BBorderStyle border)
{
	if (border == fStyle)
		return;

	fStyle = border;

	InvalidateLayout();
	Invalidate();
}


BBorderStyle
BBox::Border() const
{
	return fStyle;
}


//! This function is not part of the R5 API and is not yet finalized yet
float
BBox::TopBorderOffset()
{
	_ValidateLayoutData();

	if (!fLabel.empty() || fLabelView != nullptr)
		return ToBRect(fLayoutData.labelBox).Height() / 2;

	return 0;
}


//! This function is not part of the R5 API and is not yet finalized yet
gfx::Rect
BBox::InnerFrame()
{
	_ValidateLayoutData();

	gfx::Rect frame(Bounds());
	frame.x0 += fLayoutData.insetLeft;
	frame.y0 += fLayoutData.insetTop;
	frame.x1 -= fLayoutData.insetRight;
	frame.y1 -= fLayoutData.insetBottom;

	return frame;
}


void
BBox::SetLabel(std::string_view string)
{
	_ClearLabel();

	fLabel = string;

	InvalidateLayout();
	Invalidate();
}


void
BBox::SetLabel(std::unique_ptr<BView> viewLabel)
{
	_ClearLabel();

	if (viewLabel) {
		viewLabel->MoveTo({10.0f, 0.0f});
		// NewGUI: not into the layout
		fLabelView = _AddChild(std::move(viewLabel), ChildAt(0));
	}

	InvalidateLayout();
	Invalidate();
}


const std::string&
BBox::Label() const
{
	return fLabel;
}


BView*
BBox::LabelView() const
{
	return fLabelView;
}


void
BBox::Draw(DrawContext& context)
{
	_ValidateLayoutData();

	gfx::Canvas& canvas = context.canvas;
	canvas.Save();

	gfx::Rect labelBox{0, 0, 1, 1};
	if (!fLabel.empty()) {
		labelBox = fLayoutData.labelBox;
		canvas.ClipOutRect(labelBox);
	} else if (fLabelView != nullptr)
		labelBox = fLabelView->Bounds();

	switch (fStyle) {
		case BBorderStyle::Fancy:
			_DrawFancy(context, labelBox);
			break;

		case BBorderStyle::Plain:
			_DrawPlain(context, labelBox);
			break;

		default:
			break;
	}

	canvas.Restore();

	if (!fLabel.empty()) {
		const FontHeight fontHeight = context.text.Height(FontRole::Bold);

		// offset label up by 1/6 the font height
		float lineHeight = fontHeight.ascent + fontHeight.descent;
		float yOffset = std::round(lineHeight / 6.0f);

		const gfx::Paint paint{.color = context.palette.Get(BColorWhich::PanelText)};
		const ShapedText shaped = context.text.Shape(fLabel, FontRole::Bold);
		shaped.Draw(canvas, {10.0f, fontHeight.ascent - yOffset}, paint);
	}
}


void
BBox::AttachedToWindow()
{
	BView::AttachedToWindow();

	// AdoptParentColors(); the system colors are already set
	if (Parent() != nullptr && Parent()->ViewColor().has_value())
		SetViewColor(Parent()->ViewColor());
}


void
BBox::FrameResized(float width, float height)
{
	BView::FrameResized(width, height);

	// NewGUI: the whole view is recorded anew (11.4), no invalid areas.
	if (fStyle != BBorderStyle::None)
		Invalidate();
}


void
BBox::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);

	// make sure the box don't get smaller than it already is
	if (width < Bounds().Width())
		width = Bounds().Width();
	if (height < Bounds().Height())
		height = Bounds().Height();

	BView::ResizeTo(width, height);
}


void
BBox::GetPreferredSize(float* _width, float* _height)
{
	_ValidateLayoutData();

	if (_width)
		*_width = fLayoutData.preferred.w;
	if (_height)
		*_height = fLayoutData.preferred.h;
}


gfx::Size
BBox::MinSize()
{
	_ValidateLayoutData();

	gfx::Size size = (GetLayout() ? GetLayout()->MinSize() : fLayoutData.min);
	if (size.w < fLayoutData.min.w)
		size.w = fLayoutData.min.w;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


gfx::Size
BBox::MaxSize()
{
	_ValidateLayoutData();

	gfx::Size size = (GetLayout() ? GetLayout()->MaxSize() : fLayoutData.max);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


gfx::Size
BBox::PreferredSize()
{
	_ValidateLayoutData();

	gfx::Size size = (GetLayout() ? GetLayout()->PreferredSize() : fLayoutData.preferred);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


BAlignment
BBox::LayoutAlignment()
{
	_ValidateLayoutData();

	BAlignment alignment = (GetLayout() ? GetLayout()->Alignment() : fLayoutData.alignment);
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(), alignment);
}


BLayout*
BBox::SetLayout(std::unique_ptr<BLayout> layout)
{
	// NewGUI: the label view stays out of the layout; BView::SetLayout()
	// adds all children.
	std::unique_ptr<BView> labelView;
	if (fLabelView != nullptr)
		labelView = RemoveChild(fLabelView);

	BLayout* result = BView::SetLayout(std::move(layout));

	if (labelView != nullptr) {
		DisableLayoutInvalidation();
		fLabelView = _AddChild(std::move(labelView), ChildAt(0));
		EnableLayoutInvalidation();
	}

	return result;
}


void
BBox::LayoutInvalidated(bool /*descendants*/)
{
	fLayoutData.valid = false;
}


void
BBox::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return;

	BLayout* layout = GetLayout();

	// If the user set a layout, let the base class version call its hook.
	// NewGUI: the label view is not in the layout, so it need not be taken
	// out and put back.
	if (layout != nullptr)
		BView::DoLayout();

	_ValidateLayoutData();

	// Even if the user set a layout, restore label view to it's
	// desired position.

	// layout the label view
	if (fLabelView != nullptr) {
		fLabelView->MoveTo({fLayoutData.labelBox.x0, fLayoutData.labelBox.y0});
		fLabelView->ResizeTo(fLayoutData.labelBox.Width(), fLayoutData.labelBox.Height());
	}

	// If we have layout return here and do not layout the child
	if (layout != nullptr)
		return;

	// layout the child
	BView* child = _Child();
	if (child != nullptr) {
		gfx::Rect frame = InnerFrame();
		if (child->Flags().Has(BViewFlag::SupportsLayout))
			BLayoutUtils::AlignInFrame(child, frame);
		else
			child->MoveTo({frame.x0, frame.y0});
	}
}


// #pragma mark - private


void
BBox::_InitObject()
{
	fLabel.clear();
	fLabelView = nullptr;
	fLayoutData = LayoutData();

	// AdoptSystemColors()
	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));
}


void
BBox::_DrawPlain(DrawContext& context, gfx::Rect /*labelBox*/)
{
	compat::CompatView view(context);
	BRect rect = ToBRect(Bounds());
	rect.top += TopBorderOffset();

	float lightTint;
	float shadowTint;
	lightTint = compat::B_LIGHTEN_1_TINT;
	shadowTint = compat::B_DARKEN_1_TINT;

	const compat::rgb_color viewColor = compat::ToRgbColor(ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground)));

	if (rect.Height() == 0.0f || rect.Width() == 0.0f) {
		// used as separator
		compat::rgb_color shadow = compat::tint_color(viewColor, compat::B_DARKEN_2_TINT);

		view.SetHighColor(shadow);
		view.StrokeLine(rect.LeftTop(), rect.RightBottom());
	} else {
		// used as box
		compat::rgb_color light = compat::tint_color(viewColor, lightTint);
		compat::rgb_color shadow = compat::tint_color(viewColor, shadowTint);

		view.BeginLineArray(4);
			view.AddLine(BPoint(rect.left, rect.bottom),
					BPoint(rect.left, rect.top), light);
			view.AddLine(BPoint(rect.left + 1.0f, rect.top),
					BPoint(rect.right, rect.top), light);
			view.AddLine(BPoint(rect.left + 1.0f, rect.bottom),
					BPoint(rect.right, rect.bottom), shadow);
			view.AddLine(BPoint(rect.right, rect.bottom - 1.0f),
					BPoint(rect.right, rect.top + 1.0f), shadow);
		view.EndLineArray();
	}
}


void
BBox::_DrawFancy(DrawContext& context, gfx::Rect /*labelBox*/)
{
	BRect rect = ToBRect(Bounds());
	rect.top += TopBorderOffset();

	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	gfx::Rect frame = FromBRect(rect);
	if (rect.Height() == 1.0f) {
		// used as horizontal separator
		Look().DrawGroupFrame(context, frame, base, Border::Top);
	} else if (rect.Width() == 1.0f) {
		// used as vertical separator
		Look().DrawGroupFrame(context, frame, base, Border::Left);
	} else {
		// used as box
		Look().DrawGroupFrame(context, frame, base);
	}
}


void
BBox::_ClearLabel()
{
	if (!fLabel.empty()) {
		fLabel.clear();
	} else if (fLabelView != nullptr) {
		std::unique_ptr<BView> removed = fLabelView->RemoveSelf();
		fLabelView = nullptr;
	}
}


BView*
BBox::_Child() const
{
	for (std::int32_t i = 0; BView* view = ChildAt(i); i++) {
		if (view != fLabelView)
			return view;
	}

	return nullptr;
}


void
BBox::_ValidateLayoutData()
{
	if (fLayoutData.valid)
		return;

	// compute the label box, width and height
	bool label = true;
	float labelHeight = 0;	// height of the label (pixel count)
	if (!fLabel.empty()) {
		// leave 6 pixels of the frame, and have a gap of 4 pixels between
		// the frame and the text on either side
		const TextSystem& text = TextSystem::Default();
		const FontHeight fontHeight = text.Height(FontRole::Bold);
		fLayoutData.labelBox = FromBRect(BRect(6.0f, 0, 14.0f
			+ text.StringWidth(fLabel, FontRole::Bold), std::ceil(fontHeight.ascent)));
		labelHeight = std::ceil(fontHeight.ascent + fontHeight.descent) + 1;
	} else if (fLabelView != nullptr) {
		// the label view is placed at (10, 0) at its preferred size
		gfx::Size size = fLabelView->PreferredSize();
		fLayoutData.labelBox = {10, 0, 10 + size.w, size.h};
		labelHeight = size.h;
	} else
		label = false;

	// border
	switch (fStyle) {
		case BBorderStyle::Plain:
			fLayoutData.insetLeft = fLayoutData.insetTop = 1;
			fLayoutData.insetRight = fLayoutData.insetBottom = 1;
			break;
		case BBorderStyle::Fancy:
			fLayoutData.insetLeft = fLayoutData.insetTop = 3;
			fLayoutData.insetRight = fLayoutData.insetBottom = 3;
			break;
		case BBorderStyle::None:
		default:
			fLayoutData.insetLeft = fLayoutData.insetTop = 0;
			fLayoutData.insetRight = fLayoutData.insetBottom = 0;
			break;
	}

	// if there's a label, the top inset will be dictated by the label
	if (label && labelHeight > fLayoutData.insetTop)
		fLayoutData.insetTop = labelHeight;

	// total number of pixel the border adds
	float addWidth = fLayoutData.insetLeft + fLayoutData.insetRight;
	float addHeight = fLayoutData.insetTop + fLayoutData.insetBottom;

	// compute the minimal width induced by the label (NewGUI: pixel counts)
	float minWidth;
	if (label)
		minWidth = fLayoutData.labelBox.x1 + fLayoutData.insetRight;
	else
		minWidth = addWidth;

	BAlignment alignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle};

	// finally consider the child constraints, if we shall support layout
	BView* child = _Child();
	if (child && child->Flags().Has(BViewFlag::SupportsLayout)) {
		gfx::Size min = child->MinSize();
		gfx::Size max = child->MaxSize();
		gfx::Size preferred = child->PreferredSize();

		min.w += addWidth;
		min.h += addHeight;
		preferred.w += addWidth;
		preferred.h += addHeight;
		max.w = BLayoutUtils::AddDistances(max.w, addWidth);
		max.h = BLayoutUtils::AddDistances(max.h, addHeight);

		if (min.w < minWidth)
			min.w = minWidth;
		BLayoutUtils::FixSizeConstraints(min, max, preferred);

		fLayoutData.min = min;
		fLayoutData.max = max;
		fLayoutData.preferred = preferred;

		BAlignment childAlignment = child->LayoutAlignment();
		if (childAlignment.horizontal == BHorizontalAlignment::UseFullWidth)
			alignment.horizontal = BHorizontalAlignment::UseFullWidth;
		if (childAlignment.vertical == BVerticalAlignment::UseFullHeight)
			alignment.vertical = BVerticalAlignment::UseFullHeight;

		fLayoutData.alignment = alignment;
	} else {
		fLayoutData.min = {minWidth, addHeight};
		fLayoutData.max = {kSizeUnlimited, kSizeUnlimited};
		fLayoutData.preferred = fLayoutData.min;
		fLayoutData.alignment = alignment;
	}

	fLayoutData.valid = true;
	ResetLayoutInvalidation();
}


// #pragma mark - accessibility


void
BBox::GetAccessibleInfo(AccessibleInfo* info) const
{
	BView::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	info->role = AccessibleRole::Group;
	if (info->name.empty())
		info->name = Label();
}


} // namespace ngui
