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

#include "MenuSymbols.h"


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

	if (Superitem() == nullptr)
		_Install(Window());

	fUseCachedMenuLayout = false;

	_RelayoutIfNeeded();
}


void
BMenu::DetachedFromWindow()
{
	BView::DetachedFromWindow();

	if (Superitem() == nullptr)
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

	// NewGUI: the menu field's menu bar comes with W3c.
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
	if (Window() != nullptr && Parent() != nullptr) {
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
			// NewGUI: the menu field's menu bar (override frame) comes
			// with W3c.
			_ComputeColumnLayout(index, bestFit, moveItems, frame);
			break;

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
		if (Parent() != nullptr)
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
	gfx::Rect& frame)
{
	bool command = false;
	bool control = false;
	bool shift = false;
	bool option = false;
	bool submenu = false;

	if (index > 0)
		frame = ItemAt(index - 1)->fBounds;
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


} // namespace ngui
