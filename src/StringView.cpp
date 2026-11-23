/*
 * Copyright 2001-2015, Haiku, Inc. All rights reserved.
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 *
 * Ported from Haiku: src/kits/interface/StringView.cpp (commit b1d497bb69),
 * without archiving and scripting. NewGUI: sizes are pixel counts (Haiku's
 * plus 1); Draw() places the lines on the same pixels as Haiku, whose
 * inclusive Bounds() end one pixel before the half-open ones; measured and
 * drawn with the plain font of the text system.
 *
 * Authors (Haiku):
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Frans van Nispen (xlr8@tref.nl)
 *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */


#include <ngui/StringView.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <ngui/LayoutUtils.h>
#include <ngui/gfx/Canvas.h>
#include <ngui/gfx/Paint.h>


namespace ngui {


namespace {


// The lines of \a text, as BString::Split("\n", false).
std::vector<std::string_view>
Lines(std::string_view text)
{
	std::vector<std::string_view> lines;
	std::size_t start = 0;
	while (true) {
		const std::size_t end = text.find('\n', start);
		if (end == std::string_view::npos) {
			lines.push_back(text.substr(start));
			break;
		}
		lines.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	return lines;
}


} // namespace


BStringView::BStringView(gfx::Rect frame, std::string_view name, std::string_view text,
	BResizingMode resizingMode, BViewFlags flags)
	:
	BView(frame, name, resizingMode, flags | BViewFlag::FullUpdateOnResize),
	fText(text),
	fPreferredSize{_StringWidth(text) + 1, -1}
{
}


BStringView::BStringView(std::string_view name, std::string_view text, BViewFlags flags)
	:
	BView(name, flags | BViewFlag::FullUpdateOnResize),
	fText(text),
	fPreferredSize{_StringWidth(text) + 1, -1}
{
}


BStringView::~BStringView()
{
}


void
BStringView::AttachedToWindow()
{
	// Haiku takes the parent's view color (and the panel text color as high
	// color, which Draw() uses).
	if (BView* parent = Parent())
		SetViewColor(parent->ViewColor());

	BView::AttachedToWindow();
}


void
BStringView::GetPreferredSize(float* width, float* height)
{
	_ValidatePreferredSize();

	if (width)
		*width = fPreferredSize.w;

	if (height)
		*height = fPreferredSize.h;
}


gfx::Size
BStringView::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


gfx::Size
BStringView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


gfx::Size
BStringView::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


void
BStringView::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);

	// Resize the width only for B_ALIGN_LEFT (if its large enough already, that is)
	if (Bounds().Width() > width && Alignment() != BHorizontalAlignment::Left)
		width = Bounds().Width();

	BView::ResizeTo(width, height);
}


BAlignment
BStringView::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment{fAlign, BVerticalAlignment::Middle});
}


void
BStringView::Draw(DrawContext& context)
{
	if (fText.empty())
		return;

	const TextSystem& text = context.text;
	const FontHeight fontHeight = text.Height(FontRole::Plain);

	const gfx::Rect bounds = Bounds();
	// Haiku's inclusive bounds: the last column and row of pixels.
	const float boundsWidth = bounds.Width() - 1;
	const float boundsBottom = bounds.y1 - 1;

	const std::vector<std::string_view> lines = Lines(fText);
	const gfx::Paint paint{.color = context.palette.Get(BColorWhich::PanelText)};
	for (std::size_t i = 0; i < lines.size(); i++) {
		std::string line(lines[i]);
		float width = text.StringWidth(line, FontRole::Plain);
		if (fTruncation.has_value() && width > boundsWidth) {
			// The string needs to be truncated
			// TODO: we should cache this
			line = text.Truncate(line, boundsWidth, *fTruncation, FontRole::Plain);
			width = text.StringWidth(line, FontRole::Plain);
		}

		float y = (bounds.y0 + boundsBottom - std::ceil(fontHeight.descent))
			- std::ceil(fontHeight.ascent + fontHeight.descent + fontHeight.leading)
				* static_cast<float>(lines.size() - i - 1);
		float x;
		switch (fAlign) {
			case BHorizontalAlignment::Right:
				x = boundsWidth - width;
				break;

			case BHorizontalAlignment::Center:
				x = (boundsWidth - width) / 2.0f;
				break;

			default:
				x = 0.0f;
				break;
		}

		const ShapedText shaped = text.Shape(line, FontRole::Plain);
		context.canvas.DrawGlyphRun(shaped.Run(), {x, y}, paint);
	}
}


void
BStringView::SetText(std::string_view newText)
{
	if (newText == fText)
		return;

	fText = newText;

	float newStringWidth = _StringWidth(fText) + 1;
	if (fPreferredSize.w != newStringWidth) {
		fPreferredSize.w = newStringWidth;
		InvalidateLayout();
	}

	Invalidate();
}


const std::string&
BStringView::Text() const
{
	return fText;
}


void
BStringView::SetAlignment(BHorizontalAlignment alignment)
{
	fAlign = alignment;
	Invalidate();
}


BHorizontalAlignment
BStringView::Alignment() const
{
	return fAlign;
}


void
BStringView::SetTruncation(std::optional<TruncationMode> truncation)
{
	if (fTruncation != truncation) {
		fTruncation = truncation;
		Invalidate();
	}
}


std::optional<TruncationMode>
BStringView::Truncation() const
{
	return fTruncation;
}


void
BStringView::LayoutInvalidated(bool /*descendants*/)
{
	// invalidate cached preferred size
	fPreferredSize.h = -1;
}


gfx::Size
BStringView::_ValidatePreferredSize()
{
	if (fPreferredSize.h < 0) {
		// height
		const FontHeight fontHeight = TextSystem::Default().Height(FontRole::Plain);

		const float lines = static_cast<float>(Lines(fText).size());
		fPreferredSize.h = std::ceil(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading) * lines;
		// NewGUI: a pixel count
		fPreferredSize.h += 1;

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


float
BStringView::_StringWidth(std::string_view text)
{
	if (text.empty())
		return 0.0f;

	float maxWidth = 0.0f;
	for (std::string_view line : Lines(text)) {
		float width = TextSystem::Default().StringWidth(line, FontRole::Plain);
		if (maxWidth < width)
			maxWidth = width;
	}

	return maxWidth;
}


} // namespace ngui
