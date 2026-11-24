/*
 * Copyright 2001-2025 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/MenuItem.cpp and SeparatorItem.cpp
 * (commit b1d497bb69), without archiving and BeOS compatibility. NewGUI
 * (see MenuItem.h):
 * - fBounds is Haiku's inclusive rectangle; Frame() gives it half-open.
 * - Drawing goes to the DrawContext; the colors Haiku set as the menu's low
 *   and high color are LowColor() and HighColor().
 * - The trigger index is a byte offset into the UTF-8 label (Haiku: a
 *   character index from SetTrigger(), a byte offset from the menu's
 *   automatic triggers); the underline is measured by the text system.
 *
 * Authors (Haiku):
 *		Stefano Ceccherini, stefano.ceccherini@gmail.com
 *		Marc Flerackers, mflerackers@androme.be
 *		Bill Hayden, haydentech@users.sourceforge.net
 *		Olivier Milla
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/MenuItem.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>

#include <ngui/Clock.h>
#include <ngui/ControlLook.h>
#include <ngui/Menu.h>
#include <ngui/Message.h>
#include <ngui/Palette.h>
#include <ngui/SeparatorItem.h>
#include <ngui/TextSystem.h>
#include <ngui/Window.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Paint.h>

#include "MenuSymbols.h"
#include "compat/CompatView.h"


namespace ngui {


using compat::BPoint;
using compat::BRect;
using compat::FromBRect;


namespace {


constexpr float kMarkTint = 0.75f;

// map control key shortcuts to drawable Unicode characters
// cf. http://unicode.org/charts/PDF/U2190.pdf
constexpr std::array<const char*, 33> kUTF8ControlMap = {
	nullptr,
	"\xe2\x86\xb8", /* B_HOME U+21B8 */
	nullptr, nullptr,
	nullptr, /* B_END */
	nullptr, /* B_INSERT */
	nullptr, nullptr,
	"\xe2\x8c\xab", /* B_BACKSPACE U+232B */
	"\xe2\x86\xb9", /* B_TAB U+21B9 */
	"\xe2\x8f\x8e", /* B_ENTER, U+23CE */
	nullptr, /* B_PAGE_UP */
	nullptr, /* B_PAGE_DOWN */
	nullptr, nullptr, nullptr,
	nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
	nullptr, nullptr, nullptr, nullptr,
	"\xe2\x86\x90", /* B_LEFT_ARROW */
	"\xe2\x86\x92", /* B_RIGHT_ARROW */
	"\xe2\x86\x91", /* B_UP_ARROW */
	"\xe2\x86\x93", /* B_DOWN_ARROW */
	"\xe2\x90\xa3"  /* B_SPACE */
};

constexpr const char* kDeleteShortcutUTF8 = "\xe2\x8c\xa6"; /* B_DELETE U+2326 */

constexpr uint32 kSpace = 0x20;
constexpr uint32 kDelete = 0x7f;


BRect
Inclusive(const gfx::Rect& rect)
{
	return BRect(rect.x0, rect.y0, rect.x1, rect.y1);
}


// UTF-8 of the code point \a c.
std::string
ToUTF8(uint32 c)
{
	std::string out;
	if (c < 0x80) {
		out += static_cast<char>(c);
	} else if (c < 0x800) {
		out += static_cast<char>(0xc0 | (c >> 6));
		out += static_cast<char>(0x80 | (c & 0x3f));
	} else if (c < 0x10000) {
		out += static_cast<char>(0xe0 | (c >> 12));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (c & 0x3f));
	} else {
		out += static_cast<char>(0xf0 | (c >> 18));
		out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
		out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
		out += static_cast<char>(0x80 | (c & 0x3f));
	}
	return out;
}


// The number of bytes of the UTF-8 character starting with \a lead.
std::size_t
UTF8Length(unsigned char lead)
{
	if (lead < 0x80)
		return 1;
	if ((lead & 0xe0) == 0xc0)
		return 2;
	if ((lead & 0xf0) == 0xe0)
		return 3;
	if ((lead & 0xf8) == 0xf0)
		return 4;
	return 1;
}


} // namespace


BMenuItem::BMenuItem(std::string_view label, std::unique_ptr<BMessage> message,
	uint32 shortcut, BModifierFlags modifiers)
	:
	fLabel(label)
{
	SetMessage(std::move(message));

	fShortcut = shortcut;
	fModifiers = (fShortcut != 0 ? modifiers : BModifierFlags());
}


BMenuItem::BMenuItem(std::unique_ptr<BMenu> menu, std::unique_ptr<BMessage> message)
{
	SetMessage(std::move(message));
	_InitMenuData(std::move(menu));
}


BMenuItem::~BMenuItem()
{
	// NewGUI: the menu owns its items, so an item is destroyed after it was
	// taken out (Haiku removed it here). A submenu that is shown is in its
	// popup; hiding it gives it back.
	if (fSubmenu != nullptr && fOwnedSubmenu == nullptr)
		fSubmenu->_Hide();
	// NewGUI: an item destroyed while installed takes its shortcut along.
	if (fShortcut != 0 && fWindow != nullptr)
		fWindow->RemoveShortcut(fShortcut, fModifiers);
}


void
BMenuItem::SetLabel(std::string_view string)
{
	fLabel = string;

	if (fSuper != nullptr) {
		fSuper->InvalidateLayout();
		fSuper->Invalidate();
	}
}


void
BMenuItem::SetEnabled(bool enable)
{
	if (fEnabled == enable)
		return;

	fEnabled = enable;

	if (fSubmenu != nullptr)
		fSubmenu->SetEnabled(enable);

	if (fSuper != nullptr)
		fSuper->Invalidate();
}


void
BMenuItem::SetMarked(bool mark)
{
	fMark = mark;

	if (mark && fSuper != nullptr)
		fSuper->_ItemMarked(this);

	if (fSuper != nullptr)
		fSuper->Invalidate();
}


void
BMenuItem::SetTrigger(char trigger)
{
	fUserTrigger = trigger;

	// try uppercase letters first
	std::size_t pos = fLabel.find(static_cast<char>(std::toupper(
		static_cast<unsigned char>(trigger))));
	trigger = static_cast<char>(std::tolower(static_cast<unsigned char>(trigger)));

	if (pos == std::string::npos) {
		// take lowercase, too
		pos = fLabel.find(trigger);
	}

	if (pos != std::string::npos && trigger != 0) {
		// NewGUI: a byte offset
		fTriggerIndex = static_cast<int32>(pos);
		fTrigger = static_cast<unsigned char>(trigger);
	} else {
		fTrigger = 0;
		fTriggerIndex = -1;
	}

	if (fSuper != nullptr)
		fSuper->InvalidateLayout();
}


void
BMenuItem::SetShortcut(uint32 shortcut, BModifierFlags modifiers)
{
	if (fShortcut != 0 && fWindow != nullptr)
		fWindow->RemoveShortcut(fShortcut, fModifiers);

	if (shortcut != 0 && fWindow != nullptr)
		fWindow->_AddShortcut(&shortcut, &modifiers, this);

	fShortcut = shortcut;
	fModifiers = (fShortcut != 0 ? modifiers : BModifierFlags());

	if (fSuper != nullptr) {
		fSuper->InvalidateLayout();
		fSuper->Invalidate();
	}
}


const std::string&
BMenuItem::Label() const
{
	return fLabel;
}


bool
BMenuItem::IsEnabled() const
{
	if (fSubmenu)
		return fSubmenu->IsEnabled();

	if (!fEnabled)
		return false;

	return fSuper != nullptr ? fSuper->IsEnabled() : true;
}


bool
BMenuItem::IsMarked() const
{
	return fMark;
}


char
BMenuItem::Trigger() const
{
	return fUserTrigger;
}


uint32
BMenuItem::Shortcut(BModifierFlags* modifiers) const
{
	if (modifiers != nullptr)
		*modifiers = fModifiers;

	return fShortcut;
}


BMenu*
BMenuItem::Submenu() const
{
	return fSubmenu;
}


BMenu*
BMenuItem::Menu() const
{
	return fSuper;
}


gfx::Rect
BMenuItem::Frame() const
{
	return FromBRect(Inclusive(fBounds));
}


void
BMenuItem::GetContentSize(float* _width, float* _height)
{
	// TODO: Get rid of this. BMenu should handle this
	// automatically. Maybe it's not even needed, since our
	// BFont::Height() caches the value locally
	fSuper->_CacheFontInfo();

	fCachedWidth = TextSystem::Default().StringWidth(fLabel, FontRole::Plain);

	if (_width)
		*_width = std::ceil(fCachedWidth);
	if (_height)
		*_height = fSuper->fFontHeight;
}


std::string
BMenuItem::TruncateLabel(float maxWidth)
{
	return TextSystem::Default().Truncate(fLabel, maxWidth, TruncationMode::Middle,
		FontRole::Plain);
}


void
BMenuItem::DrawContent(DrawContext& context)
{
	fSuper->_CacheFontInfo();

	const gfx::Point location = ContentLocation();
	BPoint lineStart(location.x, location.y + fSuper->fAscent);

	float labelWidth;
	float labelHeight;
	GetContentSize(&labelWidth, &labelHeight);

	const gfx::Rect& padding = fSuper->fPad;
	float maxContentWidth = fSuper->MaxContentWidth();
	// NewGUI: Haiku's Frame().Width() is the pixel count - 1
	float frameWidth = maxContentWidth > 0 ? maxContentWidth
		: fSuper->Frame().Width() - 1 - padding.x0 - padding.x1;

	const gfx::Color high = HighColor(context);
	const TextSystem& text = context.text;
	const std::string label
		= std::round(frameWidth) >= std::round(labelWidth) ? fLabel : TruncateLabel(frameWidth);
	const ShapedText shaped = text.Shape(label, FontRole::Plain);
	context.canvas.DrawGlyphRun(shaped.Run(), {lineStart.x, lineStart.y},
		gfx::Paint{.color = high});

	if (fSuper->AreTriggersEnabled() && fTriggerIndex != -1
		&& static_cast<std::size_t>(fTriggerIndex) < fLabel.size()) {
		// NewGUI: the widths of the label up to and of the trigger
		const auto index = static_cast<std::size_t>(fTriggerIndex);
		lineStart.x += text.StringWidth(std::string_view(fLabel).substr(0, index),
			FontRole::Plain);
		lineStart.x--;
		lineStart.y++;

		BPoint lineEnd(lineStart);
		lineEnd.x += text.StringWidth(std::string_view(fLabel).substr(index,
			UTF8Length(static_cast<unsigned char>(fLabel[index]))), FontRole::Plain);

		compat::CompatView view(context);
		view.SetHighColor(compat::ToRgbColor(high));
		view.StrokeLine(lineStart, lineEnd);
	}
}


void
BMenuItem::Draw(DrawContext& context)
{
	if (_IsActivated()) {
		// fill in the background
		gfx::Rect frame(Frame());
		fSuper->Look().DrawMenuItemBackground(context, frame, LowColor(context),
			ControlState::Activated);
	}

	// draw content
	DrawContent(context);

	// draw extra symbols
	const BMenuLayout layout = fSuper->Layout();
	if (layout != BMenuLayout::InRow) {
		if (IsMarked())
			_DrawMarkSymbol(context);
	}

	if (layout == BMenuLayout::InColumn) {
		if (fShortcut != 0)
			_DrawShortcutSymbol(context, fSuper->fHasSubmenus);
		if (Submenu() != nullptr)
			_DrawSubmenuSymbol(context);
	}
}


void
BMenuItem::Highlight(bool /*highlight*/)
{
	fSuper->Invalidate();
}


bool
BMenuItem::IsSelected() const
{
	return fSelected;
}


gfx::Point
BMenuItem::ContentLocation() const
{
	const gfx::Rect& padding = fSuper->fPad;

	return {fBounds.x0 + padding.x0, fBounds.y0 + padding.y0};
}


gfx::Color
BMenuItem::LowColor(const DrawContext& context) const
{
	return _IsActivated() ? context.palette.Get(BColorWhich::MenuSelectedBackground)
		: context.palette.Get(BColorWhich::MenuBackground);
}


gfx::Color
BMenuItem::HighColor(const DrawContext& context) const
{
	bool isEnabled = IsEnabled();
	bool isSelected = IsSelected();

	if (isEnabled && isSelected)
		return context.palette.Get(BColorWhich::MenuSelectedItemText);
	if (isEnabled)
		return context.palette.Get(BColorWhich::MenuItemText);

	// Haiku: a tint of the menu's low color, which is LowColor() here
	const compat::rgb_color bgColor = compat::ToRgbColor(LowColor(context));
	if (bgColor.red + bgColor.green + bgColor.blue > 128 * 3)
		return compat::ToColor(compat::tint_color(bgColor, compat::B_DISABLED_LABEL_TINT));
	return compat::ToColor(compat::tint_color(bgColor, compat::B_LIGHTEN_2_TINT));
}


std::expected<void, Error>
BMenuItem::Invoke(const BMessage* message)
{
	if (!IsEnabled())
		return Failure(ErrorCode::NotSupported, "the item is disabled");

	if (fSuper != nullptr && fSuper->IsRadioMode())
		SetMarked(true);

	// NewGUI: no watching (Haiku: InvokeKind(), SendNotices())
	if (message == nullptr)
		message = Message();
	if (message == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message to send");

	BMessage clone = *message;
	(void)clone.AddInt32("index", fSuper != nullptr ? fSuper->IndexOf(this) : -1);
	(void)clone.AddInt64("when", Clock::Default().Now().count());
	(void)clone.AddPointer("source", this);

	return BInvoker::Invoke(&clone);
}


// #pragma mark - private


void
BMenuItem::Install(BWindow* window)
{
	if (fSubmenu != nullptr)
		fSubmenu->_Install(window);

	fWindow = window;

	uint32 shortcut = fShortcut;
	BModifierFlags modifiers = fModifiers;

	if (shortcut != 0 && fWindow != nullptr)
		fWindow->_AddShortcut(&shortcut, &modifiers, this);

	fShortcut = shortcut;
	fModifiers = (fShortcut != 0 ? modifiers : BModifierFlags());

	if (!Messenger().IsValid() && fWindow != nullptr)
		SetTarget(fWindow);
}


void
BMenuItem::Uninstall()
{
	if (fSubmenu != nullptr)
		fSubmenu->_Uninstall();

	if (fWindow != nullptr && Messenger() == BMessenger(fWindow))
		SetTarget(BMessenger());

	if (fShortcut != 0 && fWindow != nullptr)
		fWindow->RemoveShortcut(fShortcut, fModifiers);

	fWindow = nullptr;
}


void
BMenuItem::SetSuper(BMenu* super)
{
	if (fSubmenu != nullptr)
		fSubmenu->fSuper = super;

	fSuper = super;
}


void
BMenuItem::Select(bool selected)
{
	if (fSelected == selected)
		return;

	if (Submenu() != nullptr || IsEnabled()) {
		fSelected = selected;
		Highlight(selected);
	}
}


void
BMenuItem::SetAutomaticTrigger(int32 index, uint32 trigger)
{
	fTriggerIndex = index;
	fTrigger = trigger;
}


void
BMenuItem::_InitMenuData(std::unique_ptr<BMenu> menu)
{
	fOwnedSubmenu = std::move(menu);
	fSubmenu = fOwnedSubmenu.get();

	fSubmenu->fSuperitem = this;

	BMenuItem* item = fSubmenu->FindMarked();

	if (fSubmenu->IsRadioMode() && fSubmenu->IsLabelFromMarked() && item != nullptr)
		SetLabel(item->Label());
	else
		SetLabel(fSubmenu->Name());
}


bool
BMenuItem::_IsActivated() const
{
	return IsSelected() && (IsEnabled() || fSubmenu != nullptr);
}


void
BMenuItem::_DrawMarkSymbol(DrawContext& context)
{
	BRect r(Inclusive(fBounds));
	float leftMargin;
	fSuper->GetItemMargins(&leftMargin, nullptr, nullptr, nullptr);
	float gap = leftMargin / 4;
	r.right = r.left + leftMargin - gap;
	r.left += gap / 3;

	BPoint center(std::floor((r.left + r.right) / 2.0f),
		std::floor((r.top + r.bottom) / 2.0f));

	float size = std::min(r.Height() - 2, r.Width());
	r.top = std::floor(center.y - size / 2 + 0.5f);
	r.bottom = std::floor(center.y + size / 2 + 0.5f);
	r.left = std::floor(center.x - size / 2 + 0.5f);
	r.right = std::floor(center.x + size / 2 + 0.5f);

	center.x += 0.5f;
	center.y += 0.5f;
	size *= 0.3f;

	// Haiku's BShape, stroked with a pen of 2 (coordinates as they are)
	const std::array<gfx::Point, 3> arrowShape = {
		gfx::Point{center.x - size, center.y - size * 0.25f},
		gfx::Point{center.x - size * 0.25f, center.y + size},
		gfx::Point{center.x + size, center.y - size}
	};

	const gfx::Color color = compat::ToColor(compat::tint_color(
		compat::ToRgbColor(HighColor(context)), kMarkTint));
	context.canvas.StrokePolyline(arrowShape, false,
		gfx::Paint{.color = color, .strokeWidth = 2.0f});
}


void
BMenuItem::_DrawShortcutSymbol(DrawContext& context, bool submenus)
{
	const TextSystem& text = context.text;
	const float fontSize = text.FontSize(FontRole::Plain);
	const BRect bounds(Inclusive(fBounds));

	const gfx::Point content = ContentLocation();
	BPoint where(content.x, content.y);

	// Start from the right and walk our way back
	where.x = bounds.right - fontSize;

	// Leave space for the submenu arrow if any item in the menu has a submenu
	if (submenus)
		where.x -= bounds.Height() / 2;

	const float ascent = fSuper->fAscent;
	std::string symbol;
	if ((fShortcut <= kSpace && kUTF8ControlMap[fShortcut] != nullptr)
		|| fShortcut == kDelete) {
		// ASCII control character
		symbol = fShortcut == kDelete ? kDeleteShortcutUTF8 : kUTF8ControlMap[fShortcut];
	} else if (fShortcut >= kSpace && fShortcut < kDelete) {
		// ASCII printable character
		symbol = std::string(1, static_cast<char>(fShortcut));
	} else if (fShortcut < kSpace) {
		symbol = " ";
	} else {
		// convert Unicode code point to UTF-8 character string
		symbol = ToUTF8(fShortcut);
	}

	const ShapedText shaped = text.Shape(symbol, FontRole::Plain);
	context.canvas.DrawGlyphRun(shaped.Run(), {where.x, where.y + ascent},
		gfx::Paint{.color = HighColor(context)});

	where.y += (bounds.Height() - 11) / 2 - 1;
	where.x -= 4;

	// TODO: It would be nice to draw these taking into account the text (low)
	// color.
	// NewGUI: the symbols of Haiku's default keymap (Command is left Alt).
	compat::CompatView view(context);
	const auto drawSymbol = [&](MenuSymbol key) {
		where.x -= MenuSymbolWidth(key) + 1;
		view.DrawBitmap(MenuSymbolImage(key), where);
	};
	if (fModifiers.Has(BModifier::Command))
		drawSymbol(MenuSymbol::Alt);

	if (fModifiers.Has(BModifier::Control))
		drawSymbol(MenuSymbol::Control);

	if (fModifiers.Has(BModifier::Option))
		drawSymbol(MenuSymbol::Option);

	if (fModifiers.Has(BModifier::Shift))
		drawSymbol(MenuSymbol::Shift);
}


void
BMenuItem::_DrawSubmenuSymbol(DrawContext& context)
{
	const BRect bounds(Inclusive(fBounds));
	float symbolSize = std::round(bounds.Height() * 2 / 3);
	BRect rect(bounds);
	rect.left = rect.right - symbolSize;

	// 14px by default, scaled with font size up to right margin - padding
	BRect symbolRect(0, 0, symbolSize, symbolSize);
	symbolRect.OffsetTo(BPoint(rect.left,
		bounds.top + (bounds.Height() - symbolSize) / 2));

	gfx::Rect frame = FromBRect(symbolRect);
	fSuper->Look().DrawArrowShape(context, frame, HighColor(context), ArrowDirection::Right,
		{}, kMarkTint);
}


// #pragma mark - BSeparatorItem


BSeparatorItem::BSeparatorItem()
	:
	BMenuItem("", nullptr)
{
	BMenuItem::SetEnabled(false);
}


BSeparatorItem::~BSeparatorItem()
{
}


void
BSeparatorItem::SetEnabled(bool /*enable*/)
{
	// Don't do anything - we don't want to get enabled ever
}


void
BSeparatorItem::GetContentSize(float* _width, float* _height)
{
	if (Menu() != nullptr && Menu()->Layout() == BMenuLayout::InRow) {
		if (_width != nullptr)
			*_width = 2.0;
		if (_height != nullptr)
			*_height = 2.0;
	} else {
		if (_width != nullptr)
			*_width = 2.0;
		if (_height != nullptr) {
			const float fontSize = TextSystem::Default().FontSize(FontRole::Plain);
			const float height = std::floor((fontSize * 0.8f) / 2) * 2;
			*_height = std::max(4.0f, height);
		}
	}
}


void
BSeparatorItem::Draw(DrawContext& context)
{
	BMenu* menu = Menu();
	if (menu == nullptr)
		return;

	compat::CompatView view(context);
	const gfx::Rect frame = Frame();
	BRect rect(frame.x0, frame.y0, frame.x1 - 1, frame.y1 - 1);
	const compat::rgb_color lowColor = compat::ToRgbColor(LowColor(context));

	if (menu->Layout() == BMenuLayout::InRow) {
		const float startLeft = rect.left + (std::floor(rect.Width())) / 2;
		view.SetHighColor(compat::tint_color(lowColor, compat::B_DARKEN_1_TINT));
		view.StrokeLine(BPoint(startLeft, rect.top + 1.0f),
			BPoint(startLeft, rect.bottom - 1.0f));
		view.SetHighColor(compat::tint_color(lowColor, compat::B_LIGHTEN_2_TINT));
		view.StrokeLine(BPoint(startLeft + 1.0f, rect.top + 1.0f),
			BPoint(startLeft + 1.0f, rect.bottom - 1.0f));
	} else {
		const float startTop = rect.top + (std::floor(rect.Height())) / 2;
		view.SetHighColor(compat::tint_color(lowColor, compat::B_DARKEN_1_TINT));
		view.StrokeLine(BPoint(rect.left + 1.0f, startTop),
			BPoint(rect.right - 1.0f, startTop));
		view.SetHighColor(compat::tint_color(lowColor, compat::B_LIGHTEN_2_TINT));
		view.StrokeLine(BPoint(rect.left + 1.0f, startTop + 1.0f),
			BPoint(rect.right - 1.0f, startTop + 1.0f));
	}
}


} // namespace ngui
