/*
 * Copyright 2001-2020 Haiku Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/TextControl.cpp (commit
 * b1d497bb69), without archiving, scripting and icons. NewGUI (see
 * TextControl.h):
 * - Sizes are pixel counts; Haiku's inclusive rectangles become half-open
 *   ones with the same edges. The label item's width is one more than
 *   Haiku's.
 * - SetText() has no InvokeKind() guard (NewGUI has no InvokeNotify()).
 * - The text view's colors are set whether or not the control is in a
 *   window (Haiku: in AttachedToWindow() and SetEnabled() with a window).
 * - FrameResized() is BControl's: views are redrawn whole.
 *
 * Authors (Haiku):
 *		Frans van Nispen (xlr8@tref.nl)
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Ingo Weinhold <bonefish@cs.tu-berlin.de>
 *		John Scipione <jscipione@gmail.com>
 *		Joseph Groover <looncraz@looncraz.net>
 */


#include <ngui/TextControl.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include <ngui/AbstractLayoutItem.h>
#include <ngui/LayoutUtils.h>
#include <ngui/Palette.h>
#include <ngui/PropertyInfo.h>
#include <ngui/TextSystem.h>
#include <ngui/TextView.h>
#include <ngui/Window.h>

#include "TextInput.h"
#include "ScriptingSupport.h"
#include "compat/InterfaceDefs.h"


namespace ngui {


namespace {


constexpr float kFrameMargin = 2;
constexpr float kLabelInputSpacing = 3;

// No frame yet (Haiku's invalid BRect()).
constexpr gfx::Rect kNoFrame = {0, 0, -1, -1};


bool
IsValidFrame(const gfx::Rect& frame)
{
	return frame.x1 >= frame.x0 && frame.y1 >= frame.y0;
}


// Haiku's hsl_color, enough for MarkAsInvalid(): \a color with the
// lightness of \a lightnessOf, kept between 0.15 and 0.95.
gfx::Color
WithLightness(const gfx::Color& color, const gfx::Color& lightnessOf)
{
	const auto lightness = [](const gfx::Color& c) {
		return (std::max({c.r, c.g, c.b}) + std::min({c.r, c.g, c.b})) / 2;
	};

	// hue and saturation of color
	const float maxC = std::max({color.r, color.g, color.b});
	const float minC = std::min({color.r, color.g, color.b});
	const float l = (maxC + minC) / 2;
	float h = 0;
	float s = 0;
	if (maxC != minC) {
		const float d = maxC - minC;
		s = l > 0.5f ? d / (2 - maxC - minC) : d / (maxC + minC);
		if (maxC == color.r)
			h = (color.g - color.b) / d + (color.g < color.b ? 6.0f : 0.0f);
		else if (maxC == color.g)
			h = (color.b - color.r) / d + 2;
		else
			h = (color.r - color.g) / d + 4;
		h /= 6;
	}

	const float newL = std::clamp(lightness(lightnessOf), 0.15f, 0.95f);

	// back to RGB
	if (s == 0)
		return {newL, newL, newL, color.a};
	const float q = newL < 0.5f ? newL * (1 + s) : newL + s - newL * s;
	const float p = 2 * newL - q;
	const auto hueToRgb = [p, q](float t) {
		if (t < 0)
			t += 1;
		if (t > 1)
			t -= 1;
		if (t < 1.0f / 6)
			return p + (q - p) * 6 * t;
		if (t < 0.5f)
			return q;
		if (t < 2.0f / 3)
			return p + (q - p) * (2.0f / 3 - t) * 6;
		return p;
	};
	return {hueToRgb(h + 1.0f / 3), hueToRgb(h), hueToRgb(h - 1.0f / 3), color.a};
}


} // namespace


// #pragma mark - LabelLayoutItem


class BTextControl::LabelLayoutItem : public BAbstractLayoutItem {
public:
	explicit LabelLayoutItem(BTextControl* parent)
		:
		fParent(parent),
		fView(parent)
	{
	}

	~LabelLayoutItem() override
	{
		if (fParent != nullptr)
			fParent->fLayoutData.labelLayoutItem = nullptr;
	}

	gfx::Rect FrameInParent() const
	{
		const gfx::Rect parent = fParent->Frame();
		return {fFrame.x0 - parent.x0, fFrame.y0 - parent.y0, fFrame.x1 - parent.x0,
			fFrame.y1 - parent.y0};
	}

	bool IsVisible() override
	{
		return fParent != nullptr && !fParent->IsHidden();
	}

	void SetVisible(bool /*visible*/) override
	{
		// not allowed
	}

	gfx::Rect Frame() override
	{
		return fFrame;
	}

	void SetFrame(gfx::Rect frame) override
	{
		fFrame = frame;
		if (fParent != nullptr)
			fParent->_UpdateFrame();
	}

	BView* View() override
	{
		return fView;
	}

	gfx::Size BaseMinSize() override
	{
		if (fParent == nullptr)
			return {0, 0};

		fParent->_ValidateLayoutData();
		if (fParent->Label().empty())
			return {0, 0};

		// NewGUI: a pixel count
		return {fParent->fLayoutData.labelWidth
				+ fParent->Look().DefaultLabelSpacing(TextSystem::Default()) + 1,
			fParent->fLayoutData.labelHeight};
	}

	gfx::Size BaseMaxSize() override
	{
		return BaseMinSize();
	}

	gfx::Size BasePreferredSize() override
	{
		return BaseMinSize();
	}

	BAlignment BaseAlignment() override
	{
		return {BHorizontalAlignment::UseFullWidth, BVerticalAlignment::UseFullHeight};
	}

	void ParentGone()
	{
		fParent = nullptr;
	}

private:
	BTextControl*		fParent;
	BView*				fView;
	gfx::Rect			fFrame = kNoFrame;
};


// #pragma mark - TextViewLayoutItem


class BTextControl::TextViewLayoutItem : public BAbstractLayoutItem {
public:
	explicit TextViewLayoutItem(BTextControl* parent)
		:
		fParent(parent),
		fView(parent)
	{
		// by default the part right of the divider shall have an unlimited
		// maximum width
		SetExplicitMaxSize({kSizeUnlimited, kSizeUnset});
	}

	~TextViewLayoutItem() override
	{
		if (fParent != nullptr)
			fParent->fLayoutData.textViewLayoutItem = nullptr;
	}

	gfx::Rect FrameInParent() const
	{
		const gfx::Rect parent = fParent->Frame();
		return {fFrame.x0 - parent.x0, fFrame.y0 - parent.y0, fFrame.x1 - parent.x0,
			fFrame.y1 - parent.y0};
	}

	bool IsVisible() override
	{
		return fParent != nullptr && !fParent->IsHidden();
	}

	void SetVisible(bool /*visible*/) override
	{
		// not allowed
	}

	gfx::Rect Frame() override
	{
		return fFrame;
	}

	void SetFrame(gfx::Rect frame) override
	{
		fFrame = frame;
		if (fParent != nullptr)
			fParent->_UpdateFrame();
	}

	BView* View() override
	{
		return fView;
	}

	gfx::Size BaseMinSize() override
	{
		if (fParent == nullptr)
			return {0, 0};

		fParent->_ValidateLayoutData();
		gfx::Size size = fParent->fLayoutData.textViewMin;
		size.w += 2 * kFrameMargin;
		size.h += 2 * kFrameMargin;
		return size;
	}

	gfx::Size BaseMaxSize() override
	{
		gfx::Size size(BaseMinSize());
		size.w = kSizeUnlimited;
		return size;
	}

	gfx::Size BasePreferredSize() override
	{
		gfx::Size size(BaseMinSize());
		// puh, no idea...
		size.w = 100;
		return size;
	}

	BAlignment BaseAlignment() override
	{
		return {BHorizontalAlignment::UseFullWidth, BVerticalAlignment::UseFullHeight};
	}

	void ParentGone()
	{
		fParent = nullptr;
	}

private:
	BTextControl*		fParent;
	BView*				fView;
	gfx::Rect			fFrame = kNoFrame;
};


// #pragma mark - BTextControl


BTextControl::BTextControl(gfx::Rect frame, std::string_view name, std::string_view label,
	std::string_view text, std::unique_ptr<BMessage> message, BResizingMode resizingMode,
	BViewFlags flags)
	:
	BControl(frame, name, label, std::move(message), resizingMode,
		flags | BViewFlag::FrameEvents)
{
	_InitData(label);
	_InitText(text);
	_ValidateLayout();
}


BTextControl::BTextControl(std::string_view name, std::string_view label,
	std::string_view text, std::unique_ptr<BMessage> message, BViewFlags flags)
	:
	BControl(name, label, std::move(message), flags | BViewFlag::FrameEvents)
{
	_InitData(label);
	_InitText(text);
	_ValidateLayout();
}


BTextControl::BTextControl(std::string_view label, std::string_view text,
	std::unique_ptr<BMessage> message)
	:
	BControl(std::string_view{}, label, std::move(message),
		BViewFlag::WillDraw | BViewFlag::Navigable | BViewFlag::FrameEvents)
{
	_InitData(label);
	_InitText(text);
	_ValidateLayout();
}


BTextControl::~BTextControl()
{
	// NewGUI: the items stay in their layouts until BView's destructor
	// takes them out, but must not reach this part of the control any more.
	if (fLayoutData.labelLayoutItem != nullptr)
		fLayoutData.labelLayoutItem->ParentGone();
	if (fLayoutData.textViewLayoutItem != nullptr)
		fLayoutData.textViewLayoutItem->ParentGone();
}


void
BTextControl::SetText(std::string_view text)
{
	fText->SetText(text);

	if (fText->IsFocus()) {
		fText->SetInitialText();
		fText->SelectAll();
	}

	fText->Invalidate();
}


std::string_view
BTextControl::Text() const
{
	return fText->Text();
}


int32
BTextControl::TextLength() const
{
	return fText->TextLength();
}


void
BTextControl::MarkAsInvalid(bool invalid)
{
	const ControlStateFlags look = fLook;

	if (invalid)
		fLook |= ControlState::Invalid;
	else
		fLook = ControlStateFlags::FromRaw(fLook.Raw()
			& ~ControlStateFlags(ControlState::Invalid).Raw());

	if (look != fLook) {
		_UpdateTextViewColors(IsEnabled());
		Invalidate();
	}
}


BTextView*
BTextControl::TextView() const
{
	return fText;
}


void
BTextControl::SetModificationMessage(std::unique_ptr<BMessage> message)
{
	fModificationMessage = std::move(message);
}


BMessage*
BTextControl::ModificationMessage() const
{
	return fModificationMessage.get();
}


void
BTextControl::SetAlignment(BHorizontalAlignment labelAlignment,
	BHorizontalAlignment textAlignment)
{
	fText->SetAlignment(textAlignment);

	if (fLabelAlign != labelAlignment) {
		fLabelAlign = labelAlignment;
		Invalidate();
	}
}


void
BTextControl::GetAlignment(BHorizontalAlignment* label, BHorizontalAlignment* text) const
{
	if (label != nullptr)
		*label = fLabelAlign;
	if (text != nullptr)
		*text = fText->Alignment();
}


void
BTextControl::SetDivider(float position)
{
	fDivider = std::floor(position + 0.5f);

	_LayoutTextView();

	if (Window() != nullptr) {
		fText->Invalidate();
		Invalidate();
	}
}


float
BTextControl::Divider() const
{
	return fDivider;
}


void
BTextControl::Draw(DrawContext& context)
{
	const bool enabled = IsEnabled();
	const bool active = fText->IsFocus() && Window() != nullptr && Window()->IsActive();

	const gfx::Rect textFrame = fText->Frame();
	gfx::Rect rect = {textFrame.x0 - 2, textFrame.y0 - 2, textFrame.x1 + 2, textFrame.y1 + 2};
	const gfx::Color base = ViewColor().value_or(
		context.palette.Get(BColorWhich::PanelBackground));
	const gfx::Color text = context.palette.Get(BColorWhich::PanelText);

	ControlStateFlags flags = fLook;
	if (!enabled)
		flags |= ControlState::Disabled;

	if (active)
		flags |= ControlState::Focused;

	const ControlLook& look = Look();
	look.DrawTextControlBorder(context, rect, base, flags);

	if (!Label().empty()) {
		gfx::Rect labelRect;
		if (fLayoutData.labelLayoutItem != nullptr) {
			labelRect = fLayoutData.labelLayoutItem->FrameInParent();
		} else {
			// Haiku's inclusive right edge, one less
			labelRect = Bounds();
			labelRect.x1 = fDivider - kLabelInputSpacing + 1;
		}

		look.DrawLabel(context, Label(), labelRect, base, flags,
			BAlignment{fLabelAlign, BVerticalAlignment::Middle}, &text);
	}
}


void
BTextControl::MouseDown(gfx::Point /*where*/)
{
	if (!fText->IsFocus())
		fText->MakeFocus(true);
}


void
BTextControl::AttachedToWindow()
{
	BControl::AttachedToWindow();

	_UpdateTextViewColors(IsEnabled());
	fText->MakeEditable(IsEnabled());
}


void
BTextControl::MakeFocus(bool state)
{
	if (state != fText->IsFocus()) {
		fText->MakeFocus(state);

		if (state)
			fText->SelectAll();
	}
}


void
BTextControl::SetEnabled(bool enable)
{
	if (IsEnabled() == enable)
		return;

	fText->MakeEditable(enable);
	if (enable)
		fText->SetFlags(fText->Flags() | BViewFlag::Navigable);
	else {
		fText->SetFlags(BViewFlags::FromRaw(fText->Flags().Raw()
			& ~BViewFlags(BViewFlag::Navigable).Raw()));
	}

	_UpdateTextViewColors(enable);

	fText->Invalidate();

	BControl::SetEnabled(enable);
}


void
BTextControl::WindowActivated(bool active)
{
	BControl::WindowActivated(active);

	if (fText->IsFocus()) {
		// invalidate to remove/show focus indication
		Invalidate();
		fText->Invalidate();
	}
}


void
BTextControl::GetPreferredSize(float* width, float* height)
{
	_ValidateLayoutData();

	if (width != nullptr) {
		float minWidth = fLayoutData.min.w;
		if (Label().empty() && !Flags().Has(BViewFlag::SupportsLayout))
			minWidth = std::max(minWidth, Bounds().Width());
		*width = minWidth;
	}

	if (height != nullptr)
		*height = fLayoutData.min.h;
}


void
BTextControl::ResizeToPreferred()
{
	BView::ResizeToPreferred();

	fDivider = 0.0f;
	if (!Label().empty())
		fDivider = std::ceil(TextSystem::Default().StringWidth(Label(), FontRole::Plain)) + 2.0f;

	_LayoutTextView();
}


gfx::Size
BTextControl::MinSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), fLayoutData.min);
}


gfx::Size
BTextControl::MaxSize()
{
	_ValidateLayoutData();

	gfx::Size max = fLayoutData.min;
	max.w = kSizeUnlimited;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), max);
}


gfx::Size
BTextControl::PreferredSize()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), fLayoutData.min);
}


BAlignment
BTextControl::LayoutAlignment()
{
	_ValidateLayoutData();
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		{BHorizontalAlignment::Left, BVerticalAlignment::Middle});
}


std::unique_ptr<BLayoutItem>
BTextControl::CreateLabelLayoutItem()
{
	if (fLayoutData.labelLayoutItem != nullptr)
		return nullptr;

	auto item = std::make_unique<LabelLayoutItem>(this);
	fLayoutData.labelLayoutItem = item.get();
	return item;
}


std::unique_ptr<BLayoutItem>
BTextControl::CreateTextViewLayoutItem()
{
	if (fLayoutData.textViewLayoutItem != nullptr)
		return nullptr;

	auto item = std::make_unique<TextViewLayoutItem>(this);
	fLayoutData.textViewLayoutItem = item.get();
	return item;
}


void
BTextControl::LayoutInvalidated(bool /*descendants*/)
{
	fLayoutData.valid = false;
}


void
BTextControl::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		return;

	// If the user set a layout, we let the base class version call its
	// hook.
	if (GetLayout() != nullptr) {
		BView::DoLayout();
		return;
	}

	_ValidateLayoutData();

	// validate current size
	gfx::Size size = {Bounds().Width(), Bounds().Height()};
	if (size.w < fLayoutData.min.w)
		size.w = fLayoutData.min.w;

	if (size.h < fLayoutData.min.h)
		size.h = fLayoutData.min.h;

	gfx::Rect textFrame;

	// divider
	float divider = 0;
	if (fLayoutData.textViewLayoutItem != nullptr) {
		if (fLayoutData.labelLayoutItem != nullptr) {
			// We have layout items. They define the divider location.
			divider = std::abs(fLayoutData.textViewLayoutItem->Frame().x0
				- fLayoutData.labelLayoutItem->Frame().x0);
		}
		textFrame = fLayoutData.textViewLayoutItem->FrameInParent();
	} else {
		if (fLayoutData.labelWidth > 0) {
			divider = fLayoutData.labelWidth
				+ Look().DefaultLabelSpacing(TextSystem::Default());
		}
		textFrame = {divider, 0, size.w, size.h};
	}

	// place the text view and set the divider
	textFrame = {textFrame.x0 + kFrameMargin, textFrame.y0 + kFrameMargin,
		textFrame.x1 - kFrameMargin, textFrame.y1 - kFrameMargin};
	BLayoutUtils::AlignInFrame(fText, textFrame);
	fText->SetTextRect({0, 0, textFrame.Width(), textFrame.Height()});

	fDivider = divider;

	Invalidate();
}


// #pragma mark - private


void
BTextControl::_UpdateTextViewColors(bool enable)
{
	const Palette& palette = Palette::Default();
	gfx::Color textColor = palette.Get(BColorWhich::DocumentText);
	gfx::Color viewColor = palette.Get(BColorWhich::DocumentBackground);
	const gfx::Color panel = ViewColor().value_or(palette.Get(BColorWhich::PanelBackground));

	if (!enable) {
		textColor = compat::ToColor(compat::disable_color(compat::ToRgbColor(textColor),
			compat::ToRgbColor(panel)));
		viewColor = compat::ToColor(compat::disable_color(compat::ToRgbColor(panel),
			compat::ToRgbColor(viewColor)));
	} else if (fLook.Has(ControlState::Invalid)) {
		viewColor = WithLightness(palette.Get(BColorWhich::Failure), viewColor);
	}

	fText->SetTextColor(textColor);
	fText->SetViewColor(viewColor);
}


void
BTextControl::_InitData(std::string_view label)
{
	fText = nullptr;
	fModificationMessage.reset();
	fLabelAlign = BHorizontalAlignment::Left;
	fDivider = 0.0f;
	fLayoutData = LayoutData();

	// NewGUI: Haiku's Width() is one less than the pixel count
	if (!label.empty())
		fDivider = std::floor((Bounds().Width() - 1) / 2.0f);

	fLook = {};
}


void
BTextControl::_InitText(std::string_view initialText)
{
	const gfx::Rect bounds = Bounds();
	const gfx::Rect frame = {fDivider + kFrameMargin, bounds.y0 + kFrameMargin,
		bounds.x1 - kFrameMargin, bounds.y1 - kFrameMargin};
	const gfx::Rect textRect = {0, 0, std::max(frame.Width(), 0.0f),
		std::max(frame.Height(), 0.0f)};

	BViewFlags flags = BViewFlag::WillDraw | BViewFlag::FrameEvents;
	if (Flags().Has(BViewFlag::Navigable))
		flags |= BViewFlag::Navigable;
	auto text = std::make_unique<TextInput>(frame, textRect,
		BResizingMode{BHorizontalFollow::LeftRight, BVerticalFollow::TopBottom}, flags);
	fText = static_cast<TextInput*>(AddChild(std::move(text)));

	SetText(initialText);
	fText->SetAlignment(BHorizontalAlignment::Left);

	// Although this is not strictly initializing the text view,
	// it cannot be done while fText is NULL, so it resides here.
	if (Flags().Has(BViewFlag::Navigable)) {
		BView::SetFlags(BViewFlags::FromRaw(Flags().Raw()
			& ~BViewFlags(BViewFlag::Navigable).Raw()));
	}

	_UpdateTextViewColors(IsEnabled());
}


void
BTextControl::_ValidateLayout()
{
	_ValidateLayoutData();

	ResizeTo(Bounds().Width(), fLayoutData.min.h);

	_LayoutTextView();
}


void
BTextControl::_LayoutTextView()
{
	gfx::Rect frame;
	if (fLayoutData.textViewLayoutItem != nullptr) {
		frame = fLayoutData.textViewLayoutItem->FrameInParent();
	} else {
		frame = Bounds();
		frame.x0 = fDivider;
	}

	// apply the frame margin
	frame = {frame.x0 + kFrameMargin, frame.y0 + kFrameMargin, frame.x1 - kFrameMargin,
		frame.y1 - kFrameMargin};

	fText->MoveTo({frame.x0, frame.y0});
	fText->ResizeTo(std::max(frame.Width(), 0.0f), std::max(frame.Height(), 0.0f));
}


void
BTextControl::_UpdateFrame()
{
	if (fLayoutData.textViewLayoutItem == nullptr)
		return;

	const gfx::Rect textFrame = fLayoutData.textViewLayoutItem->Frame();
	gfx::Rect labelFrame = kNoFrame;
	if (fLayoutData.labelLayoutItem != nullptr)
		labelFrame = fLayoutData.labelLayoutItem->Frame();

	gfx::Rect frame;
	if (IsValidFrame(labelFrame)) {
		frame = {std::min(textFrame.x0, labelFrame.x0), std::min(textFrame.y0, labelFrame.y0),
			std::max(textFrame.x1, labelFrame.x1), std::max(textFrame.y1, labelFrame.y1)};

		// update divider
		fDivider = std::abs(textFrame.x0 - labelFrame.x0);
	} else {
		frame = textFrame;
		fDivider = 0;
	}

	// update our frame
	MoveTo({frame.x0, frame.y0});
	const gfx::Rect oldBounds = Bounds();
	ResizeTo(frame.Width(), frame.Height());
	const gfx::Rect newBounds = Bounds();

	// If the size changes, ResizeTo() will trigger a relayout, otherwise
	// we need to do that explicitly.
	if (newBounds.Width() == oldBounds.Width() && newBounds.Height() == oldBounds.Height())
		Relayout();
}


void
BTextControl::_ValidateLayoutData()
{
	if (fLayoutData.valid)
		return;

	const TextSystem& system = TextSystem::Default();
	if (!Label().empty()) {
		const FontHeight fh = system.Height(FontRole::Plain);
		fLayoutData.labelWidth = std::ceil(system.StringWidth(Label(), FontRole::Plain));
		// NewGUI: a pixel count, one more than Haiku's height
		fLayoutData.labelHeight = std::ceil(fh.ascent) + std::ceil(fh.descent) + 1;
	} else {
		fLayoutData.labelWidth = 0;
		fLayoutData.labelHeight = 0;
	}

	// compute the minimal divider
	float divider = 0;
	if (fLayoutData.labelWidth > 0)
		divider = fLayoutData.labelWidth + Look().DefaultLabelSpacing(system);

	// If we shan't do real layout, we let the current divider take influence.
	if (!Flags().Has(BViewFlag::SupportsLayout))
		divider = std::max(divider, fDivider);

	// get the minimal (== preferred) text view size
	fLayoutData.textViewMin = fText->MinSize();

	// compute our minimal (== preferred) size
	gfx::Size min(fLayoutData.textViewMin);
	min.w += 2 * kFrameMargin;
	min.h += 2 * kFrameMargin;

	if (divider > 0)
		min.w += divider;

	if (fLayoutData.labelHeight > min.h)
		min.h = fLayoutData.labelHeight;

	fLayoutData.min = min;

	fLayoutData.valid = true;
	ResetLayoutInvalidation();
}


// #pragma mark - Scripting


namespace {


// Ported from Haiku: src/kits/interface/TextControl.cpp (commit
// b1d497bb69): the text as "Value"; "Text" as the specification names it
// (20.7).
const std::vector<BPropertyInfoEntry> kTextControlProperties = {
	{"Value", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Its text.", 0,
		{BTypeCode::String}},
	{"Text", {kGetProperty, kSetProperty}, {kDirectSpecifier}, "Its text.", 0,
		{BTypeCode::String}},
};


} // namespace


void
BTextControl::MessageReceived(BMessage* message)
{
	if ((message->what == kGetProperty || message->what == kSetProperty)
		&& message->HasSpecifiers()) {
		const auto specifier = message->GetCurrentSpecifier();
		if (specifier.has_value()
			&& (specifier->property == "Value" || specifier->property == "Text")) {
			BMessage reply(kReply);
			if (message->what == kGetProperty) {
				// Do not allow stealing passwords via scripting (Haiku's
				// BTextView)
				if (TextView()->IsTypingHidden()) {
					ReplyScriptingError(*message, Error{ErrorCode::NotSupported,
						"the text is hidden"});
					return;
				}
				const auto added = reply.AddString("result", Text());
				detail::SendScriptingReply(*message, std::move(reply), added);
				return;
			}
			if (const auto text = message->FindString("data")) {
				SetText(*text);
				detail::SendScriptingReply(*message, std::move(reply), {});
				return;
			}
		}
	}
	BControl::MessageReceived(message);
}


BHandler*
BTextControl::ResolveSpecifier(BMessage* message, std::int32_t index, const BMessage& specifier,
	std::uint32_t form, std::string_view property)
{
	if (BPropertyInfo(kTextControlProperties).FindMatch(*message, 0, form, property) >= 0)
		return this;
	return BControl::ResolveSpecifier(message, index, specifier, form, property);
}

std::expected<void, Error>
BTextControl::GetSupportedSuites(BMessage* data)
{
	if (data == nullptr)
		return Failure(ErrorCode::InvalidArgument, "no message for the suites");
	if (auto added = data->AddString("suites", "suite/vnd.Be-text-control"); !added)
		return added;
	if (auto added = data->AddMessage("messages",
			BPropertyInfo(kTextControlProperties).ToMessage()); !added) {
		return added;
	}
	return BControl::GetSupportedSuites(data);
}


// #pragma mark - accessibility


void
BTextControl::GetAccessibleInfo(AccessibleInfo* info) const
{
	BControl::GetAccessibleInfo(info);
	if (info == nullptr)
		return;
	const BTextView* text = TextView();
	const bool hidden = text != nullptr && text->IsTypingHidden();
	info->role = hidden ? AccessibleRole::PasswordInput : AccessibleRole::TextInput;
	if (!hidden)
		info->value = Text();
	if (text != nullptr && !text->IsEditable())
		info->states |= AccessibleState::ReadOnly;
	if (IsEnabled()) {
		info->states |= AccessibleState::Focusable;
		info->actions |= AccessibleAction::Focus;
		if (text != nullptr && text->IsEditable())
			info->actions |= AccessibleAction::SetValue;
	}
}


std::expected<void, Error>
BTextControl::PerformAccessibleAction(AccessibleAction action, const BMessage& data)
{
	if (!IsEnabled())
		return Failure(ErrorCode::NotSupported, "the text control is disabled");
	BTextView* text = TextView();
	switch (action) {
		case AccessibleAction::Click:
		case AccessibleAction::Focus:
			text->MakeFocus(true);
			return {};
		case AccessibleAction::SetValue:
		{
			const auto value = data.FindString("value");
			if (!value.has_value())
				return Failure(ErrorCode::InvalidArgument, "SetValue needs a \"value\"");
			if (!text->IsEditable())
				return Failure(ErrorCode::NotSupported, "the text cannot be edited");
			SetText(*value);
			(void)Invoke();
			return {};
		}
		default:
			return BView::PerformAccessibleAction(action, data);
	}
}


} // namespace ngui
