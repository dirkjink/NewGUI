/*
 * Copyright 2001-2015 Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/Alert.cpp (commit b1d497bb69),
 * without archiving, scripting and the sound (see Alert.h). The type icons
 * are NewGUI's own SVG icons (BIconUtils::GetSystemIcon()), drawn anew at
 * the window's scale. NewGUI: Go() is static and asynchronous; the key filter reads
 * the first byte of "bytes" (Haiku: "byte"); B_MODAL_WINDOW is a bordered
 * window with BWindowFeel::ModalApp.
 *
 * Authors (Haiku):
 *		Axel Dörfler, axeld@pinc-software.de
 *		Erik Jaesler, erik@cgsoftware.com
 *		John Scipione, jscipione@gmail.com
 */


#include <ngui/Alert.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include <ngui/AppDefs.h>
#include <ngui/Bitmap.h>
#include <ngui/Button.h>
#include <ngui/ControlLook.h>
#include <ngui/GroupLayout.h>
#include <ngui/IconUtils.h>
#include <ngui/Invoker.h>
#include <ngui/LayoutBuilder.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Message.h>
#include <ngui/MessageFilter.h>
#include <ngui/Palette.h>
#include <ngui/SpaceLayoutItem.h>
#include <ngui/TextSystem.h>
#include <ngui/TextView.h>
#include <ngui/platform/Platform.h>

#include "compat/InterfaceDefs.h"


namespace ngui {


namespace {


constexpr std::uint32_t kAlertButtonMsg = FourCharCode("ALTB");

constexpr float kButtonOffsetSpacing = 62.0f;
constexpr float kButtonUsualWidth = 55.0f;
constexpr float kIconStripeWidthFactor = 5.0f;

constexpr float kWindowMinWidth = 310.0f;
constexpr float kWindowOffsetMinWidth = 335.0f;


// Haiku's BControlLook::ComposeIconSize(B_LARGE_ICON): 32 pixels, scaled
// with the plain font (12 pixels: 32).
float
LargeIconSize()
{
	return std::ceil(32.0f * TextSystem::Default().FontSize(FontRole::Plain) / 12.0f);
}


// The screen the point is on (the primary one, without a platform 640 ×
// 480, as Haiku's fallback).
gfx::Rect
ScreenFrame(std::optional<gfx::Point> where = std::nullopt)
{
	if (platform::Platform* platform = platform::Current()) {
		const std::vector<platform::DisplayInfo> displays = platform->Displays();
		if (where.has_value()) {
			for (const platform::DisplayInfo& display : displays) {
				if (display.frame.Contains(*where))
					return display.frame;
			}
		}
		for (const platform::DisplayInfo& display : displays) {
			if (display.primary)
				return display.frame;
		}
		if (!displays.empty())
			return displays.front().frame;
	}
	return {0, 0, 640, 480};
}


// The system icon of an alert type (Haiku's _CreateTypeIcon()), empty for
// none.
std::string_view
TypeIconName(BAlertType type)
{
	switch (type) {
		case BAlertType::Info:
			return "dialog-information";
		case BAlertType::Idea:
			return "dialog-idea";
		case BAlertType::Warning:
			return "dialog-warning";
		case BAlertType::Stop:
			return "dialog-error";
		case BAlertType::Empty:
			break;
	}
	return {};
}


// The space left of the text (Haiku's TAlertView): a darker stripe and the
// icon.
class AlertIconView : public BView {
public:
	AlertIconView()
		:
		BView("TAlertView", BViewFlag::WillDraw)
	{
		SetViewUIColor(BColorWhich::PanelBackground);
	}

	// Haiku's SetBitmap(); \a typeIcon: the system icon \a name, which is
	// drawn anew for a window scale other than 1.
	void SetBitmap(std::unique_ptr<BBitmap> icon, std::string_view typeIcon = {})
	{
		const bool resized = (fIcon == nullptr) != (icon == nullptr)
			|| (icon != nullptr && fIcon->Bounds() != icon->Bounds());
		fIcon = std::move(icon);
		fTypeIcon = typeIcon;
		fScaledIcon.reset();
		if (resized)
			InvalidateLayout();
		Invalidate();
	}

	BBitmap* Bitmap() const
	{
		return fIcon.get();
	}

	void GetPreferredSize(float* width, float* height) override
	{
		const float spacing = Look().DefaultLabelSpacing(TextSystem::Default());
		const gfx::Rect icon = fIcon != nullptr ? fIcon->Bounds()
			: gfx::Rect{0, 0, LargeIconSize(), LargeIconSize()};
		if (width != nullptr)
			*width = spacing * 3 + icon.Width();
		if (height != nullptr)
			*height = spacing + icon.Height();
	}

	gfx::Size MinSize() override
	{
		float width;
		float height;
		GetPreferredSize(&width, &height);
		return {width, height};
	}

	gfx::Size PreferredSize() override
	{
		return MinSize();
	}

	gfx::Size MaxSize() override
	{
		return {MinSize().w, kSizeUnlimited};
	}

	void Draw(DrawContext& context) override
	{
		const gfx::Rect bounds = Bounds();
		const float stripeRight = kIconStripeWidthFactor
			* Look().DefaultLabelSpacing(context.text);
		const gfx::Color base = ViewColor().value_or(
			context.palette.Get(BColorWhich::PanelBackground));
		const gfx::Color stripe = compat::ToColor(compat::tint_color(compat::ToRgbColor(base),
			compat::B_DARKEN_1_TINT));
		// Haiku's inclusive right edge
		context.canvas.FillRect({bounds.x0, bounds.y0, bounds.x0 + stripeRight + 1, bounds.y1},
			gfx::Paint{.color = stripe});

		if (fIcon == nullptr)
			return;

		// Haiku draws the bitmap at (3 spacings, 1 spacing), B_OP_ALPHA.
		const float spacing = Look().DefaultLabelSpacing(context.text);
		const gfx::Rect iconBounds = fIcon->Bounds();
		const gfx::Rect target{spacing * 3, spacing, spacing * 3 + iconBounds.Width(),
			spacing + iconBounds.Height()};
		const BBitmap* icon = fIcon.get();
		// NewGUI: a type icon is drawn for the device pixels
		if (!fTypeIcon.empty() && context.scale != 1) {
			if (fScaledIcon == nullptr || fScale != context.scale) {
				auto scaled = std::make_unique<BBitmap>(gfx::Rect{0, 0,
					std::ceil(iconBounds.Width() * context.scale),
					std::ceil(iconBounds.Height() * context.scale)}, BColorSpace::RGBA32);
				if (BIconUtils::GetSystemIcon(fTypeIcon, scaled.get()).has_value()) {
					fScaledIcon = std::move(scaled);
					fScale = context.scale;
				}
			}
			if (fScaledIcon != nullptr)
				icon = fScaledIcon.get();
		}
		const gfx::Rect pixels = icon->Bounds();
		context.canvas.DrawImage(icon->Image(), {0, 0, pixels.Width(), pixels.Height()},
			target);
	}

private:
	std::unique_ptr<BBitmap> fIcon;
	std::string_view fTypeIcon;
	std::unique_ptr<BBitmap> fScaledIcon;
	float fScale = 1;
};


} // namespace


// Presses the button whose key was typed (Haiku's _BAlertFilter_).
class BAlert::KeyFilter : public BMessageFilter {
public:
	explicit KeyFilter(BAlert* alert)
		:
		BMessageFilter(kKeyDown),
		fAlert(alert)
	{
	}

	BFilterResult Filter(BMessage* message, BHandler** /*target*/) override
	{
		if (message->what != kKeyDown)
			return BFilterResult::Dispatch;

		const auto bytes = message->FindString("bytes");
		if (!bytes.has_value() || bytes->size() != 1)
			return BFilterResult::Dispatch;

		const char byte = (*bytes)[0];
		for (int32 i = 0; i < fAlert->CountButtons(); ++i) {
			if (byte == fAlert->Shortcut(i) && fAlert->ButtonAt(i) != nullptr) {
				fAlert->ButtonAt(i)->KeyDown(" ");
				return BFilterResult::Skip;
			}
		}
		return BFilterResult::Dispatch;
	}

private:
	BAlert*	fAlert;
};


BAlert::BAlert()
	:
	BWindow({0, 0, 100, 100}, "", BWindowLook::Bordered, BWindowFeel::ModalApp,
		BWindowFlag::NotClosable | BWindowFlag::NotResizable)
{
	_Init({}, {}, {}, {}, BButtonWidth::FromWidest, BButtonSpacing::Even, BAlertType::Info);
}


BAlert::BAlert(std::string_view title, std::string_view text, std::string_view button1,
	std::string_view button2, std::string_view button3, BButtonWidth width, BAlertType type)
	:
	BWindow({0, 0, 100, 100}, title, BWindowLook::Bordered, BWindowFeel::ModalApp,
		BWindowFlag::NotClosable | BWindowFlag::NotResizable)
{
	_Init(text, button1, button2, button3, width, BButtonSpacing::Even, type);
}


BAlert::BAlert(std::string_view title, std::string_view text, std::string_view button1,
	std::string_view button2, std::string_view button3, BButtonWidth width,
	BButtonSpacing spacing, BAlertType type)
	:
	BWindow({0, 0, 100, 100}, title, BWindowLook::Bordered, BWindowFeel::ModalApp,
		BWindowFlag::NotClosable | BWindowFlag::NotResizable)
{
	_Init(text, button1, button2, button3, width, spacing, type);
}


BAlert::~BAlert()
{
}


BAlertType
BAlert::Type() const
{
	return fType;
}


void
BAlert::SetType(BAlertType type)
{
	fType = type;
}


void
BAlert::SetIcon(std::unique_ptr<BBitmap> bitmap)
{
	static_cast<AlertIconView*>(fIconView)->SetBitmap(std::move(bitmap));
}


BBitmap*
BAlert::Icon() const
{
	return static_cast<AlertIconView*>(fIconView)->Bitmap();
}


void
BAlert::_CreateTypeIcon()
{
	// The icons are NewGUI's own SVG icons (Haiku: the app_server's
	// resources); without a vector icon renderer there is none.
	const std::string_view name = TypeIconName(fType);
	if (name.empty())
		return;
	const float size = LargeIconSize();
	auto icon = std::make_unique<BBitmap>(gfx::Rect{0, 0, size, size}, BColorSpace::RGBA32);
	if (!BIconUtils::GetSystemIcon(name, icon.get()).has_value())
		return;
	static_cast<AlertIconView*>(fIconView)->SetBitmap(std::move(icon), name);
}


void
BAlert::SetText(std::string_view text)
{
	TextView()->SetText(text);
}


void
BAlert::SetButtonSpacing(BButtonSpacing spacing)
{
	fButtonSpacing = spacing;
}


void
BAlert::SetButtonWidth(BButtonWidth width)
{
	fButtonWidth = width;
}


void
BAlert::SetShortcut(int32 index, char key)
{
	if (index >= 0 && static_cast<std::size_t>(index) < fKeys.size())
		fKeys[static_cast<std::size_t>(index)] = key;
}


char
BAlert::Shortcut(int32 index) const
{
	if (index >= 0 && static_cast<std::size_t>(index) < fKeys.size())
		return fKeys[static_cast<std::size_t>(index)];

	return 0;
}


std::expected<BMessenger, Error>
BAlert::Go(std::unique_ptr<BAlert> alert, std::unique_ptr<BInvoker> invoker)
{
	if (alert == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no alert");
	alert->fInvoker = std::move(invoker);
	return _Go(std::move(alert));
}


std::expected<BMessenger, Error>
BAlert::Go(std::unique_ptr<BAlert> alert, std::function<void(int32 which)> done)
{
	if (alert == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no alert");
	alert->fDone = std::move(done);
	return _Go(std::move(alert));
}


void
BAlert::MessageReceived(BMessage* message)
{
	if (message->what != kAlertButtonMsg)
		return BWindow::MessageReceived(message);

	const std::int32_t which = message->GetInt32("which", -1);
	if (which < 0 || fReported)
		return;

	// NewGUI: reported once, even if a second button is pressed before the
	// alert has quit
	fReported = true;
	if (fInvoker != nullptr) {
		if (BMessage* out = fInvoker->Message()) {
			if (!out->ReplaceInt32("which", 0, which).has_value())
				(void)out->AddInt32("which", which);
			(void)fInvoker->Invoke();
		}
	}
	if (fDone)
		fDone(which);

	(void)PostMessage(kQuitRequested);
}


void
BAlert::AddButton(std::string_view label, char key)
{
	if (label.empty())
		return;

	auto button = std::unique_ptr<BButton>(_CreateButton(CountButtons(), label));
	BButton* added = button.get();
	fButtons.push_back(added);
	fKeys.push_back(key);
	(void)fButtonLayout->AddView(std::move(button));
	SetDefaultButton(added);
}


int32
BAlert::CountButtons() const
{
	return static_cast<int32>(fButtons.size());
}


BButton*
BAlert::ButtonAt(int32 index) const
{
	if (index >= 0 && static_cast<std::size_t>(index) < fButtons.size())
		return fButtons[static_cast<std::size_t>(index)];

	return nullptr;
}


BTextView*
BAlert::TextView() const
{
	return fTextView;
}


gfx::Point
BAlert::AlertPosition(float width, float height)
{
	// Haiku's static AlertPosition(): on the screen of the calling window
	gfx::Point result{100, 100};

	std::optional<gfx::Point> where;
	if (BWindow* window = BWindow::_DispatchingWindow())
		where = gfx::Point{window->Frame().x0, window->Frame().y0};
	const gfx::Rect screenFrame = ScreenFrame(where);

	result.x = screenFrame.x0 + (screenFrame.Width() / 2.0f) - (width / 2.0f);
	result.y = screenFrame.y0 + (screenFrame.Height() / 4.0f) - std::ceil(height / 3.0f);

	return result;
}


// #pragma mark - private


void
BAlert::_Init(std::string_view text, std::string_view button1, std::string_view button2,
	std::string_view button3, BButtonWidth buttonWidth, BButtonSpacing spacing,
	BAlertType type)
{
	auto iconView = std::make_unique<AlertIconView>();
	fIconView = iconView.get();

	auto textView = std::make_unique<BTextView>("_tv_");
	fTextView = textView.get();
	fTextView->MakeEditable(false);
	fTextView->MakeSelectable(false);
	// NewGUI: after MakeEditable(), which adopts the document colors
	fTextView->SetViewUIColor(BColorWhich::PanelBackground);
	fTextView->SetTextUIColor(BColorWhich::PanelText);
	fTextView->SetWordWrap(true);
	fTextView->SetExplicitMaxSize({kSizeUnlimited, kSizeUnlimited});

	auto buttonLayout = std::make_unique<BGroupLayout>(BOrientation::Horizontal,
		kUseHalfItemSpacing);
	fButtonLayout = buttonLayout.get();

	SetType(type);
	SetButtonWidth(buttonWidth);
	SetButtonSpacing(spacing);
	SetText(text);

	BLayoutBuilder::Group<>(this, BOrientation::Horizontal, 0)
		.Add(std::move(iconView))
		.AddGroup(BOrientation::Vertical, kUseHalfItemSpacing)
			.SetInsets(kUseHalfItemInsets)
			.Add(std::move(textView))
			.AddGroup(BOrientation::Horizontal, 0)
				.AddGlue()
				.Add(std::unique_ptr<BLayoutItem>(std::move(buttonLayout)))
			.End()
		.End();

	AddButton(button1);
	AddButton(button2);
	AddButton(button3);

	(void)AddCommonFilter(std::make_unique<KeyFilter>(this));
}


BButton*
BAlert::_CreateButton(int32 which, std::string_view label)
{
	auto message = std::make_unique<BMessage>(kAlertButtonMsg);
	(void)message->AddInt32("which", which);

	const std::string name = "_b" + std::to_string(which) + "_";
	return new BButton(name, label, std::move(message));
}


void
BAlert::_Prepare(const gfx::Rect* parentFrame)
{
	// Tweaks the layout according to the configuration.
	const float fontFactor = TextSystem::Default().FontSize(FontRole::Plain) / 11.0f;

	auto* iconView = static_cast<AlertIconView*>(fIconView);
	if (iconView->Bitmap() == nullptr)
		_CreateTypeIcon();

	if (fButtonWidth == BButtonWidth::AsUsual) {
		const float usualWidth = kButtonUsualWidth * fontFactor;
		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			if (button->MinSize().w < usualWidth)
				button->SetExplicitSize({usualWidth, kSizeUnset});
		}
	} else if (fButtonWidth == BButtonWidth::FromWidest) {
		// Get width of widest label
		float maxWidth = 0;
		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			float width;
			button->GetPreferredSize(&width, nullptr);

			if (width > maxWidth)
				maxWidth = width;
		}
		for (int32 index = 0; index < CountButtons(); index++) {
			BButton* button = ButtonAt(index);
			button->SetExplicitSize({maxWidth, kSizeUnset});
		}
	}

	if (fButtonSpacing == BButtonSpacing::Offset && CountButtons() > 1) {
		// Insert some strut
		(void)fButtonLayout->AddItem(1,
			BSpaceLayoutItem::CreateHorizontalStrut(kButtonOffsetSpacing * fontFactor));
	}

	// Position the alert so that it is centered vertically but offset a bit
	// horizontally in the parent window's frame or, if unavailable, the
	// screen frame.
	const float minWindowWidth = (fButtonSpacing == BButtonSpacing::Offset
		? kWindowOffsetMinWidth : kWindowMinWidth) * fontFactor;
	GetLayout()->SetExplicitMinSize({minWindowWidth, kSizeUnset});

	ResizeToPreferred();

	MoveTo(_AlertPosition(parentFrame != nullptr ? *parentFrame : ScreenFrame()));
}


gfx::Point
BAlert::_AlertPosition(const gfx::Rect& frame) const
{
	// Haiku's private BWindow::AlertPosition(frame), without a decorator
	// (the alert has no tab)
	const float width = Bounds().Width();
	const float height = Bounds().Height();

	gfx::Point point{frame.x0 + (frame.Width() / 2.0f) - (width / 2.0f),
		frame.y0 + (frame.Height() / 4.0f) - std::ceil(height / 3.0f)};

	const gfx::Rect screenFrame = ScreenFrame(gfx::Point{frame.x0, frame.y0});
	if (frame == screenFrame) {
		// reference frame is screen frame, skip the below adjustments
		return point;
	}

	// clip the x position within the horizontal edges of the screen
	if (point.x < screenFrame.x0)
		point.x = screenFrame.x0;
	else if (point.x + width > screenFrame.x1)
		point.x = screenFrame.x1 - width;

	// lower the window down if it is covering the window tab
	if (point.y < frame.y0)
		point.y = frame.y0;

	// clip the y position within the vertical edges of the screen
	if (point.y < screenFrame.y0)
		point.y = screenFrame.y0;
	else if (point.y + height > screenFrame.y1)
		point.y = screenFrame.y1 - height;

	return point;
}


std::expected<BMessenger, Error>
BAlert::_Go(std::unique_ptr<BAlert> alert)
{
	if (alert->CountButtons() == 0)
		return Failure(ErrorCode::InvalidArgument, "an alert must have at least one button");

	// in front of the window whose thread calls, if any
	std::optional<gfx::Rect> parentFrame;
	if (BWindow* window = BWindow::_DispatchingWindow())
		parentFrame = window->Frame();

	alert->_Prepare(parentFrame.has_value() ? &*parentFrame : nullptr);
	alert->Show();
	return BLooper::Run(std::move(alert));
}


// #pragma mark - accessibility


void
BAlert::GetAccessibleInfo(AccessibleInfo* info) const
{
	BWindow::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	info->role = AccessibleRole::Alert;
	if (const BTextView* text = TextView(); text != nullptr && info->description.empty())
		info->description = text->Text();
}


} // namespace ngui
