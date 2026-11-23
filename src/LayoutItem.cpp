/*
 * Copyright 2010-2012, Haiku, Inc.
 * Copyright 2006, Ingo Weinhold <bonefish@cs.tu-berlin.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/LayoutItem.cpp (commit b1d497bb69),
 * without archiving. RemoveSelf() hands the item back (NewGUI: the layout
 * owns its items).
 */


#include <ngui/LayoutItem.h>

#include <algorithm>
#include <cmath>

#include <ngui/Layout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/View.h>


namespace ngui {


BLayoutItem::BLayoutItem()
{
}


BLayoutItem::~BLayoutItem()
{
}


BLayout*
BLayoutItem::Layout() const
{
	return fLayout;
}


std::unique_ptr<BLayoutItem>
BLayoutItem::RemoveSelf()
{
	return Layout() != nullptr ? Layout()->RemoveItem(this) : nullptr;
}


void
BLayoutItem::SetExplicitSize(gfx::Size size)
{
	SetExplicitMinSize(size);
	SetExplicitMaxSize(size);
	SetExplicitPreferredSize(size);
}


bool
BLayoutItem::HasHeightForWidth()
{
	// no "height for width" by default
	return false;
}


void
BLayoutItem::GetHeightForWidth(float /*width*/, float* /*min*/, float* /*max*/,
	float* /*preferred*/)
{
	// no "height for width" by default
}


BView*
BLayoutItem::View()
{
	return nullptr;
}


void
BLayoutItem::InvalidateLayout(bool children)
{
	LayoutInvalidated(children);
	if (fLayout)
		fLayout->InvalidateLayout(children);
}


void
BLayoutItem::Relayout(bool immediate)
{
	BView* view = View();
	if (view && !immediate)
		view->Relayout();
	else if (view && immediate)
		view->Layout(false);
}


void*
BLayoutItem::LayoutData() const
{
	return fLayoutData;
}


void
BLayoutItem::SetLayoutData(void* data)
{
	fLayoutData = data;
}


void
BLayoutItem::AlignInFrame(gfx::Rect frame)
{
	gfx::Size maxSize = MaxSize();
	BAlignment alignment = Alignment();

	if (HasHeightForWidth()) {
		// The item has height for width, so we do the horizontal alignment
		// ourselves and restrict the height max constraint respectively.
		if (maxSize.w < frame.Width()
			&& alignment.horizontal != BHorizontalAlignment::UseFullWidth) {
			frame.x0 += std::trunc((frame.Width() - maxSize.w)
				* BLayoutUtils::RelativeHorizontal(alignment));
			frame.x1 = frame.x0 + maxSize.w;
		}
		alignment.horizontal = BHorizontalAlignment::UseFullWidth;

		float minHeight = 0;
		GetHeightForWidth(frame.Width(), &minHeight, nullptr, nullptr);

		frame.y1 = frame.y0 + std::max(frame.Height(), minHeight);
		maxSize.h = minHeight;
	}

	SetFrame(BLayoutUtils::AlignInFrame(frame, maxSize, alignment));
}


void
BLayoutItem::SetLayout(BLayout* layout)
{
	if (layout == fLayout)
		return;

	BLayout* oldLayout = fLayout;
	fLayout = layout;

	if (oldLayout)
		DetachedFromLayout(oldLayout);

	if (BView* view = View()) {
		if (oldLayout && !fLayout) {
			view->_DeregisterLayoutItem(this);
		} else if (fLayout && !oldLayout) {
			view->_RegisterLayoutItem(this);
		}
	}

	if (fLayout)
		AttachedToLayout();
}


void
BLayoutItem::LayoutInvalidated(bool /*children*/)
{
	// hook method
}


void
BLayoutItem::AttachedToLayout()
{
	// hook method
}


void
BLayoutItem::DetachedFromLayout(BLayout* /*oldLayout*/)
{
	// hook method
}


void
BLayoutItem::AncestorVisibilityChanged(bool /*shown*/)
{
	// hook method
}


} // namespace ngui
