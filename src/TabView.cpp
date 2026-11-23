/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/TabView.cpp (commit b1d497bb69),
 * without archiving, scripting and BeOS compatibility. NewGUI (see
 * TabView.h):
 * - Ownership: the tab view owns its tabs; a tab owns its view while the
 *   view is not in the container view (Haiku: the tab owned it always).
 * - The geometry is Haiku's, computed with inclusive rectangles
 *   (compat::BRect) and given out half-open: TabFrame() covers the same
 *   pixels, neighbouring tabs share one. Sizes are pixel counts.
 * - Labels of tabs on the left or right are not rotated.
 * - fTabOffset is not ported: Haiku computes it in Select() but never
 *   uses it.
 * - RemoveTab() of the last tab also clears the selection; Haiku kept it,
 *   so that the next tab added was never selected.
 * - BTab::SetView() of a tab in a tab view with a layout puts the new view
 *   into the card layout (Haiku left it out, so the page stayed empty).
 *
 * Authors (Haiku):
 *		Marc Flerackers (mflerackers@androme.be)
 *		Jérôme Duval (korli@users.berlios.de)
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Artur Wyszynski
 *		Rene Gollent (rene@gollent.com)
 */


#include <ngui/TabView.h>

#include <algorithm>
#include <cmath>

#include <ngui/CardLayout.h>
#include <ngui/GroupLayout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>

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


// The tab side as the look's side.
Border
SideBorder(BTabSide side)
{
	switch (side) {
		case BTabSide::Left:
			return Border::Left;
		case BTabSide::Right:
			return Border::Right;
		case BTabSide::Top:
			return Border::Top;
		case BTabSide::Bottom:
			return Border::Bottom;
	}
	return Border::Top;
}


float
StringWidth(std::string_view text)
{
	return TextSystem::Default().StringWidth(text, FontRole::Plain);
}


// B_MOUSE_BUTTON(4) and B_MOUSE_BUTTON(5): back and forward.
constexpr std::int32_t kBackMouseButton = 1 << 3;
constexpr std::int32_t kForwardMouseButton = 1 << 4;


bool
IsLayouted(BView* childView)
{
	BView* container = childView->Parent();
	if (container != nullptr)
		return dynamic_cast<BCardLayout*>(container->GetLayout()) != nullptr;
	return false;
}


} // namespace


BTab::BTab(std::unique_ptr<BView> contentsView)
	:
	fView(contentsView.get()),
	fOwnedView(std::move(contentsView))
{
	if (fView != nullptr)
		fLabel = fView->Name();
}


BTab::~BTab()
{
	if (fView == nullptr)
		return;

	// NewGUI: a view it does not own is in the container view.
	if (fOwnedView == nullptr)
		(void)fView->RemoveSelf();
}


const std::string&
BTab::Label() const
{
	return fLabel;
}


void
BTab::SetLabel(std::string_view label)
{
	if (fView == nullptr)
		return;

	fLabel = label;

	if (fTabView != nullptr)
		fTabView->Invalidate();
}


bool
BTab::IsSelected() const
{
	return fSelected;
}


void
BTab::Select(BView* owner)
{
	fSelected = true;

	if (owner == nullptr || fView == nullptr)
		return;

	// NOTE: Views are not added/removed, if there is layout,
	// they are made visible/invisible in that case.
	if (owner->GetLayout() == nullptr && fView->Parent() == nullptr)
		owner->AddChild(std::move(fOwnedView));
}


void
BTab::Deselect()
{
	if (fView != nullptr) {
		// NOTE: Views are not added/removed, if there is layout,
		// they are made visible/invisible in that case.
		if (!IsLayouted(fView) && fView->Parent() != nullptr)
			fOwnedView = fView->RemoveSelf();
	}

	fSelected = false;
}


void
BTab::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (fView == nullptr || fTabView == nullptr)
		return;

	fTabView->Invalidate();
}


bool
BTab::IsEnabled() const
{
	return fEnabled;
}


void
BTab::MakeFocus(bool focus)
{
	fFocus = focus;
}


bool
BTab::IsFocus() const
{
	return fFocus;
}


void
BTab::SetView(std::unique_ptr<BView> view)
{
	if (view == nullptr || fView == view.get())
		return;

	// NewGUI: the card layout the old view is in, if any
	BCardLayout* cards = nullptr;
	if (fView != nullptr && IsLayouted(fView))
		cards = dynamic_cast<BCardLayout*>(fView->Parent()->GetLayout());

	if (fOwnedView != nullptr)
		fOwnedView.reset();
	else if (fView != nullptr)
		(void)fView->RemoveSelf();

	fView = view.get();
	fLabel = fView->Name();

	if (cards != nullptr && fTabView != nullptr) {
		// NewGUI: into the card layout, at the tab's place
		const int32 index = fTabView->IndexOf(this);
		cards->AddView(index, std::move(view));
		if (fSelected)
			cards->SetVisibleItem(index);
		fTabView->Invalidate();
		return;
	}

	fOwnedView = std::move(view);

	if (fTabView != nullptr && fSelected) {
		Select(fTabView->ContainerView());
		fTabView->Invalidate();
	}
}


BView*
BTab::View() const
{
	return fView;
}


void
BTab::DrawFocusMark(DrawContext& context, gfx::Rect halfOpen)
{
	if (fTabView == nullptr)
		return;

	const BRect frame = ToBRect(halfOpen);
	float width = context.text.StringWidth(Label(), FontRole::Plain);

	compat::CompatView owner(context);
	owner.SetHighColor(owner.UiColor(compat::B_KEYBOARD_NAVIGATION_COLOR));

	float offset = IsSelected() ? 3.0f : 2.0f;
	switch (fTabView->TabSide()) {
		case BTabSide::Top:
			owner.StrokeLine(BPoint((frame.left + frame.right - width) / 2.0f,
					frame.bottom - offset),
				BPoint((frame.left + frame.right + width) / 2.0f,
					frame.bottom - offset));
			break;
		case BTabSide::Bottom:
			owner.StrokeLine(BPoint((frame.left + frame.right - width) / 2.0f,
					frame.top + offset),
				BPoint((frame.left + frame.right + width) / 2.0f,
					frame.top + offset));
			break;
		case BTabSide::Left:
			owner.StrokeLine(BPoint(frame.right - offset,
					(frame.top + frame.bottom - width) / 2.0f),
				BPoint(frame.right - offset,
					(frame.top + frame.bottom + width) / 2.0f));
			break;
		case BTabSide::Right:
			owner.StrokeLine(BPoint(frame.left + offset,
					(frame.top + frame.bottom - width) / 2.0f),
				BPoint(frame.left + offset,
					(frame.top + frame.bottom + width) / 2.0f));
			break;
	}
}


void
BTab::DrawLabel(DrawContext& context, gfx::Rect frame)
{
	if (fTabView == nullptr)
		return;

	// NewGUI: not rotated for tabs on the left or right (no rotation in the
	// Canvas); Haiku turned the label by 270 or 90 degrees.
	const gfx::Color highColor = context.palette.Get(BColorWhich::PanelText);
	ControlStateFlags flags;
	if (!IsEnabled())
		flags |= ControlState::Disabled;
	fTabView->Look().DrawLabel(context, Label(), frame,
		context.palette.Get(BColorWhich::PanelBackground), flags,
		BAlignment{BHorizontalAlignment::Center, BVerticalAlignment::Middle}, &highColor);
}


void
BTab::DrawTab(DrawContext& context, gfx::Rect frame, BTabPosition, bool)
{
	if (fTabView == nullptr)
		return;

	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);
	ControlStateFlags flags;
	BorderFlags borders = _Borders(fTabView, frame);

	int32 index = fTabView->IndexOf(this);
	int32 selected = fTabView->Selection();
	int32 first = 0;
	int32 last = fTabView->CountTabs() - 1;

	const ControlLook& look = fTabView->Look();
	gfx::Rect rect = frame;
	if (index == selected) {
		look.DrawActiveTab(context, rect, base, flags, borders,
			SideBorder(fTabView->TabSide()), index, selected, first, last);
	} else {
		look.DrawInactiveTab(context, rect, base, flags, borders,
			SideBorder(fTabView->TabSide()), index, selected, first, last);
	}

	DrawLabel(context, frame);
}


//	#pragma mark - BTab private methods


BorderFlags
BTab::_Borders(BView* owner, gfx::Rect frame) const
{
	BorderFlags borders;
	if (owner == nullptr || fTabView == nullptr)
		return borders;

	// NewGUI: half-open frames; equal edges are the same pixels as Haiku's.
	const gfx::Rect bounds = owner->Bounds();
	if (fTabView->TabSide() == BTabSide::Top || fTabView->TabSide() == BTabSide::Bottom) {
		borders = Border::Top | Border::Bottom;
		if (frame.x0 == bounds.x0)
			borders |= Border::Left;
		if (frame.x1 == bounds.x1)
			borders |= Border::Right;
	} else {
		borders = Border::Left | Border::Right;
		if (frame.y0 == bounds.y0)
			borders |= Border::Top;
		if (frame.y1 == bounds.y1)
			borders |= Border::Bottom;
	}

	return borders;
}


std::unique_ptr<BView>
BTab::_TakeView()
{
	return std::move(fOwnedView);
}


//	#pragma mark - BTabView


BTabView::BTabView(std::string_view name, BButtonWidth width, BViewFlags flags)
	:
	BView(name, flags)
{
	_InitObject(true, width);
}


BTabView::BTabView(gfx::Rect frame, std::string_view name, BButtonWidth width,
	BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags)
{
	_InitObject(false, width);
}


BTabView::~BTabView()
{
	// The tabs take their views out of the container view.
	fTabList.clear();
}


void
BTabView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if (fSelection < 0 && CountTabs() > 0)
		Select(0);
}


void
BTabView::KeyDown(std::string_view bytes)
{
	if (IsHidden())
		return;

	if (bytes.empty()) {
		BView::KeyDown(bytes);
		return;
	}

	switch (bytes[0]) {
		case keys::kDownArrow:
		case keys::kLeftArrow: {
			int32 focus = fFocus - 1;
			if (focus < 0)
				focus = CountTabs() - 1;
			SetFocusTab(focus, true);
			break;
		}

		case keys::kUpArrow:
		case keys::kRightArrow: {
			int32 focus = fFocus + 1;
			if (focus >= CountTabs())
				focus = 0;
			SetFocusTab(focus, true);
			break;
		}

		case keys::kEnter:
		case keys::kSpace:
			Select(FocusTab());
			break;

		default:
			BView::KeyDown(bytes);
	}
}


void
BTabView::MouseDown(gfx::Point where)
{
	// Which button is pressed?
	int32 buttons = 0;
	const BMessage* currentMessage
		= Window() != nullptr ? Window()->CurrentMessage() : nullptr;
	if (currentMessage != nullptr)
		buttons = currentMessage->GetInt32("buttons", 0);

	int32 selection = Selection();
	int32 numTabs = CountTabs();
	if ((buttons & kBackMouseButton) != 0) {
		// The "back" mouse button moves to previous tab
		if (selection > 0 && numTabs > 1)
			Select(Selection() - 1);
	} else if ((buttons & kForwardMouseButton) != 0) {
		// The "forward" mouse button moves to next tab
		if (selection < numTabs - 1)
			Select(Selection() + 1);
	} else {
		// Other buttons are used to select a tab by clicking directly on it
		for (int32 i = 0; i < CountTabs(); i++) {
			if (TabFrame(i).Contains(where)
					&& i != Selection()) {
				Select(i);
				return;
			}
		}
	}

	BView::MouseDown(where);
}


void
BTabView::Select(int32 index)
{
	if (index == Selection())
		return;

	if (index < 0 || index >= CountTabs())
		index = Selection();

	BTab* tab = TabAt(Selection());

	if (tab)
		tab->Deselect();

	tab = TabAt(index);
	if (tab != nullptr && fContainerView != nullptr) {
		tab->Select(fContainerView);
		fSelection = index;

		// make the view visible through the layout if there is one
		BCardLayout* layout
			= dynamic_cast<BCardLayout*>(fContainerView->GetLayout());
		if (layout != nullptr)
			layout->SetVisibleItem(index);
	}

	Invalidate();

	SetFocusTab(index, true);
}


int32
BTabView::Selection() const
{
	return fSelection;
}


void
BTabView::WindowActivated(bool active)
{
	BView::WindowActivated(active);

	if (IsFocus())
		Invalidate();
}


void
BTabView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);

	SetFocusTab(Selection(), focus);
}


void
BTabView::SetFocusTab(int32 tab, bool focus)
{
	if (tab >= CountTabs())
		tab = 0;

	if (tab < 0)
		tab = CountTabs() - 1;

	if (focus) {
		if (tab == fFocus)
			return;

		if (fFocus != -1) {
			if (TabAt(fFocus) != nullptr)
				TabAt(fFocus)->MakeFocus(false);
			Invalidate();
		}
		if (TabAt(tab) != nullptr) {
			TabAt(tab)->MakeFocus(true);
			Invalidate();
			fFocus = tab;
		}
	} else if (fFocus != -1) {
		if (TabAt(fFocus) != nullptr)
			TabAt(fFocus)->MakeFocus(false);
		Invalidate();
		fFocus = -1;
	}
}


int32
BTabView::FocusTab() const
{
	return fFocus;
}


void
BTabView::Draw(DrawContext& context)
{
	DrawTabs(context);
	DrawBox(context, TabFrame(fSelection));

	if (IsFocus() && fFocus != -1)
		TabAt(fFocus)->DrawFocusMark(context, TabFrame(fFocus));
}


gfx::Rect
BTabView::DrawTabs(DrawContext& context)
{
	BRect bounds(ToBRect(Bounds()));
	BRect tabFrame(bounds);
	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);

	// set tabFrame to area around tabs
	if (fTabSide == BTabSide::Top || fTabSide == BTabSide::Bottom) {
		if (fTabSide == BTabSide::Top)
			tabFrame.bottom = fTabHeight;
		else
			tabFrame.top = tabFrame.bottom - fTabHeight;
	} else if (fTabSide == BTabSide::Left || fTabSide == BTabSide::Right) {
		if (fTabSide == BTabSide::Left)
			tabFrame.right = fTabHeight;
		else
			tabFrame.left = tabFrame.right - fTabHeight;
	}

	// draw frame behind tabs
	gfx::Rect frame = FromBRect(tabFrame);
	Look().DrawTabFrame(context, frame, base, {}, BorderFlags{}, fBorderStyle,
		SideBorder(fTabSide));

	// draw the tabs on top of the tab frame
	int32 tabCount = CountTabs();
	for (int32 i = 0; i < tabCount; i++) {
		TabAt(i)->DrawTab(context, TabFrame(i),
			i == fSelection ? BTabPosition::Front
				: (i == 0) ? BTabPosition::First : BTabPosition::Any,
			i != fSelection - 1);
	}

	return fSelection < CountTabs() ? TabFrame(fSelection) : gfx::Rect{};
}


void
BTabView::DrawBox(DrawContext& context, gfx::Rect /*selectedTabRect*/)
{
	BRect rect(ToBRect(Bounds()));
	BorderFlags bordersToDraw = kAllBorders;
	switch (fTabSide) {
		case BTabSide::Top:
			bordersToDraw = bordersToDraw & ~BorderFlags(Border::Top);
			rect.top = fTabHeight;
			break;
		case BTabSide::Bottom:
			bordersToDraw = bordersToDraw & ~BorderFlags(Border::Bottom);
			rect.bottom -= fTabHeight;
			break;
		case BTabSide::Left:
			bordersToDraw = bordersToDraw & ~BorderFlags(Border::Left);
			rect.left = fTabHeight;
			break;
		case BTabSide::Right:
			bordersToDraw = bordersToDraw & ~BorderFlags(Border::Right);
			rect.right -= fTabHeight;
			break;
	}

	const gfx::Color base = context.palette.Get(BColorWhich::PanelBackground);
	gfx::Rect frame = FromBRect(rect);
	if (fBorderStyle == BBorderStyle::Fancy)
		Look().DrawGroupFrame(context, frame, base, bordersToDraw);
	else if (fBorderStyle == BBorderStyle::Plain) {
		Look().DrawBorder(context, frame, base, BBorderStyle::Plain, {},
			bordersToDraw);
	}
	// B_NO_BORDER draws no box
}


gfx::Rect
BTabView::TabFrame(int32 index) const
{
	if (index >= CountTabs() || index < 0)
		return {};

	const TextSystem& text = TextSystem::Default();
	const float padding = std::ceil(Look().DefaultLabelSpacing(text) * 3.3f);
	const float height = fTabHeight;
	const float offset = BLayoutUtils::ComposeSpacing(kUseWindowSpacing);
	const BRect bounds(ToBRect(Bounds()));

	float width = padding * 5.0f;
	switch (fTabWidthSetting) {
		case BButtonWidth::FromLabel:
		{
			float x = 0.0f;
			for (int32 i = 0; i < index; i++)
				x += StringWidth(TabAt(i)->Label()) + padding;

			const float labelWidth = StringWidth(TabAt(index)->Label());
			switch (fTabSide) {
				case BTabSide::Top:
					return FromBRect(BRect(offset + x, 0.0f,
						offset + x + labelWidth + padding, height));
				case BTabSide::Bottom:
					return FromBRect(BRect(offset + x, bounds.bottom - height,
						offset + x + labelWidth + padding, bounds.bottom));
				case BTabSide::Left:
					return FromBRect(BRect(0.0f, offset + x, height,
						offset + x + labelWidth + padding));
				case BTabSide::Right:
					return FromBRect(BRect(bounds.right - height, offset + x,
						bounds.right, offset + x + labelWidth + padding));
			}
			return {};
		}

		case BButtonWidth::FromWidest:
			width = 0.0;
			for (int32 i = 0; i < CountTabs(); i++) {
				float tabWidth = StringWidth(TabAt(i)->Label()) + padding;
				if (tabWidth > width)
					width = tabWidth;
			}
			[[fallthrough]];

		case BButtonWidth::AsUsual:
		default:
			const float x = offset + (float)index * width;
			switch (fTabSide) {
				case BTabSide::Top:
					return FromBRect(BRect(x, 0.0f, x + width, height));
				case BTabSide::Bottom:
					return FromBRect(BRect(x, bounds.bottom - height, x + width,
						bounds.bottom));
				case BTabSide::Left:
					return FromBRect(BRect(0.0f, x, height, x + width));
				case BTabSide::Right:
					return FromBRect(BRect(bounds.right - height, x, bounds.right,
						x + width));
			}
			return {};
	}
}


// #pragma mark -


gfx::Size
BTabView::_Size(gfx::Size containerSize) const
{
	// Haiku's sizes (pixels - 1), then the pixel count.
	gfx::Size size = _TabsMinSize();
	gfx::Size container{containerSize.w - 1 + 2 * _BorderWidth(),
		containerSize.h - 1 + 2 * _BorderWidth()};
	if (container.w > size.w)
		size.w = container.w;
	size.h += container.h;

	return {std::min(size.w + 1, kSizeUnlimited), std::min(size.h + 1, kSizeUnlimited)};
}


gfx::Size
BTabView::MinSize()
{
	gfx::Size size;
	if (GetLayout())
		size = GetLayout()->MinSize();
	else
		size = _Size(fContainerView->MinSize());

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


gfx::Size
BTabView::MaxSize()
{
	gfx::Size size;
	if (GetLayout())
		size = GetLayout()->MaxSize();
	else
		size = _Size(fContainerView->MaxSize());

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


gfx::Size
BTabView::PreferredSize()
{
	gfx::Size size;
	if (GetLayout() != nullptr)
		size = GetLayout()->PreferredSize();
	else
		size = _Size(fContainerView->PreferredSize());

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


// #pragma mark -


void
BTabView::AddTab(std::unique_ptr<BView> target, std::unique_ptr<BTab> tab)
{
	if (tab == nullptr)
		tab = std::make_unique<BTab>(std::move(target));
	else
		tab->SetView(std::move(target));

	if (fContainerView->GetLayout())
		fContainerView->GetLayout()->AddView(CountTabs(), tab->_TakeView());

	BTab* added = tab.get();
	fTabList.push_back(std::move(tab));
	added->fTabView = this;

	// When we haven't had a any tabs before, but are already attached to the
	// window, select this one.
	if (CountTabs() == 1 && Window() != nullptr)
		Select(0);
}


std::unique_ptr<BTab>
BTabView::RemoveTab(int32 index)
{
	if (index < 0 || index >= CountTabs())
		return nullptr;

	std::unique_ptr<BTab> tab = std::move(fTabList[static_cast<std::size_t>(index)]);
	fTabList.erase(fTabList.begin() + index);

	tab->Deselect();
	tab->fTabView = nullptr;

	if (fContainerView->GetLayout() && tab->fView != nullptr && tab->fOwnedView == nullptr)
		tab->fOwnedView = fContainerView->GetLayout()->RemoveView(tab->fView);

	if (CountTabs() == 0) {
		fFocus = -1;
		// NewGUI: no selection either (Haiku kept it)
		fSelection = -1;
	} else if (index <= fSelection)
		Select(fSelection - 1);

	if (fFocus >= 0) {
		if (fFocus == CountTabs() - 1 || CountTabs() == 0)
			SetFocusTab(fFocus, false);
		else
			SetFocusTab(fFocus, true);
	}

	return tab;
}


BTab*
BTabView::TabAt(int32 index) const
{
	if (index < 0 || index >= CountTabs())
		return nullptr;
	return fTabList[static_cast<std::size_t>(index)].get();
}


void
BTabView::SetTabWidth(BButtonWidth width)
{
	fTabWidthSetting = width;
	Invalidate();
}


BButtonWidth
BTabView::TabWidth() const
{
	return fTabWidthSetting;
}


void
BTabView::SetTabHeight(float height)
{
	if (fTabHeight == height)
		return;

	fTabHeight = height;
	_LayoutContainerView(GetLayout() != nullptr);

	Invalidate();
}


float
BTabView::TabHeight() const
{
	return fTabHeight;
}


void
BTabView::SetBorder(BBorderStyle borderStyle)
{
	if (fBorderStyle == borderStyle)
		return;

	fBorderStyle = borderStyle;

	_LayoutContainerView(Flags().Has(BViewFlag::SupportsLayout));
	Invalidate();
}


BBorderStyle
BTabView::Border() const
{
	return fBorderStyle;
}


void
BTabView::SetTabSide(BTabSide tabSide)
{
	if (fTabSide == tabSide)
		return;

	fTabSide = tabSide;
	_LayoutContainerView(Flags().Has(BViewFlag::SupportsLayout));
	Invalidate();
}


BTabSide
BTabView::TabSide() const
{
	return fTabSide;
}


BView*
BTabView::ContainerView() const
{
	return fContainerView;
}


int32
BTabView::CountTabs() const
{
	return static_cast<int32>(fTabList.size());
}


BView*
BTabView::ViewForTab(int32 tabIndex) const
{
	BTab* tab = TabAt(tabIndex);
	if (tab != nullptr)
		return tab->View();

	return nullptr;
}


int32
BTabView::IndexOf(const BTab* tab) const
{
	if (tab != nullptr) {
		int32 tabCount = CountTabs();
		for (int32 index = 0; index < tabCount; index++) {
			if (TabAt(index) == tab)
				return index;
		}
	}

	return -1;
}


// #pragma mark - private


void
BTabView::_InitObject(bool layouted, BButtonWidth width)
{
	fTabWidthSetting = width;
	fSelection = -1;
	fFocus = -1;
	fBorderStyle = BBorderStyle::Fancy;
	fTabSide = BTabSide::Top;

	SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));

	const TextSystem& text = TextSystem::Default();
	const FontHeight fh = text.Height(FontRole::Plain);
	fTabHeight = std::ceil(fh.ascent + fh.descent + fh.leading
		+ (Look().DefaultLabelSpacing(text) * 1.3f));

	fContainerView = nullptr;
	_InitContainerView(layouted);
}


void
BTabView::_InitContainerView(bool layouted)
{
	bool needsLayout = false;
	bool createdContainer = false;
	std::unique_ptr<BView> container;
	if (layouted) {
		if (GetLayout() == nullptr) {
			SetLayout(std::make_unique<BGroupLayout>(BOrientation::Horizontal));
			needsLayout = true;
		}

		if (fContainerView == nullptr) {
			container = std::make_unique<BView>("view container", BViewFlag::WillDraw);
			container->SetLayout(std::make_unique<BCardLayout>());
			createdContainer = true;
		}
	} else if (fContainerView == nullptr) {
		container = std::make_unique<BView>(Bounds(), "view container", kFollowAll,
			BViewFlag::WillDraw);
		createdContainer = true;
	}

	if (createdContainer)
		fContainerView = container.get();

	if (needsLayout || createdContainer)
		_LayoutContainerView(layouted);

	if (createdContainer) {
		fContainerView->SetViewColor(Palette::Default().Get(BColorWhich::PanelBackground));
		AddChild(std::move(container));
	}
}


gfx::Size
BTabView::_TabsMinSize() const
{
	// Haiku's size
	gfx::Size size{0.0f, TabHeight()};
	int32 count = std::min(2, CountTabs());
	for (int32 i = 0; i < count; i++) {
		BRect frame = ToBRect(TabFrame(i));
		size.w += frame.Width();
	}

	if (count < CountTabs()) {
		// TODO: Add size for yet to be implemented buttons that allow
		// "scrolling" the displayed tabs left/right.
	}

	return size;
}


float
BTabView::_BorderWidth() const
{
	switch (fBorderStyle) {
		default:
		case BBorderStyle::Fancy:
			return 3.0f;
		case BBorderStyle::Plain:
			return 1.0f;
		case BBorderStyle::None:
			return 0.0f;
	}
}


void
BTabView::_LayoutContainerView(bool layouted)
{
	float borderWidth = _BorderWidth();
	if (layouted) {
		float topBorderOffset;
		switch (fBorderStyle) {
			default:
			case BBorderStyle::Fancy:
				topBorderOffset = 1.0f;
				break;
			case BBorderStyle::Plain:
				topBorderOffset = 0.0f;
				break;
			case BBorderStyle::None:
				topBorderOffset = -1.0f;
				break;
		}
		BGroupLayout* layout = dynamic_cast<BGroupLayout*>(GetLayout());
		if (layout != nullptr) {
			float inset = borderWidth + TabHeight() - topBorderOffset;
			switch (fTabSide) {
				case BTabSide::Top:
					layout->SetInsets(borderWidth, inset, borderWidth, borderWidth);
					break;
				case BTabSide::Bottom:
					layout->SetInsets(borderWidth, borderWidth, borderWidth, inset);
					break;
				case BTabSide::Left:
					layout->SetInsets(inset, borderWidth, borderWidth, borderWidth);
					break;
				case BTabSide::Right:
					layout->SetInsets(borderWidth, borderWidth, inset, borderWidth);
					break;
			}
		}
	} else {
		BRect bounds = ToBRect(Bounds());
		switch (fTabSide) {
			case BTabSide::Top:
				bounds.top += TabHeight();
				break;
			case BTabSide::Bottom:
				bounds.bottom -= TabHeight();
				break;
			case BTabSide::Left:
				bounds.left += TabHeight();
				break;
			case BTabSide::Right:
				bounds.right -= TabHeight();
				break;
		}
		bounds.InsetBy(borderWidth, borderWidth);

		fContainerView->MoveTo({bounds.left, bounds.top});
		// NewGUI: pixel counts
		fContainerView->ResizeTo(bounds.Width() + 1, bounds.Height() + 1);
	}
}


} // namespace ngui
