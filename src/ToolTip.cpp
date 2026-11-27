/*
 * Copyright 2009-2012, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2009, Stephan Aßmus <superstippi@gmx.de>.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */

// Ported from Haiku: src/kits/interface/ToolTip.cpp and
// src/kits/interface/ToolTipManager.cpp (commit b1d497bb69).
//
// Differences to Haiku:
// - The tooltip is a popup of the window of its view (ToolTipPopup), drawn
//   and fed in that window's thread, not a ToolTipWindow with its own
//   thread; one per window. BToolTipManager keeps the delays and passes
//   ShowTip() and HideTip() to the window.
// - The popup has no window border of the window system: ToolTipView
//   draws the one pixel border of B_BORDERED_WINDOW_LOOK itself, outside
//   the area Haiku's ResetWindowFrame() computes.
// - Shown again for the same owner with another tip (GetToolTipAt() of a
//   view with several), the new tip replaces the old one; Haiku moved the
//   old one.
// - ResetWindowFrame() cut a tip that reaches out of the screen by the
//   wrong amount: on the left and the top it grew instead (the distance
//   subtracted with the wrong sign), at the bottom it was cut by the
//   distance instead of to the screen's edge. It is cut to the screen
//   (found by the fuzz target ToolTip).
// - No archiving, no locking: a tip's content is changed from the thread of
//   the window that shows it.

#include <ngui/ToolTip.h>

#include <algorithm>
#include <atomic>

#include <ngui/GroupLayout.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Palette.h>
#include <ngui/TextView.h>
#include <ngui/View.h>
#include <ngui/Window.h>

#include "ScreenFrame.h"
#include "ToolTipPopup.h"
#include "compat/CompatView.h"


namespace ngui {


namespace {


using compat::BPoint;
using compat::BRect;

std::atomic<std::int64_t> sShowDelay{750000};
std::atomic<std::int64_t> sHideDelay{50000};


// The view of the popup (Haiku's ToolTipView): the tip's view in a group
// layout with half the item spacing around it, and the border.
class ToolTipView : public BView {
public:
	ToolTipView()
		:
		BView({0, 0, 10, 10}, "tool tip", kFollowLeftTop, BViewFlag::WillDraw)
	{
		SetViewUIColor(BColorWhich::ToolTipBackground);
		auto layout = std::make_unique<BGroupLayout>(BOrientation::Vertical);
		// NewGUI: one pixel more for the border
		const float inset = BLayoutUtils::ComposeSpacing(kUseHalfItemSpacing) + 1;
		layout->SetInsets(inset);
		(void)SetLayout(std::move(layout));
	}

	void Draw(DrawContext& context) override
	{
		// NewGUI: the one pixel border of B_BORDERED_WINDOW_LOOK, as
		// MenuFrame draws it.
		const gfx::Rect bounds = Bounds();
		compat::CompatView view(context);
		view.SetHighColor(compat::tint_color(
			compat::ToRgbColor(context.palette.Get(BColorWhich::WindowBorder)),
			compat::B_DARKEN_3_TINT));
		view.StrokeRect(BRect(bounds.x0, bounds.y0, bounds.x1 - 1, bounds.y1 - 1));
	}
};


} // namespace


// #pragma mark - BToolTip


BToolTip::BToolTip(std::unique_ptr<BView> view)
	:
	fOwnedView(std::move(view)),
	fView(fOwnedView.get())
{
	const float spacing = BLayoutUtils::ComposeSpacing(kUseBigSpacing);
	fRelativeLocation = {spacing, spacing};
}


BToolTip::~BToolTip() = default;


BView*
BToolTip::View() const
{
	return fView;
}


void
BToolTip::SetSticky(bool enable)
{
	fIsSticky = enable;
}


bool
BToolTip::IsSticky() const
{
	return fIsSticky;
}


void
BToolTip::SetMouseRelativeLocation(gfx::Point location)
{
	fRelativeLocation = location;
}


gfx::Point
BToolTip::MouseRelativeLocation() const
{
	return fRelativeLocation;
}


void
BToolTip::SetAlignment(BAlignment alignment)
{
	fAlignment = alignment;
}


BAlignment
BToolTip::Alignment() const
{
	return fAlignment;
}


void
BToolTip::AttachedToWindow()
{
}


void
BToolTip::DetachedFromWindow()
{
}


void
BToolTip::ContentChanged()
{
	if (fShownIn != nullptr)
		fShownIn->ContentChanged();
}


// #pragma mark - BTextToolTip


namespace {


std::unique_ptr<BTextView>
MakeTextView(std::string_view text)
{
	auto view = std::make_unique<BTextView>("tool tip text");
	view->SetText(text);
	view->MakeEditable(false);
	view->SetViewUIColor(BColorWhich::ToolTipBackground);
	view->SetTextUIColor(BColorWhich::ToolTipText);
	view->SetWordWrap(false);
	// NewGUI: SetWordWrap() scrolled to the caret while the view had no
	// size yet (as Haiku's does); a tooltip shows its text from the start.
	view->ScrollTo({0, 0});
	return view;
}


} // namespace


BTextToolTip::BTextToolTip(std::string_view text)
	:
	BToolTip(MakeTextView(text))
{
	fTextView = static_cast<BTextView*>(View());
}


std::string
BTextToolTip::Text() const
{
	return std::string(fTextView->Text());
}


void
BTextToolTip::SetText(std::string_view text)
{
	fTextView->SetText(text);
	fTextView->InvalidateLayout();
	ContentChanged();
}


// #pragma mark - BToolTipManager


BToolTipManager*
BToolTipManager::Manager()
{
	static BToolTipManager manager;
	return &manager;
}


void
BToolTipManager::ShowTip(std::shared_ptr<BToolTip> tip, gfx::Point where, const BView* owner)
{
	if (owner == nullptr || owner->Window() == nullptr)
		return;
	BWindow* window = owner->Window();
	if (window->fToolTip == nullptr)
		window->fToolTip = std::make_unique<ToolTipPopup>(window);
	window->fToolTip->Show(tip, where, owner);
}


void
BToolTipManager::HideTip()
{
	BWindow* window = BWindow::_DispatchingWindow();
	if (window != nullptr && window->fToolTip != nullptr)
		window->fToolTip->Hide();
}


void
BToolTipManager::SetShowDelay(Clock::Time delay)
{
	// between 10ms and 3s
	sShowDelay = std::clamp<std::int64_t>(delay.count(), 10000, 3000000);
}


Clock::Time
BToolTipManager::ShowDelay() const
{
	return Clock::Time(sShowDelay.load());
}


void
BToolTipManager::SetHideDelay(Clock::Time delay)
{
	// between 0 and 0.5s
	sHideDelay = std::clamp<std::int64_t>(delay.count(), 0, 500000);
}


Clock::Time
BToolTipManager::HideDelay() const
{
	return Clock::Time(sHideDelay.load());
}


// #pragma mark - ToolTipPopup


ToolTipPopup::ToolTipPopup(BWindow* window)
	:
	fWindow(window)
{
}


ToolTipPopup::~ToolTipPopup()
{
	Close();
}


void
ToolTipPopup::Show(const std::shared_ptr<BToolTip>& tip, gfx::Point where, const BView* owner)
{
	if (tip == nullptr)
		return;
	if (fView != nullptr && fTip == tip) {
		// Haiku's kMsgShowToolTip to the window that shows it
		fOwner = owner;
		_ResetFrame(where);
		fHidden = false;
		fCloser.reset();
		return;
	}

	Close();
	// shown in another window
	if (tip->fShownIn != nullptr || tip->fOwnedView == nullptr)
		return;

	auto root = std::make_unique<ToolTipView>();
	(void)root->AddChild(std::move(tip->fOwnedView));
	fTip = tip;
	fOwner = owner;
	fHidden = false;
	fView = root.get();
	tip->fShownIn = this;
	// figure out size and location
	_ResetFrame(where);
	fTip->AttachedToWindow();
	(void)fWindow->_OpenPopup(std::move(root), true);
}


void
ToolTipPopup::Hide()
{
	// Haiku's ToolTipView::HideTip()
	if (fView == nullptr || fHidden)
		return;

	fHidden = true;
	const Clock::Time delay = BToolTipManager::Manager()->HideDelay();
	if (delay > Clock::Time::zero()) {
		fCloser = std::make_unique<BMessageRunner>(BMessenger(fWindow),
			BMessage(kToolTipClose), delay, 1);
	} else {
		(void)BMessenger(fWindow).SendMessage(kToolTipClose);
	}
}


void
ToolTipPopup::Close()
{
	if (fView == nullptr)
		return;

	std::unique_ptr<BView> root = fWindow->ClosePopup(fView);
	fView = nullptr;
	if (root != nullptr) {
		// don't delete this one: the tip keeps its view
		fTip->fOwnedView = root->RemoveChild(fTip->fView);
		fWindow->_DisposeLater(std::move(root));
	}
	fTip->fShownIn = nullptr;
	fTip->DetachedFromWindow();
	fTip.reset();
	fOwner = nullptr;
	fHidden = false;
	fCloser.reset();
}


void
ToolTipPopup::CloseIfHidden()
{
	if (fHidden)
		Close();
}


void
ToolTipPopup::MouseMoved(gfx::Point where)
{
	if (fView == nullptr)
		return;
	if (fTip->IsSticky())
		_ResetFrame(where);
	else {
		// close with the preferred delay in case the mouse just moved
		Hide();
	}
}


void
ToolTipPopup::MouseMovedOver(gfx::Point where)
{
	if (fView == nullptr)
		return;
	if (fTip->IsSticky())
		_ResetFrame(where);
	else {
		// close instantly if the user managed to enter
		Close();
	}
}


void
ToolTipPopup::KeyDown()
{
	if (fView != nullptr && !fTip->IsSticky())
		Hide();
}


void
ToolTipPopup::ViewDetached(const BView* view)
{
	if (fView != nullptr && view == fOwner)
		Close();
}


void
ToolTipPopup::ContentChanged()
{
	if (fView == nullptr)
		return;
	fView->InvalidateLayout(true);
	_ResetFrame(fWhere);
}


BView*
ToolTipPopup::Popup() const
{
	return fView;
}


const BView*
ToolTipPopup::Owner() const
{
	return fOwner;
}


bool
ToolTipPopup::IsHidden() const
{
	return fHidden;
}


/*!	Tries to find the right frame to show the tool tip in, trying to use the
	alignment that the tool tip specifies.
	Makes sure the tool tip can be shown on screen in its entirety, ie. it will
	resize the window if necessary.
*/
void
ToolTipPopup::_ResetFrame(gfx::Point screenWhere)
{
	fWhere = screenWhere;
	if (fView == nullptr)
		return;

	// NewGUI: Haiku's sizes are one less than the pixel counts of
	// PreferredSize(); the border is outside them.
	const gfx::Size preferred = fView->PreferredSize();
	float width = preferred.w - 2 - 1;
	float height = preferred.h - 2 - 1;
	BPoint where(screenWhere.x, screenWhere.y);
	BRect screenFrame = ScreenFrameAt(screenWhere).InsetBySelf(2, 2);
	const gfx::Point relative = fTip->MouseRelativeLocation();
	const BPoint offset(relative.x, relative.y);

	// Ensure that the tip can be placed on screen completely
	if (width > screenFrame.Width())
		width = screenFrame.Width();

	if (width > where.x - screenFrame.left
		&& width > screenFrame.right - where.x) {
		// There is no space to put the tip to the left or the right of the
		// cursor, it can either be below or above it
		if (height > where.y - screenFrame.top
			&& where.y - screenFrame.top > screenFrame.Height() / 2) {
			height = where.y - offset.y - screenFrame.top;
		} else if (height > screenFrame.bottom - where.y
			&& screenFrame.bottom - where.y > screenFrame.Height() / 2) {
			height = screenFrame.bottom - where.y - offset.y;
		}
	}

	// Find best alignment, starting with the requested one
	BAlignment alignment = fTip->Alignment();
	BPoint location = where;
	bool doesNotFit = false;
	switch (alignment.horizontal) {
		case BHorizontalAlignment::Left:
			location.x -= width + offset.x;
			if (location.x < screenFrame.left) {
				location.x = screenFrame.left;
				doesNotFit = true;
			}
			break;
		case BHorizontalAlignment::Center:
			location.x -= width / 2 - offset.x;
			if (location.x < screenFrame.left) {
				location.x = screenFrame.left;
				doesNotFit = true;
			} else if (location.x + width > screenFrame.right) {
				location.x = screenFrame.right - width;
				doesNotFit = true;
			}
			break;

		default:
			location.x += offset.x;
			if (location.x + width > screenFrame.right) {
				location.x = screenFrame.right - width;
				doesNotFit = true;
			}
			break;
	}

	if ((doesNotFit && alignment.vertical == BVerticalAlignment::Middle)
		|| (alignment.vertical == BVerticalAlignment::Middle
			&& alignment.horizontal == BHorizontalAlignment::Center))
		alignment.vertical = BVerticalAlignment::Bottom;

	// Adjust the tooltip position in cases where it would be partly out of the
	// screen frame. Try to fit the tooltip on the requested side of the
	// cursor, if that fails, try the opposite side, and if that fails again,
	// give up and leave the tooltip under the mouse cursor.
	bool firstTry = true;
	while (true) {
		switch (alignment.vertical) {
			case BVerticalAlignment::Top:
				location.y = where.y - height - offset.y;
				if (location.y < screenFrame.top) {
					alignment.vertical = firstTry ? BVerticalAlignment::Bottom
						: BVerticalAlignment::Middle;
					firstTry = false;
					continue;
				}
				break;

			case BVerticalAlignment::Middle:
				location.y -= height / 2 - offset.y;
				if (location.y < screenFrame.top)
					location.y = screenFrame.top;
				else if (location.y + height > screenFrame.bottom)
					location.y = screenFrame.bottom - height;
				break;

			default:
				location.y = where.y + offset.y;
				if (location.y + height > screenFrame.bottom) {
					alignment.vertical = firstTry ? BVerticalAlignment::Top
						: BVerticalAlignment::Middle;
					firstTry = false;
					continue;
				}
				break;
		}
		break;
	}

	where = location;

	// Cut off any out-of-screen areas
	// NewGUI: by the part outside; Haiku subtracted the negative distance on
	// the left and the top (the tip grew) and subtracted the distance at the
	// bottom.
	if (screenFrame.left > where.x) {
		width -= screenFrame.left - where.x;
		where.x = screenFrame.left;
	} else if (screenFrame.right < where.x + width)
		width = screenFrame.right - where.x;

	if (screenFrame.top > where.y) {
		height -= screenFrame.top - where.y;
		where.y = screenFrame.top;
	} else if (screenFrame.bottom < where.y + height)
		height = screenFrame.bottom - where.y;

	// Change window frame: NewGUI: the popup with its border around it
	fView->MoveTo({where.x - 1, where.y - 1});
	fView->ResizeTo(width + 1 + 2, height + 1 + 2);
}


} // namespace ngui
