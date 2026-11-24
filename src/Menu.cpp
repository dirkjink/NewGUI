/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Menu.cpp (commit b1d497bb69): the
 * items, their layout and drawing (W3a). Tracking, the menu window,
 * keyboard navigation, dynamic items and scripting come with BMenuBar
 * (W3b). NewGUI (see Menu.h):
 * - The menu owns its items; RemoveItem() gives them back.
 * - Item frames are Haiku's inclusive rectangles and the cached preferred
 *   size is Haiku's (pixels - 1); the public sizes are pixel counts.
 * - Draw() lays the items out first if needed and draws them (Haiku
 *   invalidated and drew in the next update).
 * - A submenu is shown in a popup of its menu bar's window (W3b: popups
 *   belong to the window, MenuFrame instead of BMenuWindow); while shown,
 *   the popup's frame owns it, afterwards its superitem again. No
 *   scrolling menus yet.
 * - The menu font is the plain font of TextSystem::Default(); triggers
 *   are hidden by default, as with Haiku's default menu_info.
 * - Automatic triggers: Unicode case folding and spaces only for ASCII
 *   and the common Unicode spaces (Haiku: BUnicodeChar).
 *
 * Authors (Haiku):
 *		Stephan Aßmus, superstippi@gmx.de
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Adrien Destugues, pulkomandy@pulkomandy.tk
 *		Marc Flerackers, mflerackers@androme.be
 *		Rene Gollent, anevilyak@gmail.com
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/Menu.h>

#include <algorithm>
#include <cmath>
#include <set>

#include <ngui/ControlLook.h>
#include <ngui/LayoutUtils.h>
#include <ngui/MenuItem.h>
#include <ngui/Palette.h>
#include <ngui/SeparatorItem.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>
#include <ngui/platform/Platform.h>

#include "MenuFieldBar.h"
#include "MenuPrivate.h"
#include "MenuSymbols.h"
#include "compat/CompatView.h"


namespace ngui {


namespace {


class TriggerList {
public:
	bool HasTrigger(uint32 c)
		{ return fList.find(ToLower(c)) != fList.end(); }
	bool AddTrigger(uint32 c)
	{
		fList.insert(ToLower(c));
		return true;
	}

	static uint32 ToLower(uint32 c)
	{
		// NewGUI: ASCII only (Haiku: BUnicodeChar::ToLower())
		if (c >= 'A' && c <= 'Z')
			return c - 'A' + 'a';
		return c;
	}

private:
	std::set<uint32> fList;
};


// The next code point of the UTF-8 \a text at \a position, which moves on;
// 0 at the end.
uint32
NextCodePoint(std::string_view text, std::size_t& position)
{
	if (position >= text.size())
		return 0;
	const auto lead = static_cast<unsigned char>(text[position]);
	std::size_t length = 1;
	uint32 c = lead;
	if ((lead & 0xe0) == 0xc0) {
		length = 2;
		c = lead & 0x1f;
	} else if ((lead & 0xf0) == 0xe0) {
		length = 3;
		c = lead & 0x0f;
	} else if ((lead & 0xf8) == 0xf0) {
		length = 4;
		c = lead & 0x07;
	}
	if (position + length > text.size()) {
		position = text.size();
		return 0;
	}
	for (std::size_t i = 1; i < length; i++)
		c = (c << 6) | (static_cast<unsigned char>(text[position + i]) & 0x3f);
	position += length;
	return c;
}


bool
IsSpace(uint32 c)
{
	return c == ' ' || (c >= '\t' && c <= '\r') || c == 0x00a0
		|| (c >= 0x2000 && c <= 0x200a) || c == 0x202f || c == 0x205f || c == 0x3000;
}


bool
IsAlNum(uint32 c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}


bool
ChooseTrigger(std::string_view title, int32& index, uint32& trigger, TriggerList& triggers)
{
	if (title.empty())
		return false;

	index = 0;
	uint32 c;

	// two runs: first we look out for alphanumeric ASCII characters
	std::size_t nextCharacter = 0;
	std::size_t character = nextCharacter;
	while ((c = NextCodePoint(title, nextCharacter)) != 0) {
		if (!(c < 128 && IsAlNum(c)) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = TriggerList::ToLower(c);
		index = static_cast<int32>(character);
		return triggers.AddTrigger(c);
	}

	// then, if we still haven't found something, we accept anything
	nextCharacter = 0;
	character = nextCharacter;
	while ((c = NextCodePoint(title, nextCharacter)) != 0) {
		if (IsSpace(c) || triggers.HasTrigger(c)) {
			character = nextCharacter;
			continue;
		}
		trigger = TriggerList::ToLower(c);
		index = static_cast<int32>(character);
		return triggers.AddTrigger(c);
	}

	return false;
}


} // namespace


// #pragma mark - BMenu


BMenu::BMenu(std::string_view name, BMenuLayout layout)
	:
	BView({0, 0, 0, 0}, name, kFollowLeftTop, BViewFlag::WillDraw),
	fLayout(layout)
{
	_InitData();
}


BMenu::BMenu(std::string_view name, float /*width*/, float /*height*/)
	:
	BView({0, 0, 0, 0}, name, kFollowLeftTop, BViewFlag::WillDraw),
	fLayout(BMenuLayout::InMatrix)
{
	// NewGUI: as in Haiku, the size comes from the items' frames.
	_InitData();
}


BMenu::BMenu(gfx::Rect frame, std::string_view name, BResizingMode resizingMode,
	BViewFlags flags, BMenuLayout layout, bool resizeToFit)
	:
	BView(frame, name, resizingMode, flags),
	fLayout(layout),
	fResizeToFit(resizeToFit)
{
	_InitData();
}


BMenu::~BMenu()
{
	// The items refer to the menu while they are destroyed.
	while (!fItems.empty()) {
		std::unique_ptr<BMenuItem> item = std::move(fItems.back());
		fItems.pop_back();
		item->SetSuper(nullptr);
	}
}


void
BMenu::AttachedToWindow()
{
	BView::AttachedToWindow();

	// NewGUI: a stand-alone pop-up menu, shown in a popup of the window,
	// does not install its shortcuts there (Haiku: in its own menu window).
	if (Superitem() == nullptr && !fStandalone)
		_Install(Window());

	fUseCachedMenuLayout = false;

	_RelayoutIfNeeded();
}


void
BMenu::DetachedFromWindow()
{
	BView::DetachedFromWindow();

	if (Superitem() == nullptr && !fStandalone)
		_Uninstall();
}


void
BMenu::Draw(DrawContext& context)
{
	// NewGUI: lay out and draw in the same pass.
	(void)_RelayoutIfNeeded();

	DrawBackground(context);
	DrawItems(context);
}


gfx::Size
BMenu::MinSize()
{
	_ValidatePreferredSize();

	gfx::Size size = (GetLayout() != nullptr ? GetLayout()->MinSize()
		: gfx::Size{fPreferred.w + 1, fPreferred.h + 1});

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


gfx::Size
BMenu::MaxSize()
{
	_ValidatePreferredSize();

	gfx::Size size = (GetLayout() != nullptr ? GetLayout()->MaxSize()
		: gfx::Size{fPreferred.w + 1, fPreferred.h + 1});

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


gfx::Size
BMenu::PreferredSize()
{
	_ValidatePreferredSize();

	gfx::Size size = (GetLayout() != nullptr ? GetLayout()->PreferredSize()
		: gfx::Size{fPreferred.w + 1, fPreferred.h + 1});

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


void
BMenu::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	// NewGUI: pixel counts
	if (_width)
		*_width = fPreferred.w + 1;

	if (_height)
		*_height = fPreferred.h + 1;
}


void
BMenu::DoLayout()
{
	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != nullptr) {
		BView::DoLayout();
		return;
	}

	if (_RelayoutIfNeeded())
		Invalidate();
}


bool
BMenu::AddItem(std::unique_ptr<BMenuItem> item)
{
	return AddItem(std::move(item), CountItems());
}


bool
BMenu::AddItem(std::unique_ptr<BMenuItem> item, int32 index)
{
	// NewGUI: refused (Haiku: debugger())
	if (fLayout == BMenuLayout::InMatrix)
		return false;

	if (item == nullptr)
		return false;

	if (!_AddItem(std::move(item), index))
		return false;

	InvalidateLayout();
	if (Window() != nullptr && !Window()->IsHidden()) {
		_LayoutItems(index);
		Invalidate();
	}

	return true;
}


bool
BMenu::AddItem(std::unique_ptr<BMenuItem> item, gfx::Rect frame)
{
	if (fLayout != BMenuLayout::InMatrix)
		return false;

	if (item == nullptr)
		return false;

	// Haiku's inclusive rectangle
	item->fBounds = {frame.x0, frame.y0, frame.x1 - 1, frame.y1 - 1};

	int32 index = CountItems();
	if (!_AddItem(std::move(item), index))
		return false;

	if (Window() != nullptr && !Window()->IsHidden()) {
		_LayoutItems(index);
		Invalidate();
	}

	return true;
}


bool
BMenu::AddItem(std::unique_ptr<BMenu> submenu)
{
	return AddItem(std::make_unique<BMenuItem>(std::move(submenu)), CountItems());
}


bool
BMenu::AddItem(std::unique_ptr<BMenu> submenu, int32 index)
{
	return AddItem(std::make_unique<BMenuItem>(std::move(submenu)), index);
}


bool
BMenu::AddItem(std::unique_ptr<BMenu> submenu, gfx::Rect frame)
{
	return AddItem(std::make_unique<BMenuItem>(std::move(submenu)), frame);
}


bool
BMenu::AddSeparatorItem()
{
	return AddItem(std::make_unique<BSeparatorItem>(), CountItems());
}


std::unique_ptr<BMenuItem>
BMenu::RemoveItem(BMenuItem* item)
{
	return _RemoveItemAt(IndexOf(item));
}


std::unique_ptr<BMenuItem>
BMenu::RemoveItem(int32 index)
{
	return _RemoveItemAt(index);
}


bool
BMenu::RemoveItems(int32 index, int32 count)
{
	bool success = false;

	// We iterate backwards because it's simpler
	int32 i = std::min(index + count - 1, CountItems() - 1);
	// NOTE: the range check for "index" is done after
	// calculating the last index to be removed, so
	// that the range is not "shifted" unintentionally
	index = std::max((int32)0, index);
	for (; i >= index; i--) {
		if (_RemoveItemAt(i) != nullptr)
			success = true;
	}

	return success;
}


std::unique_ptr<BMenuItem>
BMenu::RemoveItem(BMenu* submenu)
{
	return _RemoveItemAt(IndexOf(submenu));
}


int32
BMenu::CountItems() const
{
	return static_cast<int32>(fItems.size());
}


BMenuItem*
BMenu::ItemAt(int32 index) const
{
	if (index < 0 || index >= CountItems())
		return nullptr;
	return fItems[static_cast<std::size_t>(index)].get();
}


BMenu*
BMenu::SubmenuAt(int32 index) const
{
	BMenuItem* item = ItemAt(index);
	return item != nullptr ? item->Submenu() : nullptr;
}


int32
BMenu::IndexOf(const BMenuItem* item) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		if (ItemAt(i) == item)
			return i;
	}
	return -1;
}


int32
BMenu::IndexOf(const BMenu* submenu) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		if (ItemAt(i)->Submenu() == submenu)
			return i;
	}

	return -1;
}


BMenuItem*
BMenu::FindItem(std::string_view label) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Label() == label)
			return item;

		if (item->Submenu() != nullptr) {
			item = item->Submenu()->FindItem(label);
			if (item != nullptr)
				return item;
		}
	}

	return nullptr;
}


BMenuItem*
BMenu::FindItem(uint32 command) const
{
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Message() != nullptr && item->Command() == command)
			return item;

		if (item->Submenu() != nullptr) {
			item = item->Submenu()->FindItem(command);
			if (item != nullptr)
				return item;
		}
	}

	return nullptr;
}


void
BMenu::SetTargetForItems(const BHandler* handler)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->SetTarget(handler);
}


void
BMenu::SetTargetForItems(BMessenger messenger)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->SetTarget(messenger);
}


void
BMenu::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (dynamic_cast<MenuFieldBar*>(Supermenu()) != nullptr)
		Supermenu()->SetEnabled(enable);

	if (fSuperitem)
		fSuperitem->SetEnabled(enable);

	Invalidate();
}


void
BMenu::SetRadioMode(bool on)
{
	fRadioMode = on;
	if (!on)
		SetLabelFromMarked(false);
}


void
BMenu::SetTriggersEnabled(bool enable)
{
	fTriggerEnabled = enable;
	Invalidate();
}


void
BMenu::SetMaxContentWidth(float width)
{
	fMaxContentWidth = width;
}


void
BMenu::SetLabelFromMarked(bool on)
{
	fDynamicName = on;
	if (on)
		SetRadioMode(true);
}


bool
BMenu::IsLabelFromMarked() const
{
	return fDynamicName;
}


bool
BMenu::IsEnabled() const
{
	if (!fEnabled)
		return false;

	return fSuper ? fSuper->IsEnabled() : true ;
}


bool
BMenu::IsRadioMode() const
{
	return fRadioMode;
}


bool
BMenu::AreTriggersEnabled() const
{
	return fTriggerEnabled;
}


float
BMenu::MaxContentWidth() const
{
	return fMaxContentWidth;
}


BMenuItem*
BMenu::FindMarked() const
{
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return item;
	}

	return nullptr;
}


int32
BMenu::FindMarkedIndex() const
{
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		if (item->IsMarked())
			return i;
	}

	return -1;
}


BMenu*
BMenu::Supermenu() const
{
	return fSuper;
}


BMenuItem*
BMenu::Superitem() const
{
	return fSuperitem;
}


void
BMenu::SetItemMargins(float left, float top, float right, float bottom)
{
	fPad = {left, top, right, bottom};
}


void
BMenu::GetItemMargins(float* _left, float* _top, float* _right, float* _bottom) const
{
	if (_left != nullptr)
		*_left = fPad.x0;
	if (_top != nullptr)
		*_top = fPad.y0;
	if (_right != nullptr)
		*_right = fPad.x1;
	if (_bottom != nullptr)
		*_bottom = fPad.y1;
}


BMenuLayout
BMenu::Layout() const
{
	return fLayout;
}


void
BMenu::DrawBackground(DrawContext& context)
{
	const gfx::Color base = context.palette.Get(BColorWhich::MenuBackground);
	ControlStateFlags flags;
	if (!IsEnabled())
		flags |= ControlState::Disabled;

	if (IsFocus())
		flags |= ControlState::Focused;

	gfx::Rect rect = Bounds();
	BorderFlags borders = Border::Left | Border::Right;
	if (dynamic_cast<MenuFrame*>(Parent()) != nullptr) {
		// NewGUI: in its popup, which it fills (Haiku: in its menu window)
		borders |= Border::Top | Border::Bottom;
	} else if (Window() != nullptr && Parent() != nullptr) {
		if (Parent()->Frame().y0 == Window()->Bounds().y0)
			borders |= Border::Top;

		if (Parent()->Frame().y1 == Window()->Bounds().y1)
			borders |= Border::Bottom;
	} else {
		borders |= Border::Top | Border::Bottom;
	}

	Look().DrawMenuBackground(context, rect, base, flags, borders);
}


void
BMenu::DrawItems(DrawContext& context)
{
	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++)
		ItemAt(i)->Draw(context);
}


void
BMenu::SortItems(int (*compare)(const BMenuItem*, const BMenuItem*))
{
	std::stable_sort(fItems.begin(), fItems.end(),
		[compare](const std::unique_ptr<BMenuItem>& a, const std::unique_ptr<BMenuItem>& b) {
			return compare(a.get(), b.get()) < 0;
		});

	InvalidateLayout();
	if (Window() != nullptr && !Window()->IsHidden()) {
		_LayoutItems(0);
		Invalidate();
	}
}


bool
BMenu::SwapItems(int32 indexA, int32 indexB)
{
	if (ItemAt(indexA) == nullptr || ItemAt(indexB) == nullptr)
		return false;

	std::swap(fItems[static_cast<std::size_t>(indexA)],
		fItems[static_cast<std::size_t>(indexB)]);

	InvalidateLayout();
	if (Window() != nullptr && !Window()->IsHidden()) {
		_LayoutItems(std::min(indexA, indexB));
		Invalidate();
	}

	return true;
}


bool
BMenu::MoveItem(int32 indexFrom, int32 indexTo)
{
	if (ItemAt(indexFrom) == nullptr || ItemAt(indexTo) == nullptr)
		return false;

	std::unique_ptr<BMenuItem> item = std::move(fItems[static_cast<std::size_t>(indexFrom)]);
	fItems.erase(fItems.begin() + indexFrom);
	fItems.insert(fItems.begin() + indexTo, std::move(item));

	InvalidateLayout();
	if (Window() != nullptr && !Window()->IsHidden()) {
		_LayoutItems(std::min(indexFrom, indexTo));
		Invalidate();
	}

	return true;
}


void
BMenu::LayoutInvalidated(bool /*descendants*/)
{
	fUseCachedMenuLayout = false;
	fPreferred = {kSizeUnset, kSizeUnset};
}


// #pragma mark - private


void
BMenu::_InitData()
{
	const float labelSpacing = Look().DefaultLabelSpacing(TextSystem::Default());
	fPad = {std::ceil(labelSpacing * 2.3f), std::ceil(labelSpacing / 3.0f),
		std::ceil((labelSpacing / 3.0f) * 10.0f), 0.0f};

	fLastResizingMode = ResizingMode();

	// Haiku: the low color is the menu background, the view color
	// transparent.
	SetViewColor(std::nullopt);
}


bool
BMenu::_AddItem(std::unique_ptr<BMenuItem> item, int32 index)
{
	if (index < 0 || index > CountItems())
		return false;

	BMenuItem* added = item.get();
	fItems.insert(fItems.begin() + index, std::move(item));

	// NewGUI: marked once it is in the menu (Haiku: before)
	if (added->IsMarked())
		_ItemMarked(added);

	// install the item on the supermenu's window
	// or onto our window, if we are a root menu
	BWindow* window = nullptr;
	if (Superitem() != nullptr)
		window = Superitem()->fWindow;
	else
		window = Window();

	if (window != nullptr)
		added->Install(window);

	added->SetSuper(this);
	return true;
}


std::unique_ptr<BMenuItem>
BMenu::_RemoveItemAt(int32 index)
{
	if (index < 0 || index >= CountItems())
		return nullptr;

	std::unique_ptr<BMenuItem> item = std::move(fItems[static_cast<std::size_t>(index)]);
	fItems.erase(fItems.begin() + index);

	if (item.get() == fSelected)
		fSelected = nullptr;

	item->Uninstall();
	item->SetSuper(nullptr);

	InvalidateLayout();
	if (Window() != nullptr) {
		_LayoutItems(0);
		Invalidate();
	}

	return item;
}


bool
BMenu::_RelayoutIfNeeded()
{
	if (!fUseCachedMenuLayout) {
		fUseCachedMenuLayout = true;
		_CacheFontInfo();
		_LayoutItems(0);
		return true;
	}
	return false;
}


void
BMenu::_LayoutItems(int32 index)
{
	_CalcTriggers();

	float width;
	float height;
	_ComputeLayout(index, fResizeToFit, true, &width, &height);

	// NewGUI: pixel counts
	if (fResizeToFit)
		ResizeTo(width + 1, height + 1);
}


gfx::Size
BMenu::_ValidatePreferredSize()
{
	if (fPreferred.w == kSizeUnset || ResizingMode() != fLastResizingMode) {
		_ComputeLayout(0, true, false, nullptr, nullptr);
		ResetLayoutInvalidation();
	}

	return fPreferred;
}


void
BMenu::_ComputeLayout(int32 index, bool bestFit, bool moveItems, float* _width,
	float* _height)
{
	// TODO: Take "bestFit", "moveItems", "index" into account,
	// Recalculate only the needed items,
	// not the whole layout every time

	fLastResizingMode = ResizingMode();

	// Haiku's inclusive rectangle
	gfx::Rect frame{0, 0, 0, 0};
	switch (fLayout) {
		case BMenuLayout::InColumn:
		{
			// NewGUI: the bar's inclusive bounds; no looper to lock
			gfx::Rect parentFrame;
			const gfx::Rect* overrideFrame = nullptr;
			if (dynamic_cast<MenuFieldBar*>(Supermenu()) != nullptr) {
				const gfx::Rect bounds = Supermenu()->Bounds();
				parentFrame = {0, 0, bounds.Width() - 1, bounds.Height() - 1};
				overrideFrame = &parentFrame;
			}

			_ComputeColumnLayout(index, bestFit, moveItems, overrideFrame, frame);
			break;
		}

		case BMenuLayout::InRow:
			_ComputeRowLayout(index, bestFit, moveItems, frame);
			break;

		case BMenuLayout::InMatrix:
			_ComputeMatrixLayout(frame);
			break;
	}

	// change width depending on resize mode
	// NewGUI: Haiku's widths (pixels - 1)
	gfx::Size size;
	if (ResizingMode().horizontal == BHorizontalFollow::LeftRight) {
		if (dynamic_cast<MenuFieldBar*>(this) != nullptr)
			size.w = Bounds().Width() - 1 - fPad.x1;
		else if (Parent() != nullptr)
			size.w = Parent()->Frame().Width() - 1;
		else if (Window() != nullptr)
			size.w = Window()->Frame().Width() - 1;
		else
			size.w = Bounds().Width() - 1;
	} else
		size.w = frame.x1 - frame.x0;

	size.h = frame.y1 - frame.y0;

	if (_width)
		*_width = size.w;

	if (_height)
		*_height = size.h;

	if (bestFit)
		fPreferred = size;

	if (moveItems)
		fUseCachedMenuLayout = true;
}


void
BMenu::_ComputeColumnLayout(int32 index, bool /*bestFit*/, bool moveItems,
	const gfx::Rect* overrideFrame, gfx::Rect& frame)
{
	bool command = false;
	bool control = false;
	bool shift = false;
	bool option = false;
	bool submenu = false;

	if (index > 0)
		frame = ItemAt(index - 1)->fBounds;
	else if (overrideFrame != nullptr)
		frame = {0, 0, overrideFrame->x1, -1};
	else
		frame = {0, 0, 0, -1};

	const float fontSize = TextSystem::Default().FontSize(FontRole::Plain);

	// Loop over all items to set their top, bottom and left coordinates,
	// all while computing the width of the menu
	for (; index < CountItems(); index++) {
		BMenuItem* item = ItemAt(index);

		float width;
		float height;
		item->GetContentSize(&width, &height);

		if (item->fModifiers.Raw() != 0 && item->fShortcut != 0) {
			width += fontSize;
			if (item->fModifiers.Has(BModifier::Command))
				command = true;

			if (item->fModifiers.Has(BModifier::Control))
				control = true;

			if (item->fModifiers.Has(BModifier::Shift))
				shift = true;

			if (item->fModifiers.Has(BModifier::Option))
				option = true;
		}

		item->fBounds.x0 = 0.0f;
		item->fBounds.y0 = frame.y1 + 1.0f;
		item->fBounds.y1 = item->fBounds.y0 + height + fPad.y0 + fPad.y1;

		if (item->fSubmenu != nullptr)
			submenu = true;

		frame.x1 = std::max(frame.x1, width + fPad.x0 + fPad.x1);
		frame.y1 = item->fBounds.y1;
	}

	// Compute the extra space needed for shortcuts and submenus
	// NewGUI: the symbols of Haiku's default keymap (Command is left Alt)
	if (command)
		frame.x1 += MenuSymbolWidth(MenuSymbol::Alt) + 1;

	if (control)
		frame.x1 += MenuSymbolWidth(MenuSymbol::Control) + 1;

	if (option)
		frame.x1 += MenuSymbolWidth(MenuSymbol::Option) + 1;

	if (shift)
		frame.x1 += MenuSymbolWidth(MenuSymbol::Shift) + 1;

	if (submenu) {
		const gfx::Rect& first = ItemAt(0)->fBounds;
		frame.x1 += (first.y1 - first.y0) / 2;
		fHasSubmenus = true;
	} else {
		fHasSubmenus = false;
	}

	if (fMaxContentWidth > 0)
		frame.x1 = std::min(frame.x1, fMaxContentWidth);

	frame.y0 = 0;
	frame.x1 = std::ceil(frame.x1);

	// Finally update the "right" coordinate of all items
	if (moveItems) {
		for (int32 i = 0; i < CountItems(); i++)
			ItemAt(i)->fBounds.x1 = frame.x1;
	}
}


void
BMenu::_ComputeRowLayout(int32 /*index*/, bool bestFit, bool moveItems, gfx::Rect& frame)
{
	const FontHeight fh = TextSystem::Default().Height(FontRole::Plain);
	frame = {0.0f, 0.0f, 0.0f, std::ceil(fh.ascent + fh.descent + fPad.y0 + fPad.y1)};

	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);

		float width, height;
		item->GetContentSize(&width, &height);

		item->fBounds.x0 = frame.x1;
		item->fBounds.y0 = 0.0f;
		item->fBounds.x1 = item->fBounds.x0 + width + fPad.x0 + fPad.x1;

		frame.x1 = item->fBounds.x1 + 1.0f;
		frame.y1 = std::max(frame.y1, height + fPad.y0 + fPad.y1);
	}

	if (moveItems) {
		for (int32 i = 0; i < CountItems(); i++)
			ItemAt(i)->fBounds.y1 = frame.y1;
	}

	if (bestFit)
		frame.x1 = std::ceil(frame.x1);
	else
		frame.x1 = Bounds().Width() - 1;
}


void
BMenu::_ComputeMatrixLayout(gfx::Rect& frame)
{
	frame = {0, 0, 0, 0};
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item != nullptr) {
			frame.x0 = std::min(frame.x0, item->fBounds.x0);
			frame.x1 = std::max(frame.x1, item->fBounds.x1);
			frame.y0 = std::min(frame.y0, item->fBounds.y0);
			frame.y1 = std::max(frame.y1, item->fBounds.y1);
		}
	}
}


void
BMenu::_CacheFontInfo()
{
	const FontHeight fh = TextSystem::Default().Height(FontRole::Plain);
	fAscent = fh.ascent;
	fDescent = fh.descent;
	fFontHeight = std::ceil(fh.ascent + fh.descent + fh.leading);
}


void
BMenu::_ItemMarked(BMenuItem* item)
{
	if (IsRadioMode()) {
		for (int32 i = 0; i < CountItems(); i++) {
			if (ItemAt(i) != item)
				ItemAt(i)->SetMarked(false);
		}
	}

	if (IsLabelFromMarked() && Superitem() != nullptr)
		Superitem()->SetLabel(item->Label());
}


void
BMenu::_Install(BWindow* target)
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Install(target);
}


void
BMenu::_Uninstall()
{
	for (int32 i = 0; i < CountItems(); i++)
		ItemAt(i)->Uninstall();
}


void
BMenu::_CalcTriggers()
{
	TriggerList triggerList;

	// Gathers the existing triggers set by the user
	for (int32 i = 0; i < CountItems(); i++) {
		char trigger = ItemAt(i)->Trigger();
		if (trigger != 0)
			triggerList.AddTrigger(static_cast<unsigned char>(trigger));
	}

	// Set triggers for items which don't have one yet
	for (int32 i = 0; i < CountItems(); i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Trigger() == 0) {
			uint32 trigger;
			int32 index;
			if (ChooseTrigger(item->Label(), index, trigger, triggerList))
				item->SetAutomaticTrigger(index, trigger);
		}
	}
}


// #pragma mark - showing (W3b)


gfx::Point
BMenu::ScreenLocation()
{
	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	// NewGUI: a menu without supermenu opens where it is (Haiku: debugger())
	if (superMenu == nullptr || superItem == nullptr)
		return ConvertToScreen({0, 0});

	// Haiku's RightTop() + (1, 1) and LeftBottom() + (1, 1) of the inclusive
	// frame
	const gfx::Rect frame = superItem->Frame();
	gfx::Point point;
	if (superMenu->Layout() == BMenuLayout::InColumn)
		point = {frame.x1, frame.y0 + 1};
	else
		point = {frame.x0 + 1, frame.y1};

	return superMenu->ConvertToScreen(point);
}


namespace {


// The screen \a where is on, as Haiku's inclusive BScreen::Frame().
compat::BRect
ScreenFrameAt(gfx::Point where)
{
	gfx::Rect frame{0, 0, 65536, 65536};
	if (platform::Platform* platform = platform::Current()) {
		const std::vector<platform::DisplayInfo> displays = platform->Displays();
		if (!displays.empty())
			frame = displays.front().frame;
		for (const platform::DisplayInfo& display : displays) {
			if (display.frame.Contains(where)) {
				frame = display.frame;
				break;
			}
		}
	}
	return compat::BRect(frame.x0, frame.y0, frame.x1 - 1, frame.y1 - 1);
}


} // namespace


gfx::Rect
BMenu::_CalcFrame(gfx::Point where)
{
	// Ported from Haiku's _CalcFrame() with inclusive rectangles. NewGUI:
	// no frameShiftedLeft yet.
	using compat::BRect;
	const gfx::Rect bounds = Bounds();
	BRect frame(where.x, where.y, where.x + bounds.Width() - 1,
		where.y + bounds.Height() - 1);
	const BRect screenFrame = ScreenFrameAt(where);

	BMenu* superMenu = Supermenu();
	BMenuItem* superItem = Superitem();

	// TODO: Horrible hack:
	// When added to a BMenuField, a BPopUpMenu is the child of
	// a _BMCMenuBar_ to "fake" the menu hierarchy
	bool inMenuField = dynamic_cast<MenuFieldBar*>(superMenu) != nullptr;

	// Offset the menu field menu window left by the width of the checkmark
	// so that the text when the menu is closed lines up with the text when
	// the menu is open.
	if (inMenuField)
		frame.OffsetBy(-8.0f, 0.0f);

	if (superMenu == nullptr || superItem == nullptr || inMenuField) {
		// just move the window on screen
		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
		else if (frame.top < screenFrame.top)
			frame.OffsetBy(0, -frame.top);

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
		else if (frame.left < screenFrame.left)
			frame.OffsetBy(-frame.left, 0);
	} else if (superMenu->Layout() == BMenuLayout::InColumn) {
		const gfx::Rect itemFrame = superItem->Frame();
		if (frame.right > screenFrame.right)
			frame.OffsetBy(-(itemFrame.Width() - 1) - frame.Width() - 2, 0);

		if (frame.left < 0)
			frame.OffsetBy(-frame.left + 6, 0);

		if (frame.bottom > screenFrame.bottom)
			frame.OffsetBy(0, screenFrame.bottom - frame.bottom);
	} else {
		const gfx::Rect itemFrame = superItem->Frame();
		if (frame.bottom > screenFrame.bottom) {
			float spaceBelow = screenFrame.bottom - frame.top;
			float spaceOver = frame.top - screenFrame.top - (itemFrame.Height() - 1);
			if (spaceOver > spaceBelow)
				frame.OffsetBy(0, -(itemFrame.Height() - 1) - frame.Height() - 3);
		}

		if (frame.right > screenFrame.right)
			frame.OffsetBy(screenFrame.right - frame.right, 0);
	}

	return compat::FromBRect(frame);
}


BWindow*
BMenu::_RootWindow() const
{
	// The window of the menu bar (or menu) at the top of the hierarchy.
	const BMenu* menu = this;
	while (menu->Supermenu() != nullptr)
		menu = menu->Supermenu();
	return menu->Window();
}


bool
BMenu::_Show(bool selectFirstItem, BWindow* window)
{
	if (fPopup != nullptr || Window() != nullptr)
		return false;

	// NewGUI: a submenu, lent by its superitem to the popup's frame; a menu
	// without superitem (a BPopUpMenu) lent by its owner, in \a window.
	BMenuItem* superitem = Superitem();
	const bool standalone = superitem == nullptr && fSuper == nullptr;
	if (!standalone) {
		window = _RootWindow();
		if (superitem == nullptr || superitem->fOwnedSubmenu == nullptr)
			return false;
	}
	if (window == nullptr)
		return false;

	if (fSuper != nullptr) {
		const gfx::Rect bounds = fSuper->Bounds();
		const gfx::Point origin = fSuper->ConvertToScreen({bounds.x0, bounds.y0});
		fSuperbounds = {origin.x, origin.y, origin.x + bounds.Width(),
			origin.y + bounds.Height()};
	}

	// lay the items out for the size of the popup (Haiku:
	// _UpdateWindowViewSize())
	_CacheFontInfo();
	fUseCachedMenuLayout = false;
	_RelayoutIfNeeded();
	if (CountItems() == 0) {
		// room for "<empty>"
		const float width = TextSystem::Default().StringWidth("<empty>", FontRole::Plain);
		ResizeTo(std::ceil(width + fPad.x0 + fPad.x1) + 1,
			fFontHeight + fPad.y0 + fPad.y1 + 1);
	}

	const gfx::Rect frame = _CalcFrame(ScreenLocation());
	auto popup = std::make_unique<MenuFrame>(gfx::Rect{frame.x0 - 1, frame.y0 - 1,
		frame.x1 + 1, frame.y1 + 1}, this);
	std::unique_ptr<BMenu> self = standalone ? std::unique_ptr<BMenu>(this)
		: std::move(superitem->fOwnedSubmenu);
	fStandalone = standalone;
	MoveTo({1, 1});
	popup->AddChild(std::move(self));
	fPopup = window->OpenPopup(std::move(popup));

	if (selectFirstItem)
		_SelectItem(ItemAt(0), false);

	return true;
}


void
BMenu::_Hide()
{
	if (fPopup == nullptr)
		return;

	if (fSelected != nullptr)
		_SelectItem(nullptr);

	BWindow* window = Window();
	BView* popup = fPopup;
	fPopup = nullptr;

	// back to the superitem (or the owner), then the popup closes
	std::unique_ptr<BView> self = popup->RemoveChild(this);
	if (fStandalone)
		(void)self.release();
	else if (fSuperitem != nullptr)
		fSuperitem->fOwnedSubmenu.reset(static_cast<BMenu*>(self.release()));
	fStandalone = false;
	if (window != nullptr)
		(void)window->ClosePopup(popup);
}


bool
BMenu::_IsShown() const
{
	return fPopup != nullptr;
}


void
BMenu::_SelectItem(BMenuItem* item, bool showSubmenu, bool selectFirstItem)
{
	// Avoid deselecting and reselecting the same item which would cause flickering.
	if (item != fSelected) {
		if (fSelected != nullptr) {
			fSelected->Select(false);
			BMenu* subMenu = fSelected->Submenu();
			if (subMenu != nullptr && subMenu->_IsShown())
				subMenu->_Hide();
		}

		fSelected = item;
		if (fSelected != nullptr)
			fSelected->Select(true);
	}

	if (fSelected != nullptr && showSubmenu) {
		BMenu* subMenu = fSelected->Submenu();
		if (subMenu != nullptr && !subMenu->_IsShown()) {
			if (!subMenu->_Show(selectFirstItem)) {
				// something went wrong, deselect the item
				fSelected->Select(false);
				fSelected = nullptr;
			}
		}
	}
}


BMenuItem*
BMenu::_HitTestItems(gfx::Point where) const
{
	// if the point doesn't lie within the menu's
	// bounds, bail out immediately
	if (!Bounds().Contains(where))
		return nullptr;

	int32 itemCount = CountItems();
	for (int32 i = 0; i < itemCount; i++) {
		BMenuItem* item = ItemAt(i);
		if (item->Frame().Contains(where)
			&& dynamic_cast<BSeparatorItem*>(item) == nullptr) {
			return item;
		}
	}

	return nullptr;
}


// #pragma mark - MenuFrame


MenuFrame::MenuFrame(gfx::Rect frame, BMenu* menu)
	:
	BView(frame, "menu frame", kFollowAll, BViewFlag::WillDraw),
	fMenu(menu)
{
	SetViewColor(Palette::Default().Get(BColorWhich::MenuBackground));
}


void
MenuFrame::Draw(DrawContext& context)
{
	const gfx::Rect bounds = Bounds();
	if (fMenu != nullptr && fMenu->CountItems() == 0) {
		// Haiku's BMenuFrame: an empty menu shows "<empty>", disabled
		gfx::Rect rect = bounds;
		Look().DrawMenuBackground(context, rect,
			context.palette.Get(BColorWhich::MenuBackground));
		float left, top;
		fMenu->GetItemMargins(&left, &top, nullptr, nullptr);
		const FontHeight height = context.text.Height(FontRole::Plain);
		const compat::rgb_color disabled = compat::tint_color(
			compat::ToRgbColor(context.palette.Get(BColorWhich::MenuBackground)),
			compat::B_DISABLED_LABEL_TINT);
		const ShapedText shaped = context.text.Shape("<empty>", FontRole::Plain);
		context.canvas.DrawGlyphRun(shaped.Run(), {1 + left, 1 + top + height.ascent},
			gfx::Paint{.color = compat::ToColor(disabled)});
	}

	// NewGUI: the one pixel border of B_BORDERED_WINDOW_LOOK, which Haiku's
	// DefaultDecorator draws: the window border color, darkened by
	// B_DARKEN_3_TINT (its colors[5]).
	compat::CompatView view(context);
	view.SetHighColor(compat::tint_color(
		compat::ToRgbColor(context.palette.Get(BColorWhich::WindowBorder)),
		compat::B_DARKEN_3_TINT));
	view.StrokeRect(compat::BRect(bounds.x0, bounds.y0, bounds.x1 - 1, bounds.y1 - 1));
}


} // namespace ngui
